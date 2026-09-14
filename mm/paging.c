/*
 * Project Tsukasa — x86 paging setup and management
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

#include "include/paging.h"
#include "pmm.h"
#include <stddef.h>
#include <stdint.h>

extern char _kernel_start[];
extern char _kernel_end[];

#define IDENTITY_MAP_MB 16
#define IDENTITY_MAP_TABLES (IDENTITY_MAP_MB / 4)

/* Boot page directory (must be 4K-aligned). */
static page_directory_t boot_pd __attribute__((aligned(4096)));

/* Boot page tables for identity mapping (first 16 MiB). */
static page_table_t boot_pt[IDENTITY_MAP_TABLES] __attribute__((aligned(4096)));

/* Page table for framebuffer (one 4 MiB region above low memory). */
static page_table_t boot_pt_fb __attribute__((aligned(4096)));

void paging_init(void)
{
    for (unsigned int i = 0; i < PAGING_ENTRIES; i++) {
        boot_pd[i] = 0;
    }
    for (unsigned int t = 0; t < IDENTITY_MAP_TABLES; t++) {
        for (unsigned int i = 0; i < PAGING_ENTRIES; i++) {
            boot_pt[t][i] = 0;
        }
    }

    for (unsigned int t = 0; t < IDENTITY_MAP_TABLES; t++) {
        for (unsigned int i = 0; i < PAGING_ENTRIES; i++) {
            uintptr_t phys = (t * PAGING_ENTRIES + i) * PAGE_SIZE;
            boot_pt[t][i] = phys | PTE_PRESENT | PTE_WRITABLE;
        }
        boot_pd[t] = ((uintptr_t)&boot_pt[t] & 0xFFFFF000u) | PTE_PRESENT | PTE_WRITABLE;
    }

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

    /* For higher addresses, we would need to allocate a new page table. For now, only first 16 MiB is supported... */
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
    if (phys_base < (uintptr_t)IDENTITY_MAP_MB * 1024 * 1024)
        return 0;

    uintptr_t region_base = phys_base & ~0x3FFFFFu;
    unsigned int pde_idx = (unsigned int)(region_base >> 22);

    for (unsigned int i = 0; i < PAGING_ENTRIES; i++)
        boot_pt_fb[i] = (region_base + (uintptr_t)i * PAGE_SIZE) | PTE_PRESENT | PTE_WRITABLE;

    boot_pd[pde_idx] = ((uintptr_t)boot_pt_fb & 0xFFFFF000u) | PTE_PRESENT | PTE_WRITABLE;
    (void)size;
    return 0;
}
