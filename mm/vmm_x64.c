/*
 * Project Tsukasa — x86_64 Virtual Memory Manager
 *
 * Copyright (C) 2025-2026 frosty (@enafrosty) and Project Tsukasa contributors.
 *
 * Project Tsukasa was created and is maintained by frosty (@enafrosty).
 * This program is free software: you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation, either version 3 of the License, or (at your
 * option) any later version. See the top-level LICENSE file.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 */

#include <stddef.h>
#include <stdint.h>

#include "mm/vmm_x64.h"
#include "mm/pmm.h"
#include "mm/vm_space.h"
#include "proc/process.h"
#include "arch/x86_64/cpu/idt.h"
#include "include/kprintf.h"
#include "include/lapic.h"
#include "include/smp.h"
#include "include/spinlock.h"

static uint64_t g_hhdm_offset;

#ifdef __x86_64__

typedef struct {
    uint64_t entries[512];
} page_table_t;

#define VMM_X64_ADDR_MASK 0x000FFFFFFFFFF000ULL
#define VMM_X64_TLB_INVLPG_THRESHOLD 32
#define VMM_X64_FB_MMIO_BASE  0x0000000030000000ULL
#define VMM_X64_FB_MMIO_LIMIT 0x0000000040000000ULL

#define PAT_MEM_UC       0x00ULL
#define PAT_MEM_WC       0x01ULL
#define PAT_MEM_WT       0x04ULL
#define PAT_MEM_WB       0x06ULL
#define PAT_MEM_UC_MINUS 0x07ULL

#define VMM_X64_PTE_CACHE_MASK (VMM_X64_PTE_PAT | VMM_X64_PTE_CACHE_DISABLE | VMM_X64_PTE_WRITE_THROUGH)
#define VMM_X64_PDE_CACHE_MASK (VMM_X64_PDE_PAT | VMM_X64_PTE_CACHE_DISABLE | VMM_X64_PTE_WRITE_THROUGH)

_Static_assert(
    ((PAT_MEM_WB << 0) | (PAT_MEM_WT << 8) | (PAT_MEM_UC_MINUS << 16) | (PAT_MEM_UC << 24) |
     (PAT_MEM_WC << 32) | (PAT_MEM_WT << 40) | (PAT_MEM_UC_MINUS << 48) | (PAT_MEM_UC << 56)) ==
    VMM_X64_PAT_VALUE,
    "VMM_X64_PAT_VALUE must encode PA0=WB, PA3=UC, PA4=WC");

/* concurrent shm attaches). */
static spinlock_t g_vmm_lock = SPINLOCK_INIT;
static volatile int g_pat_supported = 0;
static volatile int g_nxe_supported = 0;
static volatile int g_nxe_enabled = 0;

typedef struct tlb_shootdown_req {
    uint64_t pml4_phys;
    uintptr_t va;
    size_t page_count;
    volatile uint64_t ack_mask;
} tlb_shootdown_req_t;

static spinlock_t g_tlb_lock = SPINLOCK_INIT;
static tlb_shootdown_req_t g_tlb_req;
static volatile uint64_t g_tlb_shootdown_ipi_count = 0;

static int map_pages_nolock(uint64_t pml4_phys, uintptr_t virt_addr,
                            uint64_t phys_addr, size_t page_count,
                            uint64_t map_flags);
static int unmap_pages_nolock(uint64_t pml4_phys, uintptr_t virt_addr,
                              size_t page_count);

static uint64_t read_cr3(void)
{
    uint64_t cr3;
    __asm__ volatile ("mov %%cr3, %0" : "=r"(cr3));
    return cr3;
}

static void write_cr3(uint64_t cr3)
{
    __asm__ volatile ("mov %0, %%cr3" : : "r"(cr3) : "memory");
}

static void invlpg_addr(uintptr_t addr)
{
    __asm__ volatile ("invlpg (%0)" : : "r"(addr) : "memory");
}

static page_table_t *table_virt(uint64_t phys)
{
    return (page_table_t *)(uintptr_t)vmm_phys_to_virt(phys & VMM_X64_ADDR_MASK);
}

static uint64_t alloc_table_phys(void)
{
    uint64_t phys = (uint64_t)pmm_alloc_pages(1);
    page_table_t *tbl;
    if (!phys)
        return 0;

    tbl = table_virt(phys);
    for (uint32_t i = 0; i < 512; i++)
        tbl->entries[i] = 0;

    return phys;
}

static int validate_span(uintptr_t virt_addr, uint64_t phys_addr, size_t page_count, int has_phys)
{
    uint64_t span;
    int user_range;
    int kernel_range;

    if (page_count == 0)
        return -1;
    if (!paging_is_page_aligned_uintptr(virt_addr))
        return -1;
    if (has_phys && !paging_is_page_aligned_u64(phys_addr))
        return -1;

    span = (uint64_t)page_count * (uint64_t)PAGE_SIZE;
    if (span == 0 || span > (uint64_t)SIZE_MAX)
        return -1;

    user_range = paging_range_is_user(virt_addr, (size_t)span);
    kernel_range = paging_range_is_kernel(virt_addr, (size_t)span);
    if (!user_range && !kernel_range)
        return -1;

    return 0;
}

static int validate_map_request(uintptr_t virt_addr,
                                uint64_t phys_addr,
                                size_t page_count,
                                uint64_t map_flags)
{
    uint64_t span;
    int is_user;

    if ((map_flags & PAGING_MAP_READ) == 0)
        return -1;

    if (validate_span(virt_addr, phys_addr, page_count, 1) != 0)
        return -1;

    span = (uint64_t)page_count * (uint64_t)PAGE_SIZE;
    is_user = paging_range_is_user(virt_addr, (size_t)span);
    if (is_user && ((map_flags & PAGING_MAP_USER) == 0))
        return -1;
    if (!is_user && (map_flags & PAGING_MAP_USER))
        return -1;

    return 0;
}

static uint64_t pte_flags_from_map(uint64_t map_flags)
{
    uint64_t pte = 0;

    if (map_flags & PAGING_MAP_WRITE)
        pte |= VMM_X64_PTE_WRITABLE;
    if (map_flags & PAGING_MAP_USER)
        pte |= VMM_X64_PTE_USER;
    if (map_flags & PAGING_MAP_GLOBAL)
        pte |= VMM_X64_PTE_GLOBAL;
    if (map_flags & PAGING_MAP_DEVICE) {
        pte |= VMM_X64_PTE_CACHE_DISABLE | VMM_X64_PTE_WRITE_THROUGH;
    } else if (map_flags & PAGING_MAP_WC) {
        if (g_pat_supported)
            pte |= VMM_X64_PTE_PAT;
        else
            pte |= VMM_X64_PTE_CACHE_DISABLE | VMM_X64_PTE_WRITE_THROUGH;
    }
    if ((map_flags & PAGING_MAP_EXEC) == 0 && g_nxe_enabled)
        pte |= VMM_X64_PTE_NO_EXEC;

    return pte;
}

static uint64_t map_flags_from_legacy(uint64_t legacy_flags)
{
    uint64_t map = PAGING_MAP_READ;

    if (legacy_flags & VMM_X64_PTE_WRITABLE)
        map |= PAGING_MAP_WRITE;
    if (legacy_flags & VMM_X64_PTE_USER)
        map |= PAGING_MAP_USER;
    if ((legacy_flags & VMM_X64_PTE_NO_EXEC) == 0)
        map |= PAGING_MAP_EXEC;
    if (legacy_flags & VMM_X64_PTE_GLOBAL)
        map |= PAGING_MAP_GLOBAL;
    if (legacy_flags & (VMM_X64_PTE_CACHE_DISABLE | VMM_X64_PTE_WRITE_THROUGH))
        map |= PAGING_MAP_DEVICE;
    else if (legacy_flags & VMM_X64_PTE_PAT)
        map |= PAGING_MAP_WC;

    return map;
}

