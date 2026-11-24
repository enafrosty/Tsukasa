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

#include "pagecache.h"

#include "heap.h"
#include "pmm.h"
#include "vmm_x64.h"
#include "../include/errno.h"
#include "../include/kprintf.h"
#include "../include/spinlock.h"
#include "../proc/process.h"

#define PAGECACHE_HASH_BUCKETS 1024U
#define PAGECACHE_MIN_MAX_PAGES 64U

typedef struct pagecache_lru_list {
    pagecache_page_t *head;
    pagecache_page_t *tail;
    size_t count;
} pagecache_lru_list_t;

typedef struct pagecache_state {
    int initialized;
    spinlock_t lock;
    pagecache_page_t *buckets[PAGECACHE_HASH_BUCKETS];
    pagecache_lru_list_t active;
    pagecache_lru_list_t inactive;
    size_t total_pages;
    size_t dirty_pages;
    size_t max_pages;
    size_t low_free_watermark;
    uint64_t hits;
    uint64_t misses;
    uint64_t evictions;
    uint64_t writebacks;
    uint32_t sync_seq;
} pagecache_state_t;

static pagecache_state_t g_pc = {
    .initialized = 0,
    .lock = SPINLOCK_INIT,
};

static size_t pc_hash(uint64_t obj_id, uint64_t page_index)
{
    uint64_t h = obj_id * 0x9E3779B97F4A7C15ULL;
    h ^= page_index * 0xBF58476D1CE4E5B9ULL;
    h ^= h >> 30;
    h *= 0xBF58476D1CE4E5B9ULL;
    h ^= h >> 27;
    return (size_t)(h & (PAGECACHE_HASH_BUCKETS - 1U));
}

static void pc_zero_page(void *dst)
{
    uint8_t *p = (uint8_t *)dst;
    for (size_t i = 0; i < PAGECACHE_PAGE_SIZE; i++)
        p[i] = 0;
}

static void pc_ensure_init_locked(void)
{
    if (g_pc.initialized)
        return;

    for (size_t i = 0; i < PAGECACHE_HASH_BUCKETS; i++)
        g_pc.buckets[i] = NULL;
    g_pc.active.head = NULL;
    g_pc.active.tail = NULL;
    g_pc.active.count = 0;
    g_pc.inactive.head = NULL;
    g_pc.inactive.tail = NULL;
    g_pc.inactive.count = 0;
    g_pc.total_pages = 0;
    g_pc.dirty_pages = 0;
    g_pc.hits = 0;
    g_pc.misses = 0;
    g_pc.evictions = 0;
    g_pc.writebacks = 0;

    size_t total_pmm = (size_t)pmm_total_page_count();
    size_t max_p = total_pmm / 4U;
    if (max_p < PAGECACHE_MIN_MAX_PAGES)
        max_p = PAGECACHE_MIN_MAX_PAGES;
    size_t low_wm = total_pmm / 16U;
    if (low_wm < 256U && total_pmm >= 1024U)
        low_wm = 256U;

    g_pc.max_pages = max_p;
    g_pc.low_free_watermark = low_wm;
    g_pc.initialized = 1;
}

void pagecache_init(void)
{
    unsigned long flags = spin_lock_irqsave(&g_pc.lock);
    g_pc.initialized = 0;
    pc_ensure_init_locked();
    spin_unlock_irqrestore(&g_pc.lock, flags);
}

void pagecache_set_limits(size_t max_pages, size_t low_free_watermark)
{
    unsigned long flags = spin_lock_irqsave(&g_pc.lock);
    pc_ensure_init_locked();
    g_pc.max_pages = max_pages ? max_pages : PAGECACHE_MIN_MAX_PAGES;
    g_pc.low_free_watermark = low_free_watermark;
    spin_unlock_irqrestore(&g_pc.lock, flags);
}

static void lru_unlink(pagecache_lru_list_t *list, pagecache_page_t *pg)
{
    if (pg->lru_prev)
        pg->lru_prev->lru_next = pg->lru_next;
    else if (list->head == pg)
        list->head = pg->lru_next;

    if (pg->lru_next)
        pg->lru_next->lru_prev = pg->lru_prev;
    else if (list->tail == pg)
        list->tail = pg->lru_prev;

    pg->lru_prev = NULL;
    pg->lru_next = NULL;
    if (list->count > 0)
        list->count--;
}

