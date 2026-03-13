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

#define IDENTITY_MAP_MB 16
#define IDENTITY_MAP_TABLES (IDENTITY_MAP_MB / 4)

/** Boot page directory (must be 4K-aligned). */
static page_directory_t boot_pd __attribute__((aligned(4096)));

/** Boot page tables for identity mapping (first 16 MiB). */
static page_table_t boot_pt[IDENTITY_MAP_TABLES] __attribute__((aligned(4096)));

/** Page table for framebuffer (one 4 MiB region above low memory). */
static page_table_t boot_pt_fb __attribute__((aligned(4096)));

void paging_init(void)
{
    /* Clear structures. */
    for (unsigned int i = 0; i < PAGING_ENTRIES; i++) {
        boot_pd[i] = 0;
    }
    for (unsigned int t = 0; t < IDENTITY_MAP_TABLES; t++) {
        for (unsigned int i = 0; i < PAGING_ENTRIES; i++) {
            boot_pt[t][i] = 0;
        }
    }

    /* Identity-map first 16 MiB. */
    for (unsigned int t = 0; t < IDENTITY_MAP_TABLES; t++) {
        for (unsigned int i = 0; i < PAGING_ENTRIES; i++) {
            uintptr_t phys = (t * PAGING_ENTRIES + i) * PAGE_SIZE;
            boot_pt[t][i] = phys | PTE_PRESENT | PTE_WRITABLE;
        }
        /* Point corresponding PD entry to this boot page table. */
        boot_pd[t] = ((uintptr_t)&boot_pt[t] & 0xFFFFF000u) | PTE_PRESENT | PTE_WRITABLE;
    }

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

    if (pde_idx < IDENTITY_MAP_TABLES) {
        boot_pt[pde_idx][pte_idx] = (phys & 0xFFFFF000u) | (flags & 0xFFF);
        return 0;
    }

    /* For higher addresses, we would need to allocate a new page table.
     * For now, only first 16 MiB is supported in paging_map. */
    (void)pde_idx;
    return -1;
}

void paging_unmap(uintptr_t virt)
{
    unsigned int pde_idx = PDE_INDEX(virt);
    unsigned int pte_idx = PTE_INDEX(virt);

    if (pde_idx < IDENTITY_MAP_TABLES)
        boot_pt[pde_idx][pte_idx] = 0;
}

int paging_map_framebuffer(uintptr_t phys_base, size_t size)
{
    /* Already in identity-mapped region. */
    if (phys_base < (uintptr_t)IDENTITY_MAP_MB * 1024 * 1024)
        return 0;

    uintptr_t region_base = phys_base & ~0x3FFFFFu;  /* Round down to 4 MiB. */
    unsigned int pde_idx = (unsigned int)(region_base >> 22);

    for (unsigned int i = 0; i < PAGING_ENTRIES; i++)
        boot_pt_fb[i] = (region_base + (uintptr_t)i * PAGE_SIZE) | PTE_PRESENT | PTE_WRITABLE;

    boot_pd[pde_idx] = ((uintptr_t)boot_pt_fb & 0xFFFFF000u) | PTE_PRESENT | PTE_WRITABLE;
    (void)size;
    return 0;
}