static int walk_to_pt(uint64_t pml4_phys,
                      uintptr_t virt_addr,
                      int create,
                      int user,
                      page_table_t **pt_out)
{
    page_table_t *pml4;
    page_table_t *pdpt;
    page_table_t *pd;
    page_table_t *pt;
    uint64_t pml4_idx;
    uint64_t pdpt_idx;
    uint64_t pd_idx;
    uint64_t pt_idx;
    uint64_t table_flags;

    if (!pt_out || !pml4_phys)
        return -1;

    pml4 = table_virt(pml4_phys);
    pml4_idx = ((uint64_t)virt_addr >> 39) & 0x1FFULL;
    pdpt_idx = ((uint64_t)virt_addr >> 30) & 0x1FFULL;
    pd_idx = ((uint64_t)virt_addr >> 21) & 0x1FFULL;
    pt_idx = ((uint64_t)virt_addr >> 12) & 0x1FFULL;
    table_flags = VMM_X64_PTE_PRESENT | VMM_X64_PTE_WRITABLE;
    if (user)
        table_flags |= VMM_X64_PTE_USER;

    if ((pml4->entries[pml4_idx] & VMM_X64_PTE_PRESENT) == 0) {
        uint64_t new_pdpt;
        if (!create)
            return -1;
        new_pdpt = alloc_table_phys();
        if (!new_pdpt)
            return -1;
        pml4->entries[pml4_idx] = new_pdpt | table_flags;
    } else if (user && ((pml4->entries[pml4_idx] & VMM_X64_PTE_USER) == 0)) {
        return -1;
    }

    pdpt = table_virt(pml4->entries[pml4_idx]);
    if ((pdpt->entries[pdpt_idx] & VMM_X64_PTE_PRESENT) == 0) {
        uint64_t new_pd;
        if (!create)
            return -1;
        new_pd = alloc_table_phys();
        if (!new_pd)
            return -1;
        pdpt->entries[pdpt_idx] = new_pd | table_flags;
    } else if (pdpt->entries[pdpt_idx] & VMM_X64_PDE_PS) {
        return -1;
    } else if (user && ((pdpt->entries[pdpt_idx] & VMM_X64_PTE_USER) == 0)) {
        return -1;
    }

    pd = table_virt(pdpt->entries[pdpt_idx]);
    if ((pd->entries[pd_idx] & VMM_X64_PTE_PRESENT) == 0) {
        uint64_t new_pt;
        if (!create)
            return -1;
        new_pt = alloc_table_phys();
        if (!new_pt)
            return -1;
        pd->entries[pd_idx] = new_pt | table_flags;
    } else if (pd->entries[pd_idx] & VMM_X64_PDE_PS) {
        return -1;
    } else if (user && ((pd->entries[pd_idx] & VMM_X64_PTE_USER) == 0)) {
        return -1;
    }

    pt = table_virt(pd->entries[pd_idx]);
    (void)pt_idx;
    *pt_out = pt;
    return 0;
}

static void tlb_flush_range(uint64_t pml4_phys, uintptr_t virt_addr, size_t page_count)
{
    uint64_t current_pml4;

    if (page_count == 0)
        return;

    current_pml4 = read_cr3() & VMM_X64_ADDR_MASK;
    if (current_pml4 != (pml4_phys & VMM_X64_ADDR_MASK))
        return;

    if (page_count == 1) {
        invlpg_addr(virt_addr);
        return;
    }

    if (page_count <= VMM_X64_TLB_INVLPG_THRESHOLD) {
        for (size_t i = 0; i < page_count; i++)
            invlpg_addr(virt_addr + (uintptr_t)(i * PAGE_SIZE));
        return;
    }

    write_cr3(current_pml4);
}

#endif /* __x86_64__ */

static uint64_t g_boot_kernel_pml4 = 0;

static int cpu_has_pat(void)
{
#ifdef __x86_64__
    uint32_t eax = 1u, ebx = 0u, ecx = 0u, edx = 0u;
    __asm__ volatile ("cpuid"
                      : "+a"(eax), "=b"(ebx), "+c"(ecx), "+d"(edx)
                      :
                      : "memory");
    return (edx & (1u << 16)) != 0;
#else
    return 0;
#endif
}

uint64_t vmm_x64_read_pat_cpu(void)
{
#ifdef __x86_64__
    if (!cpu_has_pat())
        return 0;
    return rdmsr(MSR_IA32_PAT);
#else
    return 0;
#endif
}

static int cpu_has_nx(void)
{
#ifdef __x86_64__
    uint32_t eax = 0x80000000u, ebx = 0u, ecx = 0u, edx = 0u;
    __asm__ volatile ("cpuid"
                      : "+a"(eax), "=b"(ebx), "+c"(ecx), "+d"(edx)
                      :
                      : "memory");
    if (eax < 0x80000001u)
        return 0;
    eax = 0x80000001u;
    __asm__ volatile ("cpuid"
                      : "+a"(eax), "=b"(ebx), "+c"(ecx), "+d"(edx)
                      :
                      : "memory");
    return (edx & (1u << 20)) != 0;
#else
    return 0;
#endif
}

int vmm_x64_init_nxe_cpu(void)
{
#ifdef __x86_64__
    if (!cpu_has_nx())
        return 0;
    uint64_t efer = rdmsr(MSR_IA32_EFER);
    if ((efer & (1ULL << 11)) == 0)
        wrmsr(MSR_IA32_EFER, efer | (1ULL << 11));
    efer = rdmsr(MSR_IA32_EFER);
    return (efer & (1ULL << 11)) != 0 ? 1 : 0;
#else
    return 0;
#endif
}

int vmm_x64_nxe_supported(void)
{
    return g_nxe_supported;
}

int vmm_x64_nxe_enabled(void)
{
    return g_nxe_enabled;
}

void vmm_x64_init(uint64_t hhdm_offset)
{
    g_hhdm_offset = hhdm_offset;
#ifdef __x86_64__
    g_boot_kernel_pml4 = read_cr3() & VMM_X64_ADDR_MASK;
    g_nxe_supported = cpu_has_nx();
    g_nxe_enabled = vmm_x64_init_nxe_cpu();
    if (vmm_x64_init_pat_cpu() == 0 && vmm_x64_read_pat_cpu() == VMM_X64_PAT_VALUE)
        g_pat_supported = 1;
    else
        g_pat_supported = 0;
#endif
}

int vmm_x64_init_pat_cpu(void)
{
#ifdef __x86_64__
    if (!cpu_has_pat())
        return -1;

    uint64_t rflags = 0;
    __asm__ volatile ("pushfq\n\tpopq %0\n\tcli" : "=r"(rflags) : : "memory");

    uint64_t cr0_orig = 0;
    __asm__ volatile ("mov %%cr0, %0" : "=r"(cr0_orig) : : "memory");
    uint64_t cr0_nocache = (cr0_orig | (1ULL << 30)) & ~(1ULL << 29);
    __asm__ volatile ("mov %0, %%cr0" : : "r"(cr0_nocache) : "memory");

    __asm__ volatile ("wbinvd" : : : "memory");

    uint64_t cr4 = 0;
    __asm__ volatile ("mov %%cr4, %0" : "=r"(cr4) : : "memory");
    if (cr4 & (1ULL << 7)) {
        __asm__ volatile ("mov %0, %%cr4" : : "r"(cr4 & ~(1ULL << 7)) : "memory");
        __asm__ volatile ("mov %0, %%cr4" : : "r"(cr4) : "memory");
    } else {
        write_cr3(read_cr3());
    }

    wrmsr(MSR_IA32_PAT, VMM_X64_PAT_VALUE);
    __asm__ volatile ("wbinvd" : : : "memory");

    if (cr4 & (1ULL << 7)) {
        __asm__ volatile ("mov %0, %%cr4" : : "r"(cr4 & ~(1ULL << 7)) : "memory");
        __asm__ volatile ("mov %0, %%cr4" : : "r"(cr4) : "memory");
    } else {
        write_cr3(read_cr3());
    }

    __asm__ volatile ("mov %0, %%cr0" : : "r"(cr0_orig) : "memory");
    uint64_t val = rdmsr(MSR_IA32_PAT);

    if (rflags & (1ULL << 9))
        __asm__ volatile ("sti" : : : "memory");

    return (val == VMM_X64_PAT_VALUE) ? 0 : -1;
#else
    return -1;
#endif
}

int vmm_x64_pat_supported(void)
{
    return g_pat_supported;
}

uint64_t vmm_x64_pat_msr(void)
{
    return vmm_x64_read_pat_cpu();
}

uint64_t vmm_x64_hhdm_offset(void)
{
    return g_hhdm_offset;
}

uint64_t vmm_get_kernel_pml4(void)
{
    return g_boot_kernel_pml4;
}

uint64_t vmm_get_current_pml4(void)
{
#ifdef __x86_64__
    return read_cr3() & VMM_X64_ADDR_MASK;
#else
    return 0;
#endif
}

