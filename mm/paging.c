/*
 * paging.c - x86 paging setup and management.
 * Identity-maps kernel region and enables paging.
 */

#include "include/paging.h"
#include "pmm.h"
#include <stddef.h>
#include <stdint.h>

extern char _kernel_start[];
extern char _kernel_end[];

/** Boot page directory (must be 4K-aligned). */
static page_directory_t boot_pd __attribute__((aligned(4096)));

/** Boot page table for first 4 MiB (identity map). */
static page_table_t boot_pt __attribute__((aligned(4096)));

void paging_init(void)
{
    /* Clear structures. */
    for (unsigned int i = 0; i < PAGING_ENTRIES; i++) {
        boot_pd[i] = 0;
        boot_pt[i] = 0;
    }

    /* Identity-map first 4 MiB (0x00000000 - 0x003FFFFF). */
    for (unsigned int i = 0; i < PAGING_ENTRIES; i++) {
        boot_pt[i] = (i * PAGE_SIZE) | PTE_PRESENT | PTE_WRITABLE;
    }

    /* Point first PD entry to boot page table. */
    boot_pd[0] = ((uintptr_t)boot_pt & 0xFFFFF000u) | PTE_PRESENT | PTE_WRITABLE;

    /* Load CR3 and enable paging. */
    uintptr_t pd_phys = (uintptr_t)boot_pd;
    __asm__ volatile (
        "movl %0, %%cr3\n"
        "movl %%cr0, %%eax\n"
        "orl $0x80000000, %%eax\n"
        "movl %%eax, %%cr0\n"
        :
        : "r"(pd_phys)
        : "eax"
    );
}

uintptr_t paging_get_current_pd(void)
{
    uintptr_t cr3;
    __asm__ volatile ("movl %%cr3, %0" : "=r"(cr3));
    return cr3;
}

int paging_map(uintptr_t virt, uintptr_t phys, uint32_t flags)
{
    unsigned int pde_idx = PDE_INDEX(virt);
    unsigned int pte_idx = PTE_INDEX(virt);

    if (pde_idx == 0) {
        /* Use boot page table for first 4 MiB. */
        boot_pt[pte_idx] = (phys & 0xFFFFF000u) | (flags & 0xFFF);
        return 0;
    }

    /* For higher addresses, we would need to allocate a new page table.
     * For now, only first 4 MiB is supported in paging_map. */
    (void)pde_idx;
    return -1;
}

void paging_unmap(uintptr_t virt)
{
    unsigned int pde_idx = PDE_INDEX(virt);
    unsigned int pte_idx = PTE_INDEX(virt);

    if (pde_idx == 0)
        boot_pt[pte_idx] = 0;
}