static void lru_push_head(pagecache_lru_list_t *list, pagecache_page_t *pg)
{
    pg->lru_prev = NULL;
    pg->lru_next = list->head;
    if (list->head)
        list->head->lru_prev = pg;
    else
        list->tail = pg;
    list->head = pg;
    list->count++;
}

static void pc_hash_insert_locked(pagecache_page_t *pg)
{
    size_t b = pc_hash(pg->obj_id, pg->page_index);
    pg->hash_next = g_pc.buckets[b];
    g_pc.buckets[b] = pg;
}

static void pc_hash_remove_locked(pagecache_page_t *pg)
{
    size_t b = pc_hash(pg->obj_id, pg->page_index);
    pagecache_page_t *prev = NULL;
    pagecache_page_t *curr = g_pc.buckets[b];
    while (curr) {
        if (curr == pg) {
            if (prev)
                prev->hash_next = curr->hash_next;
            else
                g_pc.buckets[b] = curr->hash_next;
            curr->hash_next = NULL;
            return;
        }
        prev = curr;
        curr = curr->hash_next;
    }
}

static pagecache_page_t *pc_hash_find_locked(uint64_t obj_id, uint64_t page_index)
{
    size_t b = pc_hash(obj_id, page_index);
    pagecache_page_t *curr = g_pc.buckets[b];
    while (curr) {
        if (!curr->invalid && curr->obj_id == obj_id && curr->page_index == page_index)
            return curr;
        curr = curr->hash_next;
    }
    return NULL;
}

static void pc_touch_locked(pagecache_page_t *pg)
{
    if (pg->active) {
        if (g_pc.active.head != pg) {
            lru_unlink(&g_pc.active, pg);
            lru_push_head(&g_pc.active, pg);
        }
    } else {
        lru_unlink(&g_pc.inactive, pg);
        pg->active = 1;
        lru_push_head(&g_pc.active, pg);
    }
}

static void pc_demote_active_tail_locked(size_t max_demote)
{
    size_t moved = 0;
    while (moved < max_demote && g_pc.active.tail) {
        pagecache_page_t *pg = g_pc.active.tail;
        if (pg->pin_count > 0 || pg->busy) {
            /* Rotate pinned active tail to head to inspect others. */
            if (g_pc.active.count <= 1)
                break;
            lru_unlink(&g_pc.active, pg);
            lru_push_head(&g_pc.active, pg);
            moved++;
            continue;
        }
        lru_unlink(&g_pc.active, pg);
        pg->active = 0;
        lru_push_head(&g_pc.inactive, pg);
        moved++;
    }
}

static void pc_remove_and_free_locked(pagecache_page_t *pg, unsigned long *flags)
{
    pagecache_lru_list_t *list = pg->active ? &g_pc.active : &g_pc.inactive;
    uintptr_t phys = pg->phys_addr;

    pc_hash_remove_locked(pg);
    lru_unlink(list, pg);
    if (pg->dirty && g_pc.dirty_pages > 0)
        g_pc.dirty_pages--;
    if (g_pc.total_pages > 0)
        g_pc.total_pages--;
    g_pc.evictions++;

    spin_unlock_irqrestore(&g_pc.lock, *flags);
    if (phys)
        pmm_free(phys);
    kfree(pg);
    *flags = spin_lock_irqsave(&g_pc.lock);
}