void vmm_switch_pml4(uint64_t pml4_phys)
{
#ifdef __x86_64__
    if (!pml4_phys || !paging_is_page_aligned_u64(pml4_phys))
        return;
    uint64_t cur = read_cr3() & VMM_X64_ADDR_MASK;
    uint64_t next = pml4_phys & VMM_X64_ADDR_MASK;
    if (cur != next) {
        write_cr3(next);
    }
#else
    (void)pml4_phys;
#endif
}

uintptr_t vmm_phys_to_virt(uint64_t phys_addr)
{
#ifdef __x86_64__
    if (g_hhdm_offset)
        return (uintptr_t)(phys_addr + g_hhdm_offset);
#endif
    return (uintptr_t)phys_addr;
}

#ifdef __x86_64__
static uint64_t virt_to_phys_walk_nolock(uintptr_t virt_addr)
{
    /* Full page-table walk. */
    uint64_t pml4_phys = read_cr3() & VMM_X64_ADDR_MASK;
    if (g_hhdm_offset && pml4_phys) {
        page_table_t *tbl = table_virt(pml4_phys);
        uint64_t entry;
        unsigned idx;

        idx = (virt_addr >> 39) & 0x1FF;
        entry = tbl->entries[idx];
        if (!(entry & 1))
            goto fallback;

        tbl = table_virt(entry);
        idx = (virt_addr >> 30) & 0x1FF;
        entry = tbl->entries[idx];
        if (!(entry & 1))
            goto fallback;
        if (entry & (1ULL << 7))
            return (entry & 0x000FFFFFC0000000ULL) | (virt_addr & 0x3FFFFFFFULL);

        tbl = table_virt(entry);
        idx = (virt_addr >> 21) & 0x1FF;
        entry = tbl->entries[idx];
        if (!(entry & 1))
            goto fallback;
        if (entry & (1ULL << 7))
            return (entry & 0x000FFFFFFFE00000ULL) | (virt_addr & 0x1FFFFFULL);

        tbl = table_virt(entry);
        idx = (virt_addr >> 12) & 0x1FF;
        entry = tbl->entries[idx];
        if (!(entry & 1))
            goto fallback;
        return (entry & VMM_X64_ADDR_MASK) | (virt_addr & 0xFFFULL);
    }
fallback:
    if (g_hhdm_offset && virt_addr >= (uintptr_t)g_hhdm_offset)
        return (uint64_t)virt_addr - g_hhdm_offset;
    return (uint64_t)virt_addr;
}
#endif /* __x86_64__ */

uint64_t vmm_virt_to_phys(uintptr_t virt_addr)
{
#ifdef __x86_64__
    unsigned long irq_flags = spin_lock_irqsave(&g_vmm_lock);
    uint64_t phys = virt_to_phys_walk_nolock(virt_addr);
    spin_unlock_irqrestore(&g_vmm_lock, irq_flags);
    return phys;
#else
    return (uint64_t)virt_addr;
#endif
}

int vmm_create_address_space(uint64_t *pml4_out)
{
#ifdef __x86_64__
    uint64_t new_pml4_phys;
    uint64_t kernel_pml4_phys;
    page_table_t *new_pml4;
    page_table_t *kernel_pml4;

    if (!pml4_out)
        return -1;

    new_pml4_phys = alloc_table_phys();
    if (!new_pml4_phys)
        return -1;

    kernel_pml4_phys = g_boot_kernel_pml4 ? g_boot_kernel_pml4 : vmm_get_current_pml4();
    if (!kernel_pml4_phys) {
        pmm_free_pages(new_pml4_phys, 1);
        return -1;
    }

    new_pml4 = table_virt(new_pml4_phys);
    kernel_pml4 = table_virt(kernel_pml4_phys);

    for (uint32_t i = 256; i < 512; i++)
        new_pml4->entries[i] = kernel_pml4->entries[i];

    *pml4_out = new_pml4_phys;
    kprintf("[vmm] create_address_space -> 0x%08x%08x\n",
            (uint32_t)(new_pml4_phys >> 32), (uint32_t)new_pml4_phys);
    return 0;
#else
    (void)pml4_out;
    return -1;
#endif
}

int vmm_clone_address_space(uint64_t src_pml4_phys, uint64_t *pml4_out)
{
#ifdef __x86_64__
    uint64_t new_pml4_phys;
    page_table_t *new_pml4;
    page_table_t *src_pml4;

    if (!pml4_out || !src_pml4_phys)
        return -1;

    new_pml4_phys = alloc_table_phys();
    if (!new_pml4_phys)
        return -1;

    new_pml4 = table_virt(new_pml4_phys);
    src_pml4 = table_virt(src_pml4_phys);
    for (uint32_t i = 256; i < 512; i++)
        new_pml4->entries[i] = src_pml4->entries[i];

    *pml4_out = new_pml4_phys;
    return 0;
#else
    (void)src_pml4_phys;
    (void)pml4_out;
    return -1;
#endif
}

int vmm_destroy_address_space(uint64_t pml4_phys)
{
#ifdef __x86_64__
    page_table_t *pml4;

    if (!pml4_phys || !paging_is_page_aligned_u64(pml4_phys))
        return -1;
    if ((pml4_phys & VMM_X64_ADDR_MASK) == vmm_get_current_pml4())
        return -1;

    pml4 = table_virt(pml4_phys);
    for (uint32_t pml4_idx = 0; pml4_idx < 256; pml4_idx++) {
        uint64_t pdpt_entry = pml4->entries[pml4_idx];
        page_table_t *pdpt;
        uint64_t pdpt_phys;

        if ((pdpt_entry & VMM_X64_PTE_PRESENT) == 0)
            continue;

        pdpt_phys = pdpt_entry & VMM_X64_ADDR_MASK;
        if (!pdpt_phys)
            continue;
        pdpt = table_virt(pdpt_phys);
        for (uint32_t pdpt_idx = 0; pdpt_idx < 512; pdpt_idx++) {
            uint64_t pd_entry = pdpt->entries[pdpt_idx];
            page_table_t *pd;
            uint64_t pd_phys;

            if ((pd_entry & VMM_X64_PTE_PRESENT) == 0)
                continue;
            if (pd_entry & (1ULL << 7))
                continue;

            pd_phys = pd_entry & VMM_X64_ADDR_MASK;
            if (!pd_phys)
                continue;
            pd = table_virt(pd_phys);
            for (uint32_t pd_idx = 0; pd_idx < 512; pd_idx++) {
                uint64_t pt_entry = pd->entries[pd_idx];
                page_table_t *pt;
                uint64_t pt_phys;
                if ((pt_entry & VMM_X64_PTE_PRESENT) == 0)
                    continue;
                if (pt_entry & (1ULL << 7))
                    continue;
                pt_phys = pt_entry & VMM_X64_ADDR_MASK;
                if (!pt_phys)
                    continue;

                /* Reclaim owned user frames before freeing the leaf PT; skip FB MMIO and SHM. */
                pt = table_virt(pt_phys);
                for (uint32_t pt_idx = 0; pt_idx < 512; pt_idx++) {
                    uint64_t leaf_entry = pt->entries[pt_idx];
                    uint64_t leaf_phys;
                    uint64_t va;

                    if ((leaf_entry & (VMM_X64_PTE_PRESENT | VMM_X64_PTE_PROTNONE)) == 0)
                        continue;
                    if ((leaf_entry & VMM_X64_PTE_USER) == 0)
                        continue;

                    va = ((uint64_t)pml4_idx << 39) |
                         ((uint64_t)pdpt_idx << 30) |
                         ((uint64_t)pd_idx << 21) |
                         ((uint64_t)pt_idx << 12);

                    if (va >= VMM_X64_FB_MMIO_BASE && va < VMM_X64_FB_MMIO_LIMIT)
                        continue;
                    if (va >= VM_SPACE_SHM_BASE && va < VM_SPACE_SHM_LIMIT)
                        continue;

                    leaf_phys = leaf_entry & VMM_X64_ADDR_MASK;
                    if (leaf_phys)
                        (void)pmm_page_put(leaf_phys);
                }

                pmm_free_pages(pt_phys, 1);
            }

            pmm_free_pages(pd_phys, 1);
        }

        pmm_free_pages(pdpt_phys, 1);
    }

    pmm_free_pages(pml4_phys, 1);
    return 0;
#else
    (void)pml4_phys;
    return -1;
#endif
}

