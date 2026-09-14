/*
 * Project Tsukasa — slab allocator: fixed-size object caches over the PMM
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

#include <stddef.h>
#include <stdint.h>

#include "slab.h"
#include "pmm.h"
#include "vmm_x64.h"
#include "../include/kprintf.h"
#include "../include/spinlock.h"

#define SLAB_CLASSES        7
#define SLAB_PAGE_MAGIC     0x534C4142u          /* "SLAB" */
#define SLAB_PAGE_MAGIC_INV (~SLAB_PAGE_MAGIC)

static const uint16_t slab_sizes[SLAB_CLASSES] = { 8, 16, 32, 64, 128, 256, 512 };

/* Each slab page is exactly PAGE_SIZE: this header at the base, then total_count slots of obj_size bytes... */
typedef struct slab_page {
    uint32_t magic;
    uint32_t magic_inv;
    uint16_t obj_size;
    uint16_t free_count;
    uint16_t total_count;
    uint16_t obj_start;
    uint16_t class_idx;
    uint16_t pad0;
    struct slab_page *next;
    void *freelist;
} slab_page_t;

typedef struct {
    slab_page_t *pages;
    size_t allocs;
    size_t frees;
    size_t page_count;
} slab_cache_t;

static slab_cache_t g_caches[SLAB_CLASSES];
static size_t g_total_allocs;
static size_t g_total_frees;
static size_t g_total_pages;
static int g_warned_double_free;
static spinlock_t g_slab_lock = SPINLOCK_INIT;

static void zero_bytes(void *p, size_t n)
{
    uint8_t *b = (uint8_t *)p;
    for (size_t i = 0; i < n; i++)
        b[i] = 0;
}

int slab_class_for_size(size_t size)
{
    if (size == 0)
        return -1;
    for (int i = 0; i < SLAB_CLASSES; i++) {
        if (size <= slab_sizes[i])
            return i;
    }
    return -1;
}

static int ptr_belongs_to_page(const slab_page_t *page, const void *ptr)
{
    uintptr_t base = (uintptr_t)page + page->obj_start;
    uintptr_t p = (uintptr_t)ptr;
    if (p < base || p >= (uintptr_t)page + PAGE_SIZE)
        return 0;
    return ((p - base) % page->obj_size) == 0;
}

static int page_in_cache(const slab_cache_t *cache, const slab_page_t *target)
{
    for (const slab_page_t *p = cache->pages; p; p = p->next) {
        if (p == target)
            return 1;
    }
    return 0;
}

/* Walk the freelist to catch double-frees before they corrupt it: pushing the same slot twice turns the... */
static int ptr_is_free_in_page(const slab_page_t *page, const void *ptr)
{
    const void *it = page->freelist;
    uint16_t seen = 0;

    while (it && seen < page->total_count) {
        if (it == ptr)
            return 1;
        if (!ptr_belongs_to_page(page, it))
            return 0;
        it = *(void *const *)it;
        seen++;
    }
    return 0;
}

/* One fresh page from the PMM, freelist threaded through every slot. */
static slab_page_t *slab_new_page(int cls)
{
    uint16_t obj_size = slab_sizes[cls];

    uintptr_t phys = pmm_alloc_pages(1);
    if (!phys)
        return NULL;
    slab_page_t *page = (slab_page_t *)vmm_phys_to_virt((uint64_t)phys);
    if (!page) {
        pmm_free_pages(phys, 1);
        return NULL;
    }

    size_t hdr_end = sizeof(slab_page_t);
    size_t obj_start = (hdr_end + obj_size - 1) & ~(size_t)(obj_size - 1);
    uint16_t count = (uint16_t)((PAGE_SIZE - obj_start) / obj_size);
    if (obj_start >= PAGE_SIZE || count == 0) {
        pmm_free_pages(phys, 1);
        return NULL;
    }

    page->magic = SLAB_PAGE_MAGIC;
    page->magic_inv = SLAB_PAGE_MAGIC_INV;
    page->obj_size = obj_size;
    page->free_count = count;
    page->total_count = count;
    page->obj_start = (uint16_t)obj_start;
    page->class_idx = (uint16_t)cls;
    page->pad0 = 0;
    page->next = NULL;

    uintptr_t base = (uintptr_t)page + obj_start;
    for (uint16_t k = 0; k + 1 < count; k++)
        *(void **)(base + (size_t)k * obj_size) =
            (void *)(base + (size_t)(k + 1) * obj_size);
    *(void **)(base + (size_t)(count - 1) * obj_size) = NULL;
    page->freelist = (void *)base;

    g_total_pages++;
    g_caches[cls].page_count++;
    return page;
}

