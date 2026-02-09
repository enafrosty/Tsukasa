/*
 * pmm.h - Physical Memory Manager.
 * Bitmap-based allocator for 4 KiB physical pages.
 */

#ifndef PMM_H
#define PMM_H

#include <stddef.h>
#include <stdint.h>

/** 4 KiB page size. */
#define PAGE_SIZE 4096

/** Maximum physical memory to track (256 MiB). */
#define PMM_MAX_MEM_MB 256

/** Total number of frames. */
#define PMM_FRAME_COUNT ((PMM_MAX_MEM_MB * 1024 * 1024) / PAGE_SIZE)

/**
 * Initialize the PMM from Multiboot memory map.
 * Reserves low memory (0-1 MiB), kernel, and modules.
 *
 * @param mb_info Multiboot info structure (may be NULL if no mmap).
 * @return 0 on success, -1 if no memory map available.
 */
int pmm_init(const void *mb_info);

/**
 * Allocate one physical page (4 KiB).
 *
 * @return Physical address of allocated page, or 0 on failure.
 */
uintptr_t pmm_alloc(void);

/**
 * Allocate multiple contiguous physical pages.
 *
 * @param count Number of pages to allocate.
 * @return Physical address of first page, or 0 on failure.
 */
uintptr_t pmm_alloc_pages(size_t count);

/**
 * Free a physical page.
 *
 * @param phys Physical address (must be page-aligned).
 */
void pmm_free(uintptr_t phys);

/**
 * Free multiple contiguous pages.
 *
 * @param phys Physical address of first page.
 * @param count Number of pages to free.
 */
void pmm_free_pages(uintptr_t phys, size_t count);

#endif /* PMM_H */