#ifdef __x86_64__
static int query_page_ex_nolock(uint64_t pml4_phys,
                                uintptr_t virt_addr,
                                uint64_t *phys_out,
                                uint64_t *pte_flags_out,
                                size_t *page_size_out)
{
    page_table_t *pml4;
    page_table_t *pdpt;
    page_table_t *pd;
    page_table_t *pt;
    uint64_t pml4_idx;
    uint64_t pdpt_idx;
    uint64_t pd_idx;
    uint64_t pt_idx;
    uint64_t entry;
    int user;

    if (!pml4_phys || !paging_is_page_aligned_uintptr(virt_addr))
        return -1;
    if (!paging_range_is_user(virt_addr, PAGE_SIZE) &&
        !paging_range_is_kernel(virt_addr, PAGE_SIZE))
        return -1;

    user = paging_range_is_user(virt_addr, PAGE_SIZE);
    pml4_idx = ((uint64_t)virt_addr >> 39) & 0x1FFULL;
    pdpt_idx = ((uint64_t)virt_addr >> 30) & 0x1FFULL;
    pd_idx = ((uint64_t)virt_addr >> 21) & 0x1FFULL;
    pt_idx = ((uint64_t)virt_addr >> 12) & 0x1FFULL;

    pml4 = table_virt(pml4_phys);
    entry = pml4->entries[pml4_idx];
    if ((entry & VMM_X64_PTE_PRESENT) == 0)
        return -1;
    if (user && (entry & VMM_X64_PTE_USER) == 0)
        return -1;

    pdpt = table_virt(entry);
    entry = pdpt->entries[pdpt_idx];
    if ((entry & VMM_X64_PTE_PRESENT) == 0)
        return -1;
    if (user && (entry & VMM_X64_PTE_USER) == 0)
        return -1;
    if (entry & VMM_X64_PDE_PS) {
        if (phys_out)
            *phys_out = (entry & 0x000FFFFFC0000000ULL) | ((uint64_t)virt_addr & 0x3FFFFFFFULL);
        if (pte_flags_out)
            *pte_flags_out = entry & ~0x000FFFFFC0000000ULL;
        if (page_size_out)
            *page_size_out = (size_t)(1ULL << 30);
        return 0;
    }

    pd = table_virt(entry);
    entry = pd->entries[pd_idx];
    if ((entry & VMM_X64_PTE_PRESENT) == 0)
        return -1;
    if (user && (entry & VMM_X64_PTE_USER) == 0)
        return -1;
    if (entry & VMM_X64_PDE_PS) {
        if (phys_out)
            *phys_out = (entry & 0x000FFFFFFFE00000ULL) | ((uint64_t)virt_addr & 0x1FFFFFULL);
        if (pte_flags_out)
            *pte_flags_out = entry & ~0x000FFFFFFFE00000ULL;
        if (page_size_out)
            *page_size_out = (size_t)(2ULL << 20);
        return 0;
    }

    pt = table_virt(entry);
    entry = pt->entries[pt_idx];
    if ((entry & VMM_X64_PTE_PRESENT) == 0)
        return -1;

    if (phys_out)
        *phys_out = entry & VMM_X64_ADDR_MASK;
    if (pte_flags_out)
        *pte_flags_out = entry & ~VMM_X64_ADDR_MASK;
    if (page_size_out)
        *page_size_out = (size_t)PAGE_SIZE;

    return 0;
}
#endif /* __x86_64__ */

int vmm_query_page(uint64_t pml4_phys,
                   uintptr_t virt_addr,
                   uint64_t *phys_out,
                   uint64_t *pte_flags_out)
{
#ifdef __x86_64__
    unsigned long irq_flags = spin_lock_irqsave(&g_vmm_lock);
    int rc = query_page_ex_nolock(pml4_phys, virt_addr, phys_out, pte_flags_out, NULL);
    spin_unlock_irqrestore(&g_vmm_lock, irq_flags);
    return rc;
#else
    (void)pml4_phys;
    (void)virt_addr;
    (void)phys_out;
    (void)pte_flags_out;
    return -1;
#endif
}

int vmm_query_page_ex(uint64_t pml4_phys,
                      uintptr_t virt_addr,
                      uint64_t *phys_out,
                      uint64_t *pte_flags_out,
                      size_t *page_size_out)
{
#ifdef __x86_64__
    unsigned long irq_flags = spin_lock_irqsave(&g_vmm_lock);
    int rc = query_page_ex_nolock(pml4_phys, virt_addr, phys_out, pte_flags_out, page_size_out);
    spin_unlock_irqrestore(&g_vmm_lock, irq_flags);
    return rc;
#else
    (void)pml4_phys;
    (void)virt_addr;
    (void)phys_out;
    (void)pte_flags_out;
    (void)page_size_out;
    return -1;
#endif
}

#ifdef __x86_64__
static int map_pages_nolock(uint64_t pml4_phys,
                            uintptr_t virt_addr,
                            uint64_t phys_addr,
                            size_t page_count,
                            uint64_t map_flags)
{
    uint64_t pte_flags;
    int user;

    if (!pml4_phys)
        return -1;
    if (validate_map_request(virt_addr, phys_addr, page_count, map_flags) != 0)
        return -1;

    pte_flags = pte_flags_from_map(map_flags);
    user = (map_flags & PAGING_MAP_USER) ? 1 : 0;

    for (size_t i = 0; i < page_count; i++) {
        page_table_t *pt = NULL;
        uintptr_t va = virt_addr + (uintptr_t)(i * PAGE_SIZE);
        uint64_t pa = phys_addr + (uint64_t)(i * PAGE_SIZE);
        uint64_t pt_idx;

        if (walk_to_pt(pml4_phys, va, 1, user, &pt) != 0) {
            if (i > 0)
                unmap_pages_nolock(pml4_phys, virt_addr, i);
            return -1;
        }

        pt_idx = ((uint64_t)va >> 12) & 0x1FFULL;
        if (pt->entries[pt_idx] & VMM_X64_PTE_PRESENT) {
            if (i > 0)
                unmap_pages_nolock(pml4_phys, virt_addr, i);
            return -1;
        }

        pt->entries[pt_idx] = (pa & VMM_X64_ADDR_MASK) | pte_flags | VMM_X64_PTE_PRESENT;
    }

    tlb_flush_range(pml4_phys, virt_addr, page_count);
    return 0;
}
#endif /* __x86_64__ */

int vmm_map_pages(uint64_t pml4_phys,
                  uintptr_t virt_addr,
                  uint64_t phys_addr,
                  size_t page_count,
                  uint64_t map_flags)
{
#ifdef __x86_64__
    unsigned long irq_flags = spin_lock_irqsave(&g_vmm_lock);
    int rc = map_pages_nolock(pml4_phys, virt_addr, phys_addr, page_count, map_flags);
    spin_unlock_irqrestore(&g_vmm_lock, irq_flags);
    return rc;
#else
    (void)pml4_phys;
    (void)virt_addr;
    (void)phys_addr;
    (void)page_count;
    (void)map_flags;
    return -1;
#endif
}

#ifdef __x86_64__
static int unmap_pages_nolock(uint64_t pml4_phys,
                              uintptr_t virt_addr,
                              size_t page_count)
{
    int user;

    if (!pml4_phys)
        return -1;
    if (validate_span(virt_addr, 0, page_count, 0) != 0)
        return -1;

    user = paging_range_is_user(virt_addr, page_count * (size_t)PAGE_SIZE) ? 1 : 0;
    for (size_t i = 0; i < page_count; i++) {
        size_t leaf_size = 0;
        if (query_page_ex_nolock(pml4_phys,
                                 virt_addr + (uintptr_t)(i * PAGE_SIZE),
                                 NULL,
                                 NULL,
                                 &leaf_size) != 0 ||
            leaf_size != (size_t)PAGE_SIZE) {
            return -1;
        }
    }

    for (size_t i = 0; i < page_count; i++) {
        page_table_t *pt = NULL;
        uintptr_t va = virt_addr + (uintptr_t)(i * PAGE_SIZE);
        uint64_t pt_idx;

        if (walk_to_pt(pml4_phys, va, 0, user, &pt) != 0)
            return -1;

        pt_idx = ((uint64_t)va >> 12) & 0x1FFULL;
        pt->entries[pt_idx] = 0;
    }

    tlb_flush_range(pml4_phys, virt_addr, page_count);
    return 0;
}
#endif /* __x86_64__ */

int vmm_unmap_pages(uint64_t pml4_phys,
                    uintptr_t virt_addr,
                    size_t page_count)
{
#ifdef __x86_64__
    unsigned long irq_flags = spin_lock_irqsave(&g_vmm_lock);
    int rc = unmap_pages_nolock(pml4_phys, virt_addr, page_count);
    spin_unlock_irqrestore(&g_vmm_lock, irq_flags);
    return rc;
#else
    (void)pml4_phys;
    (void)virt_addr;
    (void)page_count;
    return -1;
#endif
}