/* Locate the owning page by masking the low bits (pages are PAGE_SIZE-sized and aligned), then run the... */
static int slab_owns_locked(void *ptr, slab_page_t **out)
{
    if (!ptr)
        return 0;

    slab_page_t *page = (slab_page_t *)((uintptr_t)ptr & ~(uintptr_t)(PAGE_SIZE - 1));

    if (page->magic != SLAB_PAGE_MAGIC)
        return 0;
    if (page->magic_inv != SLAB_PAGE_MAGIC_INV)
        return 0;
    if (!page->obj_size || !page->total_count)
        return 0;
    if (page->obj_start < sizeof(slab_page_t) || page->obj_start >= PAGE_SIZE)
        return 0;
    if (page->class_idx >= SLAB_CLASSES)
        return 0;
    if (slab_sizes[page->class_idx] != page->obj_size)
        return 0;
    if (page->free_count > page->total_count)
        return 0;

    uint16_t expected = (uint16_t)((PAGE_SIZE - page->obj_start) / page->obj_size);
    if (expected != page->total_count)
        return 0;

    if (page->freelist && !ptr_belongs_to_page(page, page->freelist))
        return 0;
    if (!page_in_cache(&g_caches[page->class_idx], page))
        return 0;
    if (!ptr_belongs_to_page(page, ptr))
        return 0;

    *out = page;
    return 1;
}

/* Caller holds g_slab_lock. */
static void *slab_alloc_locked(int cls)
{
    slab_cache_t *cache = &g_caches[cls];

    slab_page_t *page = cache->pages;
    while (page && page->free_count == 0)
        page = page->next;

    if (!page) {
        page = slab_new_page(cls);
        if (!page)
            return NULL;
        page->next = cache->pages;
        cache->pages = page;
    }

    void *obj = page->freelist;

#ifdef __x86_64__
    if ((uintptr_t)obj < 0xFFFF800000000000ULL) {
        kprintf("[slab] ERROR: corrupt freelist cls=%d page=0x%08x%08x fl=0x%08x%08x\n",
                cls,
                (uint32_t)((uintptr_t)page >> 32), (uint32_t)((uintptr_t)page & 0xFFFFFFFFu),
                (uint32_t)((uintptr_t)obj >> 32), (uint32_t)((uintptr_t)obj & 0xFFFFFFFFu));

        if (cache->pages == page) {
            cache->pages = page->next;
        } else {
            slab_page_t *prev = cache->pages;
            while (prev && prev->next != page)
                prev = prev->next;
            if (prev)
                prev->next = page->next;
        }
        /* Isolate: no longer reachable for alloc or (via page_in_cache) free. */
        page->free_count = 0;
        page->freelist = NULL;
        page->next = NULL;

        page = slab_new_page(cls);
        if (!page)
            return NULL;
        page->next = cache->pages;
        cache->pages = page;
        obj = page->freelist;
    }
#endif

    page->freelist = *(void **)obj;
    page->free_count--;
    cache->allocs++;
    g_total_allocs++;

    zero_bytes(obj, slab_sizes[cls]);
    return obj;
}

void *slab_kmalloc(size_t size)
{
    int cls = slab_class_for_size(size);
    if (cls < 0)
        return NULL;

    unsigned long irq_flags = spin_lock_irqsave(&g_slab_lock);
    void *obj = slab_alloc_locked(cls);
    spin_unlock_irqrestore(&g_slab_lock, irq_flags);
    return obj;
}

int slab_kfree_if_owned(void *ptr)
{
    slab_page_t *page = NULL;
    int handled = 0;

    if (!ptr)
        return 0;

    unsigned long irq_flags = spin_lock_irqsave(&g_slab_lock);
    if (slab_owns_locked(ptr, &page)) {
        handled = 1;
        if (page->free_count >= page->total_count ||
            ptr_is_free_in_page(page, ptr)) {
            if (!g_warned_double_free) {
                g_warned_double_free = 1;
                kprintf("[slab] WARN: double free rejected ptr=0x%08x%08x cls=%u\n",
                        (uint32_t)(((uint64_t)(uintptr_t)ptr) >> 32),
                        (uint32_t)((uintptr_t)ptr & 0xFFFFFFFFu),
                        page->class_idx);
            }
        } else {
            *(void **)ptr = page->freelist;
            page->freelist = ptr;
            page->free_count++;
            g_caches[page->class_idx].frees++;
            g_total_frees++;
        }
    }
    spin_unlock_irqrestore(&g_slab_lock, irq_flags);
    return handled;
}

void slab_get_stats(slab_stats_t *out)
{
    if (!out)
        return;
    unsigned long irq_flags = spin_lock_irqsave(&g_slab_lock);
    out->allocs = g_total_allocs;
    out->frees = g_total_frees;
    out->pages = g_total_pages;
    spin_unlock_irqrestore(&g_slab_lock, irq_flags);
}

