#include "vmm_x64.h"

#include <stdint.h>

#include "pmm.h"

static uint64_t g_hhdm_offset;

#ifdef __x86_64__

typedef struct {
    uint64_t entries[512];
} page_table_t;

#define VMM_X64_ADDR_MASK 0x000FFFFFFFFFF000ULL
#define VMM_X64_TLB_INVLPG_THRESHOLD 32

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
        pte |= (1ULL << 8);
    if (map_flags & PAGING_MAP_DEVICE)
        pte |= VMM_X64_PTE_CACHE_DISABLE | VMM_X64_PTE_WRITE_THROUGH;
    if ((map_flags & PAGING_MAP_EXEC) == 0)
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
    if (legacy_flags & (1ULL << 8))
        map |= PAGING_MAP_GLOBAL;
    if (legacy_flags & (VMM_X64_PTE_CACHE_DISABLE | VMM_X64_PTE_WRITE_THROUGH))
        map |= PAGING_MAP_DEVICE;

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

void vmm_x64_init(uint64_t hhdm_offset)
{
    g_hhdm_offset = hhdm_offset;
}

uint64_t vmm_x64_hhdm_offset(void)
{
    return g_hhdm_offset;
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
    write_cr3(pml4_phys & VMM_X64_ADDR_MASK);
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

uint64_t vmm_virt_to_phys(uintptr_t virt_addr)
{
#ifdef __x86_64__
    if (g_hhdm_offset && virt_addr >= (uintptr_t)g_hhdm_offset)
        return (uint64_t)virt_addr - g_hhdm_offset;
#endif
    return (uint64_t)virt_addr;
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

    kernel_pml4_phys = vmm_get_current_pml4();
    if (!kernel_pml4_phys) {
        pmm_free_pages(new_pml4_phys, 1);
        return -1;
    }

    new_pml4 = table_virt(new_pml4_phys);
    kernel_pml4 = table_virt(kernel_pml4_phys);

    for (uint32_t i = 256; i < 512; i++)
        new_pml4->entries[i] = kernel_pml4->entries[i];

    *pml4_out = new_pml4_phys;
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
        pdpt = table_virt(pdpt_phys);
        for (uint32_t pdpt_idx = 0; pdpt_idx < 512; pdpt_idx++) {
            uint64_t pd_entry = pdpt->entries[pdpt_idx];
            page_table_t *pd;
            uint64_t pd_phys;

            if ((pd_entry & VMM_X64_PTE_PRESENT) == 0)
                continue;

            pd_phys = pd_entry & VMM_X64_ADDR_MASK;
            pd = table_virt(pd_phys);
            for (uint32_t pd_idx = 0; pd_idx < 512; pd_idx++) {
                uint64_t pt_entry = pd->entries[pd_idx];
                uint64_t pt_phys;
                if ((pt_entry & VMM_X64_PTE_PRESENT) == 0)
                    continue;
                if (pt_entry & (1ULL << 7))
                    continue;
                pt_phys = pt_entry & VMM_X64_ADDR_MASK;
                if (pt_phys)
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

int vmm_query_page(uint64_t pml4_phys,
                   uintptr_t virt_addr,
                   uint64_t *phys_out,
                   uint64_t *pte_flags_out)
{
#ifdef __x86_64__
    page_table_t *pt;
    uint64_t pt_idx;
    uint64_t entry;
    int user;

    if (!pml4_phys || !paging_is_page_aligned_uintptr(virt_addr))
        return -1;
    if (!paging_range_is_user(virt_addr, PAGE_SIZE) &&
        !paging_range_is_kernel(virt_addr, PAGE_SIZE))
        return -1;

    user = paging_range_is_user(virt_addr, PAGE_SIZE);
    if (walk_to_pt(pml4_phys, virt_addr, 0, user, &pt) != 0)
        return -1;

    pt_idx = ((uint64_t)virt_addr >> 12) & 0x1FFULL;
    entry = pt->entries[pt_idx];
    if ((entry & VMM_X64_PTE_PRESENT) == 0)
        return -1;

    if (phys_out)
        *phys_out = entry & VMM_X64_ADDR_MASK;
    if (pte_flags_out)
        *pte_flags_out = entry & ~VMM_X64_ADDR_MASK;

    return 0;
#else
    (void)pml4_phys;
    (void)virt_addr;
    (void)phys_out;
    (void)pte_flags_out;
    return -1;
#endif
}

int vmm_map_pages(uint64_t pml4_phys,
                  uintptr_t virt_addr,
                  uint64_t phys_addr,
                  size_t page_count,
                  uint64_t map_flags)
{
#ifdef __x86_64__
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
                vmm_unmap_pages(pml4_phys, virt_addr, i);
            return -1;
        }

        pt_idx = ((uint64_t)va >> 12) & 0x1FFULL;
        if (pt->entries[pt_idx] & VMM_X64_PTE_PRESENT) {
            if (i > 0)
                vmm_unmap_pages(pml4_phys, virt_addr, i);
            return -1;
        }

        pt->entries[pt_idx] = (pa & VMM_X64_ADDR_MASK) | pte_flags | VMM_X64_PTE_PRESENT;
    }

    tlb_flush_range(pml4_phys, virt_addr, page_count);
    return 0;
#else
    (void)pml4_phys;
    (void)virt_addr;
    (void)phys_addr;
    (void)page_count;
    (void)map_flags;
    return -1;
#endif
}

int vmm_unmap_pages(uint64_t pml4_phys,
                    uintptr_t virt_addr,
                    size_t page_count)
{
#ifdef __x86_64__
    int user;

    if (!pml4_phys)
        return -1;
    if (validate_span(virt_addr, 0, page_count, 0) != 0)
        return -1;

    user = paging_range_is_user(virt_addr, page_count * (size_t)PAGE_SIZE) ? 1 : 0;
    for (size_t i = 0; i < page_count; i++) {
        if (vmm_query_page(pml4_phys,
                           virt_addr + (uintptr_t)(i * PAGE_SIZE),
                           NULL,
                           NULL) != 0) {
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
    for (size_t i = 0; i < page_count; i++) {
        page_table_t *pt = NULL;
        uintptr_t va = virt_addr + (uintptr_t)(i * PAGE_SIZE);
        uint64_t pt_idx;
        uint64_t entry;

        if (walk_to_pt(pml4_phys, va, 0, user, &pt) != 0)
            return -1;

        pt_idx = ((uint64_t)va >> 12) & 0x1FFULL;
        entry = pt->entries[pt_idx];
        if ((entry & VMM_X64_PTE_PRESENT) == 0)
            return -1;

        pt->entries[pt_idx] = (entry & VMM_X64_ADDR_MASK) | pte_flags | VMM_X64_PTE_PRESENT;
    }

    tlb_flush_range(pml4_phys, virt_addr, page_count);
    return 0;
#else
    (void)pml4_phys;
    (void)virt_addr;
    (void)page_count;
    (void)map_flags;
    return -1;
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

int vmm_map_io_region(uint64_t phys_base, size_t size, uintptr_t *virt_base)
{
    if (!virt_base || size == 0)
        return -1;

#ifdef __x86_64__
    uint64_t phys_page;
    uint64_t phys_end;
    size_t page_count;
    uintptr_t virt_page;
    uint64_t pml4_phys;
    uint64_t page_offset;

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

    for (size_t i = 0; i < page_count; i++) {
        uintptr_t va = virt_page + (uintptr_t)(i * (size_t)PAGE_SIZE);
        uint64_t pa = phys_page + ((uint64_t)i * (uint64_t)PAGE_SIZE);
        uint64_t existing_pa = 0;

        if (vmm_query_page(pml4_phys, va, &existing_pa, NULL) == 0) {
            if ((existing_pa & VMM_X64_ADDR_MASK) != (pa & VMM_X64_ADDR_MASK))
                return -1;
            continue;
        }

        if (vmm_map_pages(pml4_phys,
                          va,
                          pa,
                          1,
                          PAGING_MAP_READ | PAGING_MAP_WRITE |
                              PAGING_MAP_GLOBAL | PAGING_MAP_DEVICE) != 0) {
            return -1;
        }
    }

    *virt_base = virt_page + (uintptr_t)page_offset;
#else
    *virt_base = (uintptr_t)phys_base;
#endif
    return 0;
}