int vmm_protect_pages(uint64_t pml4_phys,
                      uintptr_t virt_addr,
                      size_t page_count,
                      uint64_t map_flags)
{
#ifdef __x86_64__
    uint64_t pte_flags;
    int user;

    if (!pml4_phys)
        return -1;
    if ((map_flags & PAGING_MAP_READ) == 0)
        return -1;
    if (validate_span(virt_addr, 0, page_count, 0) != 0)
        return -1;

    user = (map_flags & PAGING_MAP_USER) ? 1 : 0;
    if (user && !paging_range_is_user(virt_addr, page_count * (size_t)PAGE_SIZE))
        return -1;
    if (!user && !paging_range_is_kernel(virt_addr, page_count * (size_t)PAGE_SIZE))
        return -1;

    pte_flags = pte_flags_from_map(map_flags);
    {
        unsigned long irq_flags = spin_lock_irqsave(&g_vmm_lock);
        for (size_t i = 0; i < page_count; i++) {
            size_t leaf_size = 0;
            if (query_page_ex_nolock(pml4_phys,
                                     virt_addr + (uintptr_t)(i * PAGE_SIZE),
                                     NULL,
                                     NULL,
                                     &leaf_size) != 0 ||
                leaf_size != (size_t)PAGE_SIZE) {
                spin_unlock_irqrestore(&g_vmm_lock, irq_flags);
                return -1;
            }
        }

        for (size_t i = 0; i < page_count; i++) {
            page_table_t *pt = NULL;
            uintptr_t va = virt_addr + (uintptr_t)(i * PAGE_SIZE);
            uint64_t pt_idx;
            uint64_t entry;

            if (walk_to_pt(pml4_phys, va, 0, user, &pt) != 0) {
                spin_unlock_irqrestore(&g_vmm_lock, irq_flags);
                return -1;
            }

            pt_idx = ((uint64_t)va >> 12) & 0x1FFULL;
            entry = pt->entries[pt_idx];
            if ((entry & VMM_X64_PTE_PRESENT) == 0) {
                spin_unlock_irqrestore(&g_vmm_lock, irq_flags);
                return -1;
            }

            pt->entries[pt_idx] = (entry & VMM_X64_ADDR_MASK) | pte_flags | VMM_X64_PTE_PRESENT;
        }

        tlb_flush_range(pml4_phys, virt_addr, page_count);
        spin_unlock_irqrestore(&g_vmm_lock, irq_flags);
    }
    return 0;
#else
    (void)pml4_phys;
    (void)virt_addr;
    (void)page_count;
    (void)map_flags;
    return -1;
#endif
}

int vmm_protect_present_pages(uint64_t pml4_phys,
                              uintptr_t virt_addr,
                              size_t page_count,
                              uint64_t map_flags)
{
#ifdef __x86_64__
    int user;
    int prot_none;
    uint64_t pte_flags;

    if (!pml4_phys)
        return -1;
    if (validate_span(virt_addr, 0, page_count, 0) != 0)
        return -1;

    user = (map_flags & PAGING_MAP_USER) ? 1 : 0;
    if (user && !paging_range_is_user(virt_addr, page_count * (size_t)PAGE_SIZE))
        return -1;
    if (!user && !paging_range_is_kernel(virt_addr, page_count * (size_t)PAGE_SIZE))
        return -1;

    prot_none = ((map_flags & PAGING_MAP_READ) == 0) ? 1 : 0;
    pte_flags = prot_none ? (VMM_X64_PTE_USER | VMM_X64_PTE_PROTNONE)
                          : (pte_flags_from_map(map_flags) | VMM_X64_PTE_PRESENT);

    unsigned long irq_flags = spin_lock_irqsave(&g_vmm_lock);
    for (size_t i = 0; i < page_count; i++) {
        page_table_t *pt = NULL;
        uintptr_t va = virt_addr + (uintptr_t)(i * PAGE_SIZE);
        uint64_t pt_idx;
        uint64_t entry;

        if (walk_to_pt(pml4_phys, va, 0, user, &pt) != 0)
            continue;

        pt_idx = ((uint64_t)va >> 12) & 0x1FFULL;
        entry = pt->entries[pt_idx];
        if ((entry & (VMM_X64_PTE_PRESENT | VMM_X64_PTE_PROTNONE)) == 0)
            continue;

        uint64_t updated_flags = pte_flags | (entry & VMM_X64_PTE_COW);
        if (updated_flags & VMM_X64_PTE_COW)
            updated_flags &= ~VMM_X64_PTE_WRITABLE;
        pt->entries[pt_idx] = (entry & VMM_X64_ADDR_MASK) | updated_flags;
    }
    tlb_flush_range(pml4_phys, virt_addr, page_count);
    spin_unlock_irqrestore(&g_vmm_lock, irq_flags);
    return 0;
#else
    (void)pml4_phys;
    (void)virt_addr;
    (void)page_count;
    (void)map_flags;
    return -1;
#endif
}

int vmm_unmap_page_collect(uint64_t pml4_phys,
                           uintptr_t virt_addr,
                           uint64_t *phys_out,
                           uint64_t *flags_out)
{
#ifdef __x86_64__
    int user;
    page_table_t *pt = NULL;
    uint64_t pt_idx;
    uint64_t entry;

    if (!pml4_phys || validate_span(virt_addr, 0, 1, 0) != 0)
        return -1;

    user = paging_range_is_user(virt_addr, PAGE_SIZE) ? 1 : 0;
    unsigned long irq_flags = spin_lock_irqsave(&g_vmm_lock);
    if (walk_to_pt(pml4_phys, virt_addr, 0, user, &pt) != 0) {
        spin_unlock_irqrestore(&g_vmm_lock, irq_flags);
        return -1;
    }
    pt_idx = ((uint64_t)virt_addr >> 12) & 0x1FFULL;
    entry = pt->entries[pt_idx];
    if ((entry & (VMM_X64_PTE_PRESENT | VMM_X64_PTE_PROTNONE)) == 0) {
        spin_unlock_irqrestore(&g_vmm_lock, irq_flags);
        return -1;
    }
    pt->entries[pt_idx] = 0;
    spin_unlock_irqrestore(&g_vmm_lock, irq_flags);

    if (phys_out)
        *phys_out = entry & VMM_X64_ADDR_MASK;
    if (flags_out)
        *flags_out = entry & ~VMM_X64_ADDR_MASK;
    return 0;
#else
    (void)pml4_phys;
    (void)virt_addr;
    (void)phys_out;
    (void)flags_out;
    return -1;
#endif
}

int vmm_map_user_page_if_absent(uint64_t pml4_phys,
                                uintptr_t virt_addr,
                                uint64_t phys_addr,
                                uint64_t map_flags)
{
#ifdef __x86_64__
    if (!pml4_phys || (map_flags & PAGING_MAP_USER) == 0)
        return -1;
    if (validate_map_request(virt_addr, phys_addr, 1, map_flags) != 0)
        return -1;

    uint64_t pte_flags = pte_flags_from_map(map_flags);
    uint64_t pt_idx = ((uint64_t)virt_addr >> 12) & 0x1FFULL;
    page_table_t *pt = NULL;

    unsigned long irq_flags = spin_lock_irqsave(&g_vmm_lock);
    if (walk_to_pt(pml4_phys, virt_addr, 1, 1, &pt) != 0) {
        spin_unlock_irqrestore(&g_vmm_lock, irq_flags);
        return -1;
    }
    if (pt->entries[pt_idx] & (VMM_X64_PTE_PRESENT | VMM_X64_PTE_PROTNONE)) {
        spin_unlock_irqrestore(&g_vmm_lock, irq_flags);
        return 1;
    }
    pt->entries[pt_idx] = (phys_addr & VMM_X64_ADDR_MASK) | pte_flags | VMM_X64_PTE_PRESENT;
    tlb_flush_range(pml4_phys, virt_addr, 1);
    spin_unlock_irqrestore(&g_vmm_lock, irq_flags);
    return 0;
#else
    (void)pml4_phys;
    (void)virt_addr;
    (void)phys_addr;
    (void)map_flags;
    return -1;
#endif
}

