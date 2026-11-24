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

/* Total number of frames within the supported physical cap. */
#define PMM_FRAME_COUNT \
    ((size_t)(((uint64_t)PMM_MAX_MEM_MB * 1024ULL * 1024ULL) / PAGE_SIZE))

/* Buddy orders 0..11 (4 KiB .. 8 MiB). */
#define PMM_MAX_ORDER 11u

typedef enum pmm_zone {
    PMM_ZONE_DMA    = 0, /* < 16 MiB */
    PMM_ZONE_DMA32  = 1, /* 16 MiB .. < 4 GiB */
    PMM_ZONE_NORMAL = 2, /* >= 4 GiB */
    PMM_ZONE_COUNT  = 3
} pmm_zone_t;

#define PAGE_FLAG_RESERVED  (1U << 0)
#define PAGE_FLAG_BUDDY     (1U << 1)
#define PAGE_FLAG_ANON      (1U << 2)
#define PAGE_FLAG_PAGECACHE (1U << 3)
#define PAGE_FLAG_SLAB      (1U << 4)
#define PAGE_FLAG_KERNEL    (1U << 5)
#define PAGE_FLAG_PCP       (1U << 6)

typedef struct page {
    uint32_t refcount;
    uint32_t flags;
    uint8_t order;
    uint8_t zone;
    uint16_t reserved0;
    uint32_t reserved1;
    struct page *prev;
    struct page *next;
} page_t;

typedef struct pmm_zone_stats {
    size_t total_pages[PMM_ZONE_COUNT];
    size_t free_pages[PMM_ZONE_COUNT];
    size_t free_by_order[PMM_ZONE_COUNT][PMM_MAX_ORDER + 1];
    size_t pcp_pages;
} pmm_zone_stats_t;

/* Initialize the PMM from Limine/Multiboot memory map. */
int pmm_init(const void *mb_info);

/* Per-frame struct page metadata and refcount helpers. */
page_t *pmm_phys_to_page(uintptr_t phys);
uintptr_t pmm_page_to_phys(const page_t *pg);
uint32_t pmm_page_get(uintptr_t phys);
uint32_t pmm_page_put(uintptr_t phys);
uint32_t pmm_page_refcount(uintptr_t phys);

/* Zoned buddy allocator interface. */
uintptr_t pmm_alloc_order(unsigned int order);
void pmm_free_order(uintptr_t phys, unsigned int order);
uintptr_t pmm_alloc_zone(int zone, unsigned int order);

/* Per-CPU order-0 page magazine cache lifecycle. */
void pmm_pcp_init_cpu(uint32_t cpu_id);
void pmm_pcp_drain_all(void);

/* Zone accounting and shared zero page. */
void pmm_get_zone_stats(pmm_zone_stats_t *out);
uintptr_t pmm_zero_page_phys(void);

/* Legacy page allocation helpers backed by the zoned buddy PMM. */
uintptr_t pmm_alloc(void);
uintptr_t pmm_alloc_pages(size_t count);
void pmm_free(uintptr_t phys);
void pmm_free_pages(uintptr_t phys, size_t count);

/* PMM accounting snapshots. */
uintptr_t pmm_total_page_count(void);
uintptr_t pmm_used_page_count(void);
uintptr_t pmm_free_page_count(void);

/* Boot-time buddy/refcount selftests. */
void pmm_run_selftests(void);

#endif /* PMM_H */