#include "heap.h"

void slab_run_selftests(void)
{
    int pass = 0;
    int fail = 0;

    kprintf("[guide09][test] start\n");

    {
        void *first = slab_kmalloc(8);
        uintptr_t page_base = (uintptr_t)first & ~(uintptr_t)(PAGE_SIZE - 1);
        const slab_page_t *pg = (const slab_page_t *)page_base;
        uint16_t total = pg->total_count;
        uintptr_t prev = (uintptr_t)first;
        int ok = (first != NULL) && (total > 400);

        for (uint16_t k = 1; ok && k < total; k++) {
            void *p = slab_kmalloc(8);
            if (!p || ((uintptr_t)p & ~(uintptr_t)(PAGE_SIZE - 1)) != page_base ||
                (uintptr_t)p != prev + 8) {
                ok = 0;
                break;
            }
            prev = (uintptr_t)p;
        }
        if (ok && pg->free_count != 0)
            ok = 0;

        for (uintptr_t p = prev; ok && p >= page_base + pg->obj_start; p -= 8) {
            if (!slab_kfree_if_owned((void *)p))
                ok = 0;
        }
        if (ok && (pg->free_count != total))
            ok = 0;

        if (ok) {
            pass++;
            kprintf("[guide09] slot walk PASS (class0 slots=%u, freelist restored)\n",
                    total);
        } else {
            fail++;
            kprintf("[guide09] slot walk FAIL\n");
        }
    }

    {
        static const size_t sizes[] = { 8, 16, 32, 64, 128, 256, 512 };
        slab_stats_t s0, s1;
        int ok = 1;

        slab_get_stats(&s0);
        for (unsigned i = 0; i < sizeof(sizes) / sizeof(sizes[0]); i++) {
            void *p = kmalloc(sizes[i]);
            if (!p) {
                ok = 0;
                break;
            }
            if (!slab_kfree_if_owned(p)) {
                ok = 0;
                kfree(p);
                break;
            }
        }
        void *big = kmalloc(513);
        if (!big) {
            ok = 0;
        } else {
            if (slab_kfree_if_owned(big)) {
                ok = 0;
            } else {
                kfree(big);
            }
        }
        slab_get_stats(&s1);
        if (ok && (s1.allocs - s0.allocs) != 7)
            ok = 0;

        if (ok) {
            pass++;
            kprintf("[guide09] size routing PASS (7 slab classes + 513->tlsf)\n");
        } else {
            fail++;
            kprintf("[guide09] size routing FAIL\n");
        }
    }

    {
        slab_stats_t s0, s1;
        void *p = slab_kmalloc(32);
        int ok = (p != NULL);

        if (ok)
            ok = slab_kfree_if_owned(p) == 1;
        slab_get_stats(&s0);
        if (ok)
            ok = slab_kfree_if_owned(p) == 1;
        slab_get_stats(&s1);
        if (ok && s1.frees != s0.frees)
            ok = 0;

        if (ok) {
            pass++;
            kprintf("[guide09] double free rejected PASS\n");
        } else {
            fail++;
            kprintf("[guide09] double free rejected FAIL\n");
        }
    }

    {
        enum { LIVE_SLOTS = 64, CHURN_OPS = 20000 };
        static void *live[LIVE_SLOTS];
        slab_stats_t s0, s1;
        uint32_t lcg = 0x12345678u;
        int ok = 1;

        slab_get_stats(&s0);
        for (int i = 0; i < CHURN_OPS && ok; i++) {
            lcg = lcg * 1664525u + 1013904223u;
            unsigned slot = (lcg >> 8) % LIVE_SLOTS;
            if (live[slot]) {
                kfree(live[slot]);
                live[slot] = NULL;
            } else {
                size_t sz = slab_sizes[(lcg >> 16) % SLAB_CLASSES];
                live[slot] = kmalloc(sz);
                if (!live[slot])
                    ok = 0;
            }
        }
        for (unsigned i = 0; i < LIVE_SLOTS; i++) {
            if (live[i]) {
                kfree(live[i]);
                live[i] = NULL;
            }
        }
        slab_get_stats(&s1);
        if (ok && (s1.allocs - s0.allocs) != (s1.frees - s0.frees))
            ok = 0;
        if (ok && s1.pages > 48)
            ok = 0;

        if (ok) {
            pass++;
            kprintf("[guide09] churn PASS (ops=%u allocs+%u pages=%u)\n",
                    (unsigned)CHURN_OPS, (unsigned)(s1.allocs - s0.allocs),
                    (unsigned)s1.pages);
        } else {
            fail++;
            kprintf("[guide09] churn FAIL (pages=%u)\n", (unsigned)s1.pages);
        }
    }

    kprintf("[guide09][test] done pass=%d fail=%d\n", pass, fail);
}