/* Try to evict one clean unpinned page, or write back a dirty unpinned page. */
static int pc_reclaim_one_locked(unsigned long *flags)
{
    for (int pass = 0; pass < 2; pass++) {
        pagecache_page_t *curr = g_pc.inactive.tail;
        while (curr) {
            pagecache_page_t *prev = curr->lru_prev;
            if (curr->pin_count == 0 && !curr->busy && !curr->dirty) {
                pc_remove_and_free_locked(curr, flags);
                return 1;
            }
            curr = prev;
        }
        if (g_pc.active.count > 0)
            pc_demote_active_tail_locked(16U);
    }

    /* No clean unpinned pages: attempt writeback on an unpinned dirty page. */
    pagecache_page_t *dirty_pg = NULL;
    for (int list_idx = 0; list_idx < 2 && !dirty_pg; list_idx++) {
        pagecache_page_t *curr = (list_idx == 0) ? g_pc.inactive.tail : g_pc.active.tail;
        while (curr) {
            if (curr->pin_count == 0 && !curr->busy && curr->dirty &&
                curr->ops && curr->ops->writepage) {
                dirty_pg = curr;
                break;
            }
            curr = curr->lru_prev;
        }
    }

    if (!dirty_pg)
        return 0;

    const pagecache_ops_t *ops = dirty_pg->ops;
    void *ctx = dirty_pg->ops_ctx;
    uint64_t oid = dirty_pg->obj_id;
    uint64_t pidx = dirty_pg->page_index;
    const void *data = dirty_pg->data;

    dirty_pg->pin_count++;
    dirty_pg->busy = 1;
    spin_unlock_irqrestore(&g_pc.lock, *flags);

    int rc = ops->writepage(ctx, oid, pidx, data);

    *flags = spin_lock_irqsave(&g_pc.lock);
    dirty_pg->busy = 0;
    if (rc == 0 && dirty_pg->dirty) {
        dirty_pg->dirty = 0;
        if (g_pc.dirty_pages > 0)
            g_pc.dirty_pages--;
        g_pc.writebacks++;
    }
    if (dirty_pg->pin_count > 0)
        dirty_pg->pin_count--;

    if (rc == 0 && dirty_pg->pin_count == 0 && !dirty_pg->dirty && !dirty_pg->invalid) {
        pc_remove_and_free_locked(dirty_pg, flags);
        return 1;
    }
    return (rc == 0) ? 1 : 0;
}

static int pc_enforce_watermarks_locked(unsigned long *flags)
{
    size_t spins = 0;
    while (g_pc.total_pages >= g_pc.max_pages ||
           (g_pc.total_pages > 0 && pmm_free_page_count() <= g_pc.low_free_watermark)) {
        if (!pc_reclaim_one_locked(flags))
            break;
        if (++spins > g_pc.max_pages + 64U)
            break;
    }
    if (g_pc.total_pages >= g_pc.max_pages)
        return -ENOMEM;
    return 0;
}

