/*
 * Project Tsukasa - Unified bounded LRU page cache
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

#ifndef TSUKASA_PAGECACHE_H
#define TSUKASA_PAGECACHE_H

#include <stddef.h>
#include <stdint.h>

#define PAGECACHE_PAGE_SIZE 4096U

#define PAGECACHE_OPS_FS_LOCK   (1U << 0)
#define PAGECACHE_GET_NO_FS_WB  (1U << 0)

typedef struct pagecache_ops {
    int (*readpage)(void *ctx, uint64_t obj_id, uint64_t page_index, void *buf);
    int (*writepage)(void *ctx, uint64_t obj_id, uint64_t page_index, const void *buf);
    uint32_t flags;
} pagecache_ops_t;

typedef struct pagecache_page {
    uint64_t obj_id;
    uint64_t page_index;
    uintptr_t phys_addr;
    void *data;
    uint32_t pin_count;
    uint32_t sync_seq;
    uint8_t dirty;
    uint8_t uptodate;
    uint8_t active;
    uint8_t busy;
    uint8_t invalid;
    const pagecache_ops_t *ops;
    void *ops_ctx;
    struct pagecache_page *hash_next;
    struct pagecache_page *lru_prev;
    struct pagecache_page *lru_next;
} pagecache_page_t;

typedef struct pagecache_stats {
    uint64_t hits;
    uint64_t misses;
    uint64_t evictions;
    uint64_t writebacks;
    size_t total_pages;
    size_t active_pages;
    size_t inactive_pages;
    size_t dirty_pages;
    size_t max_pages;
    size_t low_free_watermark;
} pagecache_stats_t;

void pagecache_init(void);
void pagecache_set_limits(size_t max_pages, size_t low_free_watermark);

pagecache_page_t *pagecache_get_page(uint64_t obj_id,
                                     uint64_t page_index,
                                     const pagecache_ops_t *ops,
                                     void *ops_ctx);
pagecache_page_t *pagecache_get_page_flags(uint64_t obj_id,
                                           uint64_t page_index,
                                           const pagecache_ops_t *ops,
                                           void *ops_ctx,
                                           uint32_t get_flags);
pagecache_page_t *pagecache_lookup_page(uint64_t obj_id, uint64_t page_index);
void pagecache_put_page(pagecache_page_t *page);
void pagecache_mark_dirty(pagecache_page_t *page);

int pagecache_sync_obj(uint64_t obj_id);
int pagecache_sync_obj_range(uint64_t obj_id, uint64_t start_page, uint64_t end_page);
int pagecache_sync_all(void);
void pagecache_invalidate_obj(uint64_t obj_id);
void pagecache_stats(pagecache_stats_t *out);

void pagecache_run_selftests(void);

#endif /* TSUKASA_PAGECACHE_H */
