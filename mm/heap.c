/*
 * heap.c - Kernel heap backed by TLSF (Two-Level Segregated Fit).
 * Provides O(1) kmalloc/kfree, spinlock-protected for SMP safety.
 * Public API: heap_init(), kmalloc(), kfree()  — unchanged.
 */

#include "heap.h"
#include "pmm.h"
#include "tlsf.h"
#include "vmm_x64.h"
#include "../include/spinlock.h"
#include <stddef.h>
#include <stdint.h>

/** Initial heap size in pages (64 KiB). */
#define HEAP_INIT_PAGES 16

/** Minimum slab to add when the heap is exhausted (32 KiB). */
#define HEAP_GROW_PAGES 8

static tlsf_t    *g_heap  = NULL;
static spinlock_t g_lock  = SPINLOCK_INIT;

void heap_init(void)
{
    /* Allocate initial pages from PMM. */
    uintptr_t phys = pmm_alloc_pages(HEAP_INIT_PAGES);
    if (phys == 0)
        return;

    g_heap = tlsf_create((void *)vmm_phys_to_virt((uint64_t)phys),
                         HEAP_INIT_PAGES * 4096u);
}

void *kmalloc(size_t size)
{
    if (!g_heap || size == 0)
        return NULL;

    spin_lock(&g_lock);
    void *ptr = tlsf_malloc(g_heap, size);
    if (!ptr) {
        /* Heap exhausted — grow by adding a new PMM slab. */
        size_t pages = (size + 4095u) / 4096u;
        if (pages < HEAP_GROW_PAGES) pages = HEAP_GROW_PAGES;
        uintptr_t phys = pmm_alloc_pages(pages);
        if (phys) {
            tlsf_add_pool(g_heap,
                          (void *)vmm_phys_to_virt((uint64_t)phys),
                          pages * 4096u);
            ptr = tlsf_malloc(g_heap, size);
        }
    }
    spin_unlock(&g_lock);
    return ptr;
}

void kfree(void *ptr)
{
    if (!g_heap || !ptr)
        return;
    spin_lock(&g_lock);
    tlsf_free(g_heap, ptr);
    spin_unlock(&g_lock);
}
