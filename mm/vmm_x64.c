#include "vmm_x64.h"

#include <stdint.h>

#include "pmm.h"

static uint64_t g_hhdm_offset;

#ifdef __x86_64__

typedef struct {
    uint64_t entries[512];
} page_table_t;

#define VMM_X64_ADDR_MASK 0x000FFFFFFFFFF000ULL

static uint64_t read_cr3(void)
{
    uint64_t cr3;
    __asm__ volatile ("mov %%cr3, %0" : "=r"(cr3));
    return cr3;
}

static page_table_t *table_virt(uint64_t phys)
{
    return (page_table_t *)(uintptr_t)vmm_phys_to_virt(phys & VMM_X64_ADDR_MASK);
}

static uint64_t alloc_table_phys(void)
{
    uint64_t phys = (uint64_t)pmm_alloc_pages(1);
    if (!phys)
        return 0;

    page_table_t *tbl = table_virt(phys);
    for (uint32_t i = 0; i < 512; i++)
        tbl->entries[i] = 0;

    return phys;
}

#endif

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
    return read_cr3() & 0x000FFFFFFFFFF000ULL;
#else
    return 0;
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

int vmm_map_page(uintptr_t virt_addr, uint64_t phys_addr, uint64_t flags)
{
#ifdef __x86_64__
    uint64_t pml4_phys;
    page_table_t *pml4;
    uint64_t pml4_idx, pdpt_idx, pd_idx, pt_idx;

    if ((virt_addr & (VMM_X64_PAGE_SIZE - 1)) || (phys_addr & (VMM_X64_PAGE_SIZE - 1)))
        return -1;

    pml4_phys = vmm_get_current_pml4();
    if (!pml4_phys)
        return -1;

    pml4 = table_virt(pml4_phys);

    pml4_idx = ((uint64_t)virt_addr >> 39) & 0x1FFULL;
    pdpt_idx = ((uint64_t)virt_addr >> 30) & 0x1FFULL;
    pd_idx = ((uint64_t)virt_addr >> 21) & 0x1FFULL;
    pt_idx = ((uint64_t)virt_addr >> 12) & 0x1FFULL;

    if (!(pml4->entries[pml4_idx] & VMM_X64_PTE_PRESENT)) {
        uint64_t new_pdpt = alloc_table_phys();
        if (!new_pdpt)
            return -1;
        pml4->entries[pml4_idx] = new_pdpt | VMM_X64_PTE_PRESENT | VMM_X64_PTE_WRITABLE;
    }

    page_table_t *pdpt = table_virt(pml4->entries[pml4_idx]);
    if (!(pdpt->entries[pdpt_idx] & VMM_X64_PTE_PRESENT)) {
        uint64_t new_pd = alloc_table_phys();
        if (!new_pd)
            return -1;
        pdpt->entries[pdpt_idx] = new_pd | VMM_X64_PTE_PRESENT | VMM_X64_PTE_WRITABLE;
    }

    page_table_t *pd = table_virt(pdpt->entries[pdpt_idx]);
    if (!(pd->entries[pd_idx] & VMM_X64_PTE_PRESENT)) {
        uint64_t new_pt = alloc_table_phys();
        if (!new_pt)
            return -1;
        pd->entries[pd_idx] = new_pt | VMM_X64_PTE_PRESENT | VMM_X64_PTE_WRITABLE;
    }

    page_table_t *pt = table_virt(pd->entries[pd_idx]);
    pt->entries[pt_idx] = (phys_addr & VMM_X64_ADDR_MASK) | flags | VMM_X64_PTE_PRESENT;

    __asm__ volatile ("invlpg (%0)" : : "r"(virt_addr) : "memory");
    return 0;
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
    uint64_t pml4_phys = vmm_get_current_pml4();
    if (!pml4_phys)
        return -1;

    page_table_t *pml4 = table_virt(pml4_phys);
    uint64_t pml4_idx = ((uint64_t)virt_addr >> 39) & 0x1FFULL;
    uint64_t pdpt_idx = ((uint64_t)virt_addr >> 30) & 0x1FFULL;
    uint64_t pd_idx = ((uint64_t)virt_addr >> 21) & 0x1FFULL;
    uint64_t pt_idx = ((uint64_t)virt_addr >> 12) & 0x1FFULL;

    if (!(pml4->entries[pml4_idx] & VMM_X64_PTE_PRESENT))
        return -1;

    page_table_t *pdpt = table_virt(pml4->entries[pml4_idx]);
    if (!(pdpt->entries[pdpt_idx] & VMM_X64_PTE_PRESENT))
        return -1;

    page_table_t *pd = table_virt(pdpt->entries[pdpt_idx]);
    if (!(pd->entries[pd_idx] & VMM_X64_PTE_PRESENT))
        return -1;

    page_table_t *pt = table_virt(pd->entries[pd_idx]);
    pt->entries[pt_idx] = 0;

    __asm__ volatile ("invlpg (%0)" : : "r"(virt_addr) : "memory");
    return 0;
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
    *virt_base = vmm_phys_to_virt(phys_base);
#else
    *virt_base = (uintptr_t)phys_base;
#endif
    return 0;
}