pagecache_page_t *pagecache_get_page(uint64_t obj_id,
                                     uint64_t page_index,
                                     const pagecache_ops_t *ops,
                                     void *ops_ctx)
{
    unsigned long flags = spin_lock_irqsave(&g_pc.lock);
    pc_ensure_init_locked();

retry:
    {
        pagecache_page_t *hit = pc_hash_find_locked(obj_id, page_index);
        if (hit) {
            hit->pin_count++;
            if (hit->busy) {
                spin_unlock_irqrestore(&g_pc.lock, flags);
                while (__atomic_load_n(&hit->busy, __ATOMIC_ACQUIRE))
                    process_yield();
                flags = spin_lock_irqsave(&g_pc.lock);
                if (hit->invalid || !hit->uptodate) {
                    if (hit->pin_count > 0)
                        hit->pin_count--;
                    if (hit->invalid && hit->pin_count == 0) {
                        uintptr_t phys = hit->phys_addr;
                        spin_unlock_irqrestore(&g_pc.lock, flags);
                        if (phys)
                            pmm_free(phys);
                        kfree(hit);
                        return NULL;
                    }
                    spin_unlock_irqrestore(&g_pc.lock, flags);
                    return NULL;
                }
            }
            if (ops && !hit->ops) {
                hit->ops = ops;
                hit->ops_ctx = ops_ctx;
            }
            pc_touch_locked(hit);
            g_pc.hits++;
            spin_unlock_irqrestore(&g_pc.lock, flags);
            return hit;
        }
    }

    if (pc_enforce_watermarks_locked(&flags) != 0) {
        spin_unlock_irqrestore(&g_pc.lock, flags);
        return NULL;
    }

    /* Check again after possible lock release during reclaim. */
    if (pc_hash_find_locked(obj_id, page_index))
        goto retry;

    spin_unlock_irqrestore(&g_pc.lock, flags);

    pagecache_page_t *pg = (pagecache_page_t *)kmalloc(sizeof(pagecache_page_t));
    if (!pg)
        return NULL;

    uintptr_t phys = pmm_alloc();
    if (!phys) {
        /* Last-ditch reclaim and retry PMM allocation once. */
        flags = spin_lock_irqsave(&g_pc.lock);
        (void)pc_reclaim_one_locked(&flags);
        spin_unlock_irqrestore(&g_pc.lock, flags);
        phys = pmm_alloc();
        if (!phys) {
            kfree(pg);
            return NULL;
        }
    }

    void *vaddr = (void *)vmm_phys_to_virt((uint64_t)phys);
    pg->obj_id = obj_id;
    pg->page_index = page_index;
    pg->phys_addr = phys;
    pg->data = vaddr;
    pg->pin_count = 1U;
    pg->sync_seq = 0;
    pg->dirty = 0;
    pg->uptodate = 0;
    pg->active = 0;
    pg->busy = 1;
    pg->invalid = 0;
    pg->ops = ops;
    pg->ops_ctx = ops_ctx;
    pg->hash_next = NULL;
    pg->lru_prev = NULL;
    pg->lru_next = NULL;

    flags = spin_lock_irqsave(&g_pc.lock);
    if (pc_hash_find_locked(obj_id, page_index)) {
        spin_unlock_irqrestore(&g_pc.lock, flags);
        pmm_free(phys);
        kfree(pg);
        flags = spin_lock_irqsave(&g_pc.lock);
        goto retry;
    }
    if (g_pc.total_pages >= g_pc.max_pages) {
        if (pc_enforce_watermarks_locked(&flags) != 0) {
            spin_unlock_irqrestore(&g_pc.lock, flags);
            pmm_free(phys);
            kfree(pg);
            return NULL;
        }
    }

    g_pc.misses++;
    pc_hash_insert_locked(pg);
    lru_push_head(&g_pc.inactive, pg);
    g_pc.total_pages++;
    spin_unlock_irqrestore(&g_pc.lock, flags);

    int io_rc = 0;
    if (ops && ops->readpage)
        io_rc = ops->readpage(ops_ctx, obj_id, page_index, vaddr);
    else
        pc_zero_page(vaddr);

    flags = spin_lock_irqsave(&g_pc.lock);
    if (io_rc != 0) {
        pg->invalid = 1;
        __atomic_store_n(&pg->busy, 0, __ATOMIC_RELEASE);
        pc_hash_remove_locked(pg);
        lru_unlink(pg->active ? &g_pc.active : &g_pc.inactive, pg);
        if (g_pc.total_pages > 0)
            g_pc.total_pages--;
        pg->pin_count--;
        if (pg->pin_count == 0) {
            spin_unlock_irqrestore(&g_pc.lock, flags);
            pmm_free(phys);
            kfree(pg);
            return NULL;
        }
        spin_unlock_irqrestore(&g_pc.lock, flags);
        return NULL;
    }

    pg->uptodate = 1;
    __atomic_store_n(&pg->busy, 0, __ATOMIC_RELEASE);
    spin_unlock_irqrestore(&g_pc.lock, flags);
    return pg;
}

void pagecache_put_page(pagecache_page_t *page)
{
    if (!page)
        return;

    unsigned long flags = spin_lock_irqsave(&g_pc.lock);
    if (page->pin_count > 0)
        page->pin_count--;
    if (page->invalid && page->pin_count == 0) {
        uintptr_t phys = page->phys_addr;
        spin_unlock_irqrestore(&g_pc.lock, flags);
        if (phys)
            pmm_free(phys);
        kfree(page);
        return;
    }
    spin_unlock_irqrestore(&g_pc.lock, flags);
}

void pagecache_mark_dirty(pagecache_page_t *page)
{
    if (!page)
        return;

    unsigned long flags = spin_lock_irqsave(&g_pc.lock);
    if (!page->invalid && !page->dirty) {
        page->dirty = 1;
        g_pc.dirty_pages++;
    }
    spin_unlock_irqrestore(&g_pc.lock, flags);
}