int vmm_cow_replace_user_page(uint64_t pml4_phys,
                              uintptr_t virt_addr,
                              uint64_t expected_old_phys,
                              uint64_t new_phys,
                              uint64_t map_flags)
{
#ifdef __x86_64__
    if (!pml4_phys || (map_flags & PAGING_MAP_USER) == 0)
        return -1;
    if (validate_map_request(virt_addr, new_phys, 1, map_flags) != 0)
        return -1;

    uint64_t pte_flags = pte_flags_from_map(map_flags);
    uint64_t pt_idx = ((uint64_t)virt_addr >> 12) & 0x1FFULL;
    page_table_t *pt = NULL;

    unsigned long irq_flags = spin_lock_irqsave(&g_vmm_lock);
    if (walk_to_pt(pml4_phys, virt_addr, 0, 1, &pt) != 0) {
        spin_unlock_irqrestore(&g_vmm_lock, irq_flags);
        return -1;
    }
    uint64_t entry = pt->entries[pt_idx];
    if ((entry & VMM_X64_PTE_PRESENT) == 0) {
        spin_unlock_irqrestore(&g_vmm_lock, irq_flags);
        return -1;
    }
    if (entry & VMM_X64_PTE_WRITABLE) {
        spin_unlock_irqrestore(&g_vmm_lock, irq_flags);
        return 1;
    }
    if ((entry & VMM_X64_ADDR_MASK) != (expected_old_phys & VMM_X64_ADDR_MASK)) {
        spin_unlock_irqrestore(&g_vmm_lock, irq_flags);
        return -1;
    }
    pt->entries[pt_idx] = (new_phys & VMM_X64_ADDR_MASK) | pte_flags | VMM_X64_PTE_PRESENT;
    tlb_flush_range(pml4_phys, virt_addr, 1);
    spin_unlock_irqrestore(&g_vmm_lock, irq_flags);
    return 0;
#else
    (void)pml4_phys;
    (void)virt_addr;
    (void)expected_old_phys;
    (void)new_phys;
    (void)map_flags;
    return -1;
#endif
}

int vmm_upgrade_user_page_write(uint64_t pml4_phys,
                                uintptr_t virt_addr,
                                uint64_t expected_phys,
                                uint64_t map_flags)
{
#ifdef __x86_64__
    if (!pml4_phys || (map_flags & PAGING_MAP_USER) == 0)
        return -1;
    if (validate_map_request(virt_addr, expected_phys, 1, map_flags) != 0)
        return -1;

    uint64_t pte_flags = pte_flags_from_map(map_flags);
    uint64_t pt_idx = ((uint64_t)virt_addr >> 12) & 0x1FFULL;
    page_table_t *pt = NULL;

    unsigned long irq_flags = spin_lock_irqsave(&g_vmm_lock);
    if (walk_to_pt(pml4_phys, virt_addr, 0, 1, &pt) != 0) {
        spin_unlock_irqrestore(&g_vmm_lock, irq_flags);
        return -1;
    }
    uint64_t entry = pt->entries[pt_idx];
    if ((entry & VMM_X64_PTE_PRESENT) == 0) {
        spin_unlock_irqrestore(&g_vmm_lock, irq_flags);
        return -1;
    }
    if ((entry & VMM_X64_ADDR_MASK) != (expected_phys & VMM_X64_ADDR_MASK)) {
        spin_unlock_irqrestore(&g_vmm_lock, irq_flags);
        return -1;
    }
    if (entry & VMM_X64_PTE_WRITABLE) {
        spin_unlock_irqrestore(&g_vmm_lock, irq_flags);
        return 1;
    }
    pt->entries[pt_idx] = (entry & VMM_X64_ADDR_MASK) | pte_flags | VMM_X64_PTE_PRESENT;
    tlb_flush_range(pml4_phys, virt_addr, 1);
    spin_unlock_irqrestore(&g_vmm_lock, irq_flags);
    return 0;
#else
    (void)pml4_phys;
    (void)virt_addr;
    (void)expected_phys;
    (void)map_flags;
    return -1;
#endif
}

int vmm_fork_copy_pte(uint64_t src_pml4_phys,
                      uint64_t dst_pml4_phys,
                      uintptr_t virt_addr,
                      int is_private,
                      int *copied_out,
                      uint64_t *phys_out,
                      int *need_shootdown_out)
{
#ifdef __x86_64__
    if (copied_out)
        *copied_out = 0;
    if (phys_out)
        *phys_out = 0;
    if (!src_pml4_phys || !dst_pml4_phys)
        return -1;
    if (!paging_range_is_user(virt_addr, PAGE_SIZE))
        return -1;

    uint64_t pt_idx = ((uint64_t)virt_addr >> 12) & 0x1FFULL;
    page_table_t *src_pt = NULL;
    page_table_t *dst_pt = NULL;

    unsigned long irq_flags = spin_lock_irqsave(&g_vmm_lock);
    if (walk_to_pt(src_pml4_phys, virt_addr, 0, 1, &src_pt) != 0) {
        spin_unlock_irqrestore(&g_vmm_lock, irq_flags);
        return 0;
    }

    uint64_t entry = src_pt->entries[pt_idx];
    if ((entry & (VMM_X64_PTE_PRESENT | VMM_X64_PTE_PROTNONE)) == 0 ||
        (entry & VMM_X64_PTE_USER) == 0) {
        spin_unlock_irqrestore(&g_vmm_lock, irq_flags);
        return 0;
    }

    if (walk_to_pt(dst_pml4_phys, virt_addr, 1, 1, &dst_pt) != 0) {
        spin_unlock_irqrestore(&g_vmm_lock, irq_flags);
        return -1;
    }

    uint64_t child_entry = entry;
    if (is_private && (entry & VMM_X64_PTE_PRESENT) && (entry & VMM_X64_PTE_WRITABLE)) {
        uint64_t cow_entry = (entry & ~VMM_X64_PTE_WRITABLE) | VMM_X64_PTE_COW;
        src_pt->entries[pt_idx] = cow_entry;
        child_entry = cow_entry;
        tlb_flush_range(src_pml4_phys, virt_addr, 1);
        if (need_shootdown_out)
            *need_shootdown_out = 1;
    }

    dst_pt->entries[pt_idx] = child_entry;
    spin_unlock_irqrestore(&g_vmm_lock, irq_flags);

    if (copied_out)
        *copied_out = 1;
    if (phys_out)
        *phys_out = child_entry & VMM_X64_ADDR_MASK;
    return 0;
#else
    (void)src_pml4_phys;
    (void)dst_pml4_phys;
    (void)virt_addr;
    (void)is_private;
    (void)copied_out;
    (void)phys_out;
    (void)need_shootdown_out;
    return -1;
#endif
}

#ifdef __x86_64__
static void tlb_flush_local_any(uint64_t pml4_phys, uintptr_t va, size_t page_count)
{
    uint64_t cur = read_cr3() & VMM_X64_ADDR_MASK;
    if (pml4_phys != 0 && cur != (pml4_phys & VMM_X64_ADDR_MASK))
        return;

    if (page_count == 0 || page_count > VMM_X64_TLB_INVLPG_THRESHOLD) {
        write_cr3(cur);
        return;
    }
    for (size_t i = 0; i < page_count; i++)
        invlpg_addr(va + (uintptr_t)(i * PAGE_SIZE));
}

static void service_pending_shootdown_local(uint32_t cpu)
{
    if (cpu >= 64u)
        return;
    uint64_t bit = 1ULL << cpu;
    if ((__atomic_load_n(&g_tlb_req.ack_mask, __ATOMIC_ACQUIRE) & bit) != 0ULL) {
        uint64_t pml4 = __atomic_load_n(&g_tlb_req.pml4_phys, __ATOMIC_RELAXED);
        uintptr_t va = __atomic_load_n(&g_tlb_req.va, __ATOMIC_RELAXED);
        size_t cnt = __atomic_load_n(&g_tlb_req.page_count, __ATOMIC_RELAXED);
        tlb_flush_local_any(pml4, va, cnt);
        __atomic_fetch_and(&g_tlb_req.ack_mask, ~bit, __ATOMIC_SEQ_CST);
    }
}
#endif

