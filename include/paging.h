/*
 * paging.h - x86 paging structures and flags.
 */

#ifndef PAGING_H
#define PAGING_H

#include <stddef.h>
#include <stdint.h>

/** Page size in bytes (must match mm/pmm.h). */
#ifndef PAGE_SIZE
#define PAGE_SIZE 4096
#endif

/** Page table/directory entry flags. */
#define PTE_PRESENT   (1u << 0)
#define PTE_WRITABLE  (1u << 1)
#define PTE_USER      (1u << 2)
#define PTE_ACCESSED  (1u << 5)
#define PTE_DIRTY     (1u << 6)

/** Number of entries per page table/directory. */
#define PAGING_ENTRIES 1024

/** Virtual address to page directory index. */
#define PDE_INDEX(virt) (((uint32_t)(virt) >> 22) & 0x3FFu)

/** Virtual address to page table index. */
#define PTE_INDEX(virt) (((uint32_t)(virt) >> 12) & 0x3FFu)

/** Page directory entry (points to page table). */
typedef uint32_t pde_t;

/** Page table entry (points to physical frame). */
typedef uint32_t pte_t;

/** Page directory (array of 1024 PDEs). */
typedef pde_t page_directory_t[PAGING_ENTRIES];

/** Page table (array of 1024 PTEs). */
typedef pte_t page_table_t[PAGING_ENTRIES];

/**
 * Initialize paging: create boot page directory, identity-map kernel region,
 * enable CR0.PG. Must be called before any paging operations.
 */
void paging_init(void);

/**
 * Get the current page directory physical address (for loading into CR3).
 *
 * @return Physical address of current page directory.
 */
uintptr_t paging_get_current_pd(void);

/**
 * Map a physical page to a virtual address in the current page directory.
 *
 * @param virt Virtual address (page-aligned).
 * @param phys Physical address (page-aligned).
 * @param flags PTE flags (PTE_PRESENT, PTE_WRITABLE, PTE_USER).
 * @return 0 on success, -1 on failure.
 */
int paging_map(uintptr_t virt, uintptr_t phys, uint32_t flags);

/**
 * Unmap a virtual page.
 *
 * @param virt Virtual address (page-aligned).
 */
void paging_unmap(uintptr_t virt);

/**
 * Identity-map a physical range (e.g. framebuffer) so it is accessible with paging on.
 * Use for framebuffer above 4 MiB. Maps a full 4 MiB region containing phys_base.
 *
 * @param phys_base Physical base address of the region (e.g. framebuffer_addr).
 * @param size      Size in bytes (used only to ensure we map at least one 4 MiB region).
 * @return 0 on success, -1 if phys_base is in first 4 MiB (already mapped).
 */
int paging_map_framebuffer(uintptr_t phys_base, size_t size);

#endif /* PAGING_H */