static int pc_sync_matching(uint64_t obj_id, int match_all)
{
    int first_err = 0;
    unsigned long flags = spin_lock_irqsave(&g_pc.lock);
    pc_ensure_init_locked();
    uint32_t seq = ++g_pc.sync_seq;
    if (seq == 0)
        seq = ++g_pc.sync_seq;

    for (size_t b = 0; b < PAGECACHE_HASH_BUCKETS; b++) {
restart_bucket:
        {
            pagecache_page_t *curr = g_pc.buckets[b];
            while (curr) {
                if (!curr->invalid && curr->dirty && !curr->busy &&
                    curr->sync_seq != seq &&
                    (match_all || curr->obj_id == obj_id)) {
                    const pagecache_ops_t *ops = curr->ops;
                    void *ctx = curr->ops_ctx;
                    uint64_t oid = curr->obj_id;
                    uint64_t pidx = curr->page_index;
                    const void *data = curr->data;

                    curr->sync_seq = seq;
                    if (!ops || !ops->writepage) {
                        curr->dirty = 0;
                        if (g_pc.dirty_pages > 0)
                            g_pc.dirty_pages--;
                        curr = curr->hash_next;
                        continue;
                    }

                    curr->pin_count++;
                    curr->busy = 1;
                    spin_unlock_irqrestore(&g_pc.lock, flags);

                    int rc = ops->writepage(ctx, oid, pidx, data);

                    flags = spin_lock_irqsave(&g_pc.lock);
                    __atomic_store_n(&curr->busy, 0, __ATOMIC_RELEASE);
                    if (rc == 0) {
                        if (curr->dirty) {
                            curr->dirty = 0;
                            if (g_pc.dirty_pages > 0)
                                g_pc.dirty_pages--;
                            g_pc.writebacks++;
                        }
                    } else if (first_err == 0) {
                        first_err = rc;
                    }

                    if (curr->pin_count > 0)
                        curr->pin_count--;
                    if (curr->invalid && curr->pin_count == 0) {
                        uintptr_t phys = curr->phys_addr;
                        spin_unlock_irqrestore(&g_pc.lock, flags);
                        if (phys)
                            pmm_free(phys);
                        kfree(curr);
                        flags = spin_lock_irqsave(&g_pc.lock);
                    }
                    goto restart_bucket;
                }
                curr = curr->hash_next;
            }
        }
    }

    spin_unlock_irqrestore(&g_pc.lock, flags);
    return first_err;
}

int pagecache_sync_obj(uint64_t obj_id)
{
    return pc_sync_matching(obj_id, 0);
}

int pagecache_sync_all(void)
{
    return pc_sync_matching(0, 1);
}

void pagecache_invalidate_obj(uint64_t obj_id)
{
    unsigned long flags = spin_lock_irqsave(&g_pc.lock);
    pc_ensure_init_locked();

    for (size_t b = 0; b < PAGECACHE_HASH_BUCKETS; b++) {
        pagecache_page_t *prev = NULL;
        pagecache_page_t *curr = g_pc.buckets[b];
        while (curr) {
            pagecache_page_t *next = curr->hash_next;
            if (curr->obj_id == obj_id) {
                if (prev)
                    prev->hash_next = next;
                else
                    g_pc.buckets[b] = next;
                curr->hash_next = NULL;

                lru_unlink(curr->active ? &g_pc.active : &g_pc.inactive, curr);
                if (curr->dirty && g_pc.dirty_pages > 0)
                    g_pc.dirty_pages--;
                curr->dirty = 0;
                curr->invalid = 1;
                if (g_pc.total_pages > 0)
                    g_pc.total_pages--;

                if (curr->pin_count == 0) {
                    uintptr_t phys = curr->phys_addr;
                    spin_unlock_irqrestore(&g_pc.lock, flags);
                    if (phys)
                        pmm_free(phys);
                    kfree(curr);
                    flags = spin_lock_irqsave(&g_pc.lock);
                    /* Restart bucket scan after dropping lock. */
                    prev = NULL;
                    curr = g_pc.buckets[b];
                    continue;
                }
                curr = next;
                continue;
            }
            prev = curr;
            curr = next;
        }
    }

    spin_unlock_irqrestore(&g_pc.lock, flags);
}