void tlb_shootdown_ipi_handler(void)
{
#ifdef __x86_64__
    uint32_t cpu = smp_this_cpu_id();
    uint64_t pml4 = __atomic_load_n(&g_tlb_req.pml4_phys, __ATOMIC_ACQUIRE);
    uintptr_t va = __atomic_load_n(&g_tlb_req.va, __ATOMIC_RELAXED);
    size_t cnt = __atomic_load_n(&g_tlb_req.page_count, __ATOMIC_RELAXED);

    tlb_flush_local_any(pml4, va, cnt);
    __atomic_fetch_add(&g_tlb_shootdown_ipi_count, 1ULL, __ATOMIC_RELAXED);
    lapic_eoi();
    if (cpu < 64u)
        __atomic_fetch_and(&g_tlb_req.ack_mask, ~(1ULL << cpu), __ATOMIC_SEQ_CST);
#endif
}

uint64_t vmm_tlb_shootdown_ipi_count(void)
{
    return __atomic_load_n(&g_tlb_shootdown_ipi_count, __ATOMIC_RELAXED);
}

void vmm_tlb_shootdown(vm_space_t *space, uintptr_t va, size_t page_count)
{
#ifdef __x86_64__
    uint64_t pml4_phys = space ? (space->pml4_phys & VMM_X64_ADDR_MASK) : 0ULL;
    tlb_flush_local_any(pml4_phys, va, page_count);

    if (!space || smp_cpu_count() <= 1u)
        return;

    uint32_t this_cpu = smp_this_cpu_id();
    uint64_t active = __atomic_load_n(&space->active_cpus, __ATOMIC_SEQ_CST);
    uint64_t remote = (this_cpu < 64u) ? (active & ~(1ULL << this_cpu)) : active;
    if (remote == 0ULL)
        return;

    uint32_t ncpus = smp_cpu_count();
    uint64_t target_mask = 0;
    for (uint32_t c = 0; c < ncpus && c < 64u; c++) {
        if (remote & (1ULL << c)) {
            cpu_state_t *cs = smp_get_cpu(c);
            if (cs && cs->online)
                target_mask |= (1ULL << c);
        }
    }
    if (target_mask == 0ULL)
        return;

    uint64_t rflags = 0;
    __asm__ volatile ("pushfq\n\tpopq %0" : "=r"(rflags) : : "memory");

    while (!spin_trylock(&g_tlb_lock)) {
        if ((rflags & (1ULL << 9)) == 0)
            service_pending_shootdown_local(this_cpu);
        __asm__ volatile ("pause");
    }

    __asm__ volatile ("cli" : : : "memory");

    g_tlb_req.pml4_phys = pml4_phys;
    g_tlb_req.va = va;
    g_tlb_req.page_count = page_count;
    __atomic_store_n(&g_tlb_req.ack_mask, target_mask, __ATOMIC_SEQ_CST);

    for (uint32_t c = 0; c < ncpus && c < 64u; c++) {
        if (target_mask & (1ULL << c)) {
            uint32_t lapic_id = smp_get_lapic_id(c);
            if (lapic_id != 0xFFFFFFFFu)
                lapic_send_ipi(lapic_id, (uint8_t)IDT_VEC_TLB_SHOOTDOWN);
            else
                __atomic_fetch_and(&g_tlb_req.ack_mask, ~(1ULL << c), __ATOMIC_SEQ_CST);
        }
    }

    while (__atomic_load_n(&g_tlb_req.ack_mask, __ATOMIC_ACQUIRE) != 0ULL)
        __asm__ volatile ("pause");

    spin_unlock(&g_tlb_lock);
    if (rflags & (1ULL << 9))
        __asm__ volatile ("sti" : : : "memory");
#else
    (void)space;
    (void)va;
    (void)page_count;
#endif
}

int vmm_map_page(uintptr_t virt_addr, uint64_t phys_addr, uint64_t flags)
{
#ifdef __x86_64__
    uint64_t map_flags;
    map_flags = map_flags_from_legacy(flags);
    return vmm_map_pages(vmm_get_current_pml4(), virt_addr, phys_addr, 1, map_flags);
#else
    (void)virt_addr;
    (void)phys_addr;
    (void)flags;
    return -1;
#endif
}

int vmm_unmap_page(uintptr_t virt_addr)
{
#ifdef __x86_64__
    return vmm_unmap_pages(vmm_get_current_pml4(), virt_addr, 1);
#else
    (void)virt_addr;
    return -1;
#endif
}

#ifdef __x86_64__
static int split_1g_page_nolock(page_table_t *pdpt, uint64_t pdpt_idx)
{
    uint64_t entry = pdpt->entries[pdpt_idx];
    uint64_t base_phys = entry & 0x000FFFFFC0000000ULL;
    uint64_t leaf_flags = entry & (VMM_X64_PTE_PRESENT | VMM_X64_PTE_WRITABLE |
                                   VMM_X64_PTE_USER | VMM_X64_PTE_WRITE_THROUGH |
                                   VMM_X64_PTE_CACHE_DISABLE | VMM_X64_PDE_PS |
                                   VMM_X64_PTE_GLOBAL | VMM_X64_PDE_PAT |
                                   VMM_X64_PTE_NO_EXEC);
    uint64_t new_pd_phys = alloc_table_phys();
    if (!new_pd_phys)
        return -1;

    page_table_t *new_pd = table_virt(new_pd_phys);
    for (uint64_t i = 0; i < 512; i++) {
        new_pd->entries[i] = (base_phys + (i << 21)) | leaf_flags | VMM_X64_PDE_PS;
    }

    uint64_t dir_flags = VMM_X64_PTE_PRESENT | VMM_X64_PTE_WRITABLE | (entry & VMM_X64_PTE_USER);
    pdpt->entries[pdpt_idx] = new_pd_phys | dir_flags;
    return 0;
}

static int split_2m_page_nolock(page_table_t *pd, uint64_t pd_idx)
{
    uint64_t entry = pd->entries[pd_idx];
    uint64_t base_phys = entry & 0x000FFFFFFFE00000ULL;
    uint64_t leaf_flags = entry & (VMM_X64_PTE_PRESENT | VMM_X64_PTE_WRITABLE |
                                   VMM_X64_PTE_USER | VMM_X64_PTE_WRITE_THROUGH |
                                   VMM_X64_PTE_CACHE_DISABLE | VMM_X64_PTE_GLOBAL |
                                   VMM_X64_PTE_NO_EXEC);
    /* Move PAT from huge-page bit 12 to 4 KiB PTE bit 7 so bit 12 does not pollute PFN. */
    if (entry & VMM_X64_PDE_PAT)
        leaf_flags |= VMM_X64_PTE_PAT;

    uint64_t new_pt_phys = alloc_table_phys();
    if (!new_pt_phys)
        return -1;

    page_table_t *new_pt = table_virt(new_pt_phys);
    for (uint64_t i = 0; i < 512; i++) {
        new_pt->entries[i] = (base_phys + (i << 12)) | leaf_flags;
    }

    uint64_t dir_flags = VMM_X64_PTE_PRESENT | VMM_X64_PTE_WRITABLE | (entry & VMM_X64_PTE_USER);
    pd->entries[pd_idx] = new_pt_phys | dir_flags;
    return 0;
}

