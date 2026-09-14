/*
 * Project Tsukasa — Kernel heap backed by TLSF (Two-Level Segregated Fit)
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

#include "heap.h"

#include "pmm.h"
#include "slab.h"
#include "tlsf.h"
#include "vmm_x64.h"
#include "../include/kprintf.h"
#include "../include/spinlock.h"

#include <stddef.h>
#include <stdint.h>

#define HEAP_INIT_PAGES 256
#define HEAP_GROW_PAGES 32
#define HEAP_ALLOC_MAGIC 0x4850414C4C4F434FULL

typedef struct heap_alloc_header {
    uint64_t magic;
    size_t size;
} heap_alloc_header_t;

static tlsf_t *g_heap = NULL;
/* Hold with IRQs off (irq-save) like the scheduler lock. */
static spinlock_t g_lock = SPINLOCK_INIT;
static size_t g_pool_bytes;
static size_t g_allocated_bytes;
static size_t g_peak_allocated_bytes;
static int g_bad_free_warned;

void heap_init(void)
{
    uintptr_t phys = pmm_alloc_pages(HEAP_INIT_PAGES);
    if (phys == 0)
        return;

    g_heap = tlsf_create((void *)vmm_phys_to_virt((uint64_t)phys),
                         HEAP_INIT_PAGES * 4096u);
    if (g_heap)
        g_pool_bytes = HEAP_INIT_PAGES * 4096u;
    else
    g_pool_bytes = 0;
    g_allocated_bytes = 0;
    g_peak_allocated_bytes = 0;
    g_bad_free_warned = 0;
}

void *kmalloc(size_t size)
{
    void *ptr;
    size_t req_size;
    unsigned long irq_flags;

    if (!g_heap || size == 0)
        return NULL;

    req_size = size + sizeof(heap_alloc_header_t);
    if (req_size < size)
        return NULL;

    if (size <= SLAB_MAX_SIZE) {
        void *sp = slab_kmalloc(size);
        if (sp)
            return sp;
    }

    irq_flags = spin_lock_irqsave(&g_lock);
    ptr = tlsf_malloc(g_heap, req_size);
    if (!ptr) {
        size_t pages = (req_size + 4095u) / 4096u;
        uintptr_t phys;
        if (pages < HEAP_GROW_PAGES)
            pages = HEAP_GROW_PAGES;
        phys = pmm_alloc_pages(pages);
        if (phys) {
            tlsf_add_pool(g_heap,
                          (void *)vmm_phys_to_virt((uint64_t)phys),
                          pages * 4096u);
            g_pool_bytes += pages * 4096u;
            ptr = tlsf_malloc(g_heap, req_size);
        }
    }

    if (ptr) {
        heap_alloc_header_t *hdr = (heap_alloc_header_t *)ptr;
        hdr->magic = HEAP_ALLOC_MAGIC;
        hdr->size = size;
        g_allocated_bytes += size;
        if (g_allocated_bytes > g_peak_allocated_bytes)
            g_peak_allocated_bytes = g_allocated_bytes;
        ptr = (void *)(hdr + 1);
    }

    spin_unlock_irqrestore(&g_lock, irq_flags);
    return ptr;
}

void kfree(void *ptr)
{
    heap_alloc_header_t *hdr;
    unsigned long irq_flags;

    if (!ptr)
        return;

    if (slab_kfree_if_owned(ptr))
        return;

    if (!g_heap)
        return;

    irq_flags = spin_lock_irqsave(&g_lock);
    hdr = ((heap_alloc_header_t *)ptr) - 1;
    if (hdr->magic == HEAP_ALLOC_MAGIC) {
        hdr->magic = 0;
        if (g_allocated_bytes >= hdr->size)
            g_allocated_bytes -= hdr->size;
        else
            g_allocated_bytes = 0;
        tlsf_free(g_heap, hdr);
    } else {
        if (!g_bad_free_warned) {
            g_bad_free_warned = 1;
            kprintf("[heap] WARN: invalid/double free rejected ptr=0x%08x%08x magic=0x%08x%08x\n",
                    (uint32_t)((uint64_t)(uintptr_t)ptr >> 32),
                    (uint32_t)((uint64_t)(uintptr_t)ptr & 0xFFFFFFFFu),
                    (uint32_t)(hdr->magic >> 32),
                    (uint32_t)(hdr->magic & 0xFFFFFFFFu));
        }
    }
    spin_unlock_irqrestore(&g_lock, irq_flags);
}

void heap_get_stats(heap_stats_t *out)
{
    unsigned long irq_flags;

    if (!out)
        return;

    irq_flags = spin_lock_irqsave(&g_lock);
    out->pool_bytes = g_pool_bytes;
    out->allocated_bytes = g_allocated_bytes;
    out->peak_allocated_bytes = g_peak_allocated_bytes;
    spin_unlock_irqrestore(&g_lock, irq_flags);

    {
        slab_stats_t ss;
        slab_get_stats(&ss);
        out->slab_allocs = ss.allocs;
        out->slab_frees = ss.frees;
        out->slab_pages = ss.pages;
    }
}
