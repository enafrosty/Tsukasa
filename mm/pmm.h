/*
 * Project Tsukasa — Physical Memory Manager
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

#ifndef PMM_H
#define PMM_H

#include <stddef.h>
#include <stdint.h>

/* 4 KiB page size. */
#define PAGE_SIZE 4096

#ifdef __x86_64__
/* Maximum physical memory to track on x64 path (64 GiB). */
#define PMM_MAX_MEM_MB 65536
#else
/* Maximum physical memory to track on i386 path (256 MiB). */
#define PMM_MAX_MEM_MB 256
#endif

/* Total number of frames. */
#define PMM_FRAME_COUNT \
    ((size_t)(((uint64_t)PMM_MAX_MEM_MB * 1024ULL * 1024ULL) / PAGE_SIZE))

/* Initialize the PMM from Multiboot memory map. */
int pmm_init(const void *mb_info);

uintptr_t pmm_alloc(void);

/* Allocate multiple contiguous physical pages. */
uintptr_t pmm_alloc_pages(size_t count);

void pmm_free(uintptr_t phys);

/* Free multiple contiguous pages. @param phys Physical address of first page. @param count Number of pages... */
void pmm_free_pages(uintptr_t phys, size_t count);

/* PMM accounting snapshots. */
uintptr_t pmm_total_page_count(void);
uintptr_t pmm_used_page_count(void);
uintptr_t pmm_free_page_count(void);

#endif /* PMM_H */