static int map_kernel_phys_region(uint64_t phys_base,
                                  size_t size,
                                  uintptr_t *virt_base,
                                  uint64_t cache_map_flag)
{
    uint64_t phys_page;
    uint64_t phys_end;
    size_t page_count;
    uintptr_t virt_page;
    uint64_t pml4_phys;
    uint64_t page_offset;
    int use_wc;

    if (!virt_base || size == 0)
        return -1;
    if (phys_base + (uint64_t)size - 1ULL < phys_base)
        return -1;

    phys_page = phys_base & ~(uint64_t)(PAGE_SIZE - 1);
    page_offset = phys_base - phys_page;
    phys_end = phys_base + (uint64_t)size - 1ULL;
    page_count = (size_t)(((phys_end - phys_page) / (uint64_t)PAGE_SIZE) + 1ULL);
    virt_page = vmm_phys_to_virt(phys_page);
    pml4_phys = vmm_get_current_pml4();

    if (!pml4_phys || page_count == 0)
        return -1;
    if (!paging_range_is_kernel(virt_page, page_count * (size_t)PAGE_SIZE))
        return -1;

    use_wc = ((cache_map_flag & PAGING_MAP_WC) != 0 &&
              (cache_map_flag & PAGING_MAP_DEVICE) == 0 &&
              g_pat_supported != 0);

    {
        unsigned long irq_flags = spin_lock_irqsave(&g_vmm_lock);
        page_table_t *pml4 = table_virt(pml4_phys);
        uint64_t table_flags = VMM_X64_PTE_PRESENT | VMM_X64_PTE_WRITABLE;
        int did_split = 0;

        size_t i = 0;
        while (i < page_count) {
            uintptr_t va = virt_page + (uintptr_t)(i * (size_t)PAGE_SIZE);
            uint64_t pa = phys_page + ((uint64_t)i * (uint64_t)PAGE_SIZE);
            uint64_t pml4_idx = ((uint64_t)va >> 39) & 0x1FFULL;
            uint64_t pdpt_idx = ((uint64_t)va >> 30) & 0x1FFULL;
            uint64_t pd_idx = ((uint64_t)va >> 21) & 0x1FFULL;
            uint64_t pt_idx = ((uint64_t)va >> 12) & 0x1FFULL;
            page_table_t *pdpt;
            page_table_t *pd;
            page_table_t *pt;

            if ((pml4->entries[pml4_idx] & VMM_X64_PTE_PRESENT) == 0) {
                uint64_t new_pdpt = alloc_table_phys();
                if (!new_pdpt) {
                    spin_unlock_irqrestore(&g_vmm_lock, irq_flags);
                    return -1;
                }
                pml4->entries[pml4_idx] = new_pdpt | table_flags;
            }

            pdpt = table_virt(pml4->entries[pml4_idx]);
            if ((pdpt->entries[pdpt_idx] & VMM_X64_PTE_PRESENT) == 0) {
                uint64_t new_pd = alloc_table_phys();
                if (!new_pd) {
                    spin_unlock_irqrestore(&g_vmm_lock, irq_flags);
                    return -1;
                }
                pdpt->entries[pdpt_idx] = new_pd | table_flags;
            } else if (pdpt->entries[pdpt_idx] & VMM_X64_PDE_PS) {
                if ((pdpt->entries[pdpt_idx] & 0x000FFFFFC0000000ULL) != (pa & ~0x3FFFFFFFULL)) {
                    spin_unlock_irqrestore(&g_vmm_lock, irq_flags);
                    return -1;
                }
                if (split_1g_page_nolock(pdpt, pdpt_idx) != 0) {
                    spin_unlock_irqrestore(&g_vmm_lock, irq_flags);
                    return -1;
                }
                invlpg_addr(va);
                did_split = 1;
            }

            pd = table_virt(pdpt->entries[pdpt_idx]);
            if ((pd->entries[pd_idx] & VMM_X64_PTE_PRESENT) == 0) {
                uint64_t new_pt = alloc_table_phys();
                if (!new_pt) {
                    spin_unlock_irqrestore(&g_vmm_lock, irq_flags);
                    return -1;
                }
                pd->entries[pd_idx] = new_pt | table_flags;
            } else if (pd->entries[pd_idx] & VMM_X64_PDE_PS) {
                uint64_t huge_pa = pd->entries[pd_idx] & 0x000FFFFFFFE00000ULL;
                if (huge_pa != (pa & ~0x1FFFFFULL)) {
                    spin_unlock_irqrestore(&g_vmm_lock, irq_flags);
                    return -1;
                }
                if ((va & 0x1FFFFFULL) == 0 && (pa & 0x1FFFFFULL) == 0 &&
                    (page_count - i) >= 512) {
                    uint64_t entry = pd->entries[pd_idx] & ~VMM_X64_PDE_CACHE_MASK;
                    if (use_wc)
                        entry |= VMM_X64_PDE_PAT;
                    else
                        entry |= VMM_X64_PTE_CACHE_DISABLE | VMM_X64_PTE_WRITE_THROUGH;
                    entry |= VMM_X64_PTE_WRITABLE | VMM_X64_PDE_PS;
                    if (g_nxe_enabled)
                        entry |= VMM_X64_PTE_NO_EXEC;
                    pd->entries[pd_idx] = entry;
                    invlpg_addr(va);
                    i += 512;
                    continue;
                }
                if (split_2m_page_nolock(pd, pd_idx) != 0) {
                    spin_unlock_irqrestore(&g_vmm_lock, irq_flags);
                    return -1;
                }
                invlpg_addr(va);
                did_split = 1;
            }

            pt = table_virt(pd->entries[pd_idx]);
            if (pt->entries[pt_idx] & VMM_X64_PTE_PRESENT) {
                if ((pt->entries[pt_idx] & VMM_X64_ADDR_MASK) != (pa & VMM_X64_ADDR_MASK)) {
                    spin_unlock_irqrestore(&g_vmm_lock, irq_flags);
                    return -1;
                }
                uint64_t entry = pt->entries[pt_idx] & ~VMM_X64_PTE_CACHE_MASK;
                if (use_wc)
                    entry |= VMM_X64_PTE_PAT;
                else
                    entry |= VMM_X64_PTE_CACHE_DISABLE | VMM_X64_PTE_WRITE_THROUGH;
                entry |= VMM_X64_PTE_WRITABLE;
                if (g_nxe_enabled)
                    entry |= VMM_X64_PTE_NO_EXEC;
                pt->entries[pt_idx] = entry;
            } else {
                uint64_t pte_flags = pte_flags_from_map(PAGING_MAP_READ | PAGING_MAP_WRITE |
                                                        PAGING_MAP_GLOBAL | cache_map_flag);
                pt->entries[pt_idx] = (pa & VMM_X64_ADDR_MASK) | pte_flags | VMM_X64_PTE_PRESENT;
            }
            invlpg_addr(va);
            i++;
        }

        if (did_split)
            write_cr3(pml4_phys);

        spin_unlock_irqrestore(&g_vmm_lock, irq_flags);
    }

    *virt_base = virt_page + (uintptr_t)page_offset;
    return 0;
}
#endif /* __x86_64__ */

int vmm_map_io_region(uint64_t phys_base, size_t size, uintptr_t *virt_base)
{
    if (!virt_base || size == 0)
        return -1;

#ifdef __x86_64__
    return map_kernel_phys_region(phys_base, size, virt_base, PAGING_MAP_DEVICE);
#else
    *virt_base = (uintptr_t)phys_base;
    return 0;
#endif
}

int vmm_map_wc_region(uint64_t phys_base, size_t size, uintptr_t *virt_base)
{
    if (!virt_base || size == 0)
        return -1;

#ifdef __x86_64__
    return map_kernel_phys_region(phys_base, size, virt_base, PAGING_MAP_WC);
#else
    *virt_base = (uintptr_t)phys_base;
    return 0;
#endif
}

int vmm_validate_user_ptr(const void *ptr, size_t len, int need_write)
{
    uintptr_t p = (uintptr_t)ptr;
    if (!p)
        return 0;
    if (len == 0)
        return (p >= PAGING_USER_VA_MIN && p <= PAGING_USER_VA_MAX) ? 1 : 0;
    if (p < PAGING_USER_VA_MIN || p + len > PAGING_USER_VA_MAX || (p + len < p))
        return 0;
#ifdef __x86_64__
    uint64_t pml4 = vmm_get_current_pml4();
    if (!pml4)
        return 0;
    uintptr_t first = p & ~(VMM_X64_PAGE_SIZE - 1);
    uintptr_t last = (p + len - 1) & ~(VMM_X64_PAGE_SIZE - 1);
    for (uintptr_t va = first; va <= last; va += VMM_X64_PAGE_SIZE) {
        uint64_t phys = 0, flags = 0;
        int ok = (vmm_query_page(pml4, va, &phys, &flags) == 0);
        if (!ok || !(flags & VMM_X64_PTE_PRESENT) || !(flags & VMM_X64_PTE_USER) ||
            (need_write && !(flags & VMM_X64_PTE_WRITABLE))) {
            uint64_t rflags = 0;
            __asm__ volatile ("pushfq\n\tpopq %0" : "=r"(rflags) : : "memory");
            process_t *cur = process_current();
            if ((rflags & (1ULL << 9)) && cur && cur->vm_space.owns_pml4 &&
                cur->vm_space.pml4_phys == pml4) {
                uint64_t err = (ok && (flags & VMM_X64_PTE_PRESENT)) ? 1ULL : 0ULL;
                err |= 0x4ULL;
                if (need_write)
                    err |= 0x2ULL;
                if (vm_space_handle_page_fault(&cur->vm_space, va, err) == 0 &&
                    vmm_query_page(pml4, va, &phys, &flags) == 0) {
                    ok = 1;
                } else {
                    return 0;
                }
            } else {
                return 0;
            }
        }
        if (!ok || !(flags & VMM_X64_PTE_PRESENT) || !(flags & VMM_X64_PTE_USER))
            return 0;
        if (need_write && !(flags & VMM_X64_PTE_WRITABLE))
            return 0;
    }
#endif
    return 1;
}