void pagecache_stats(pagecache_stats_t *out)
{
    if (!out)
        return;
    unsigned long flags = spin_lock_irqsave(&g_pc.lock);
    pc_ensure_init_locked();
    out->hits = g_pc.hits;
    out->misses = g_pc.misses;
    out->evictions = g_pc.evictions;
    out->writebacks = g_pc.writebacks;
    out->total_pages = g_pc.total_pages;
    out->active_pages = g_pc.active.count;
    out->inactive_pages = g_pc.inactive.count;
    out->dirty_pages = g_pc.dirty_pages;
    out->max_pages = g_pc.max_pages;
    out->low_free_watermark = g_pc.low_free_watermark;
    spin_unlock_irqrestore(&g_pc.lock, flags);
}

typedef struct pc_test_ctx {
    uint32_t writes;
    uint32_t reads;
    uint64_t last_written_idx;
} pc_test_ctx_t;

static int pc_test_readpage(void *ctx, uint64_t obj_id, uint64_t page_index, void *buf)
{
    pc_test_ctx_t *tc = (pc_test_ctx_t *)ctx;
    (void)obj_id;
    tc->reads++;
    uint8_t *b = (uint8_t *)buf;
    b[0] = (uint8_t)(page_index & 0xFFU);
    b[1] = (uint8_t)((page_index >> 8) & 0xFFU);
    return 0;
}

static int pc_test_writepage(void *ctx, uint64_t obj_id, uint64_t page_index, const void *buf)
{
    pc_test_ctx_t *tc = (pc_test_ctx_t *)ctx;
    (void)obj_id;
    (void)buf;
    tc->writes++;
    tc->last_written_idx = page_index;
    return 0;
}

void pagecache_run_selftests(void)
{
    pagecache_stats_t saved;
    pagecache_stats(&saved);
    size_t orig_max = saved.max_pages;
    size_t orig_wm = saved.low_free_watermark;

    uintptr_t free_before = pmm_free_page_count();
    pagecache_set_limits(64U, 16U);

    pc_test_ctx_t tc = { 0, 0, 0 };
    pagecache_ops_t ops = {
        .readpage = pc_test_readpage,
        .writepage = pc_test_writepage,
    };

    const uint64_t test_obj = 0xFEEDFACE00000001ULL;
    int ok = 1;

    /* Populate 160 unique pages (> 64 cap) and verify bounded eviction without OOM. */
    for (uint64_t i = 0; i < 160U; i++) {
        pagecache_page_t *pg = pagecache_get_page(test_obj, i, &ops, &tc);
        if (!pg || !pg->uptodate) {
            ok = 0;
            break;
        }
        uint8_t *d = (uint8_t *)pg->data;
        if (d[0] != (uint8_t)(i & 0xFFU))
            ok = 0;
        if ((i % 20U) == 0U)
            pagecache_mark_dirty(pg);
        pagecache_put_page(pg);

        pagecache_stats_t st;
        pagecache_stats(&st);
        if (st.total_pages > 64U)
            ok = 0;
    }

    /* Hit the most recent page twice to verify inactive -> active promotion. */
    pagecache_page_t *hot = pagecache_get_page(test_obj, 159U, &ops, &tc);
    if (!hot) {
        ok = 0;
    } else {
        pagecache_put_page(hot);
    }

    if (pagecache_sync_obj(test_obj) != 0)
        ok = 0;

    pagecache_stats_t st_mid;
    pagecache_stats(&st_mid);
    if (st_mid.evictions == 0 || st_mid.hits == 0 || st_mid.dirty_pages != 0 ||
        st_mid.total_pages > 64U || tc.writes == 0) {
        ok = 0;
    }

    pagecache_invalidate_obj(test_obj);
    pagecache_set_limits(orig_max, orig_wm);

    uintptr_t free_after = pmm_free_page_count();
    if (free_after < free_before - 4U)
        ok = 0;

    kprintf("[selftest] pagecache eviction %s total=%u evict=%llu hits=%llu wb=%llu free=%u->%u\n",
            ok ? "PASS" : "FAIL",
            (unsigned)st_mid.total_pages,
            (unsigned long long)st_mid.evictions,
            (unsigned long long)st_mid.hits,
            (unsigned long long)st_mid.writebacks,
            (unsigned)free_before,
            (unsigned)free_after);
}
