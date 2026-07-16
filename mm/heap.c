/*
 * heap.c - Kernel heap backed by TLSF (Two-Level Segregated Fit).
 */

#include "heap.h"

#include "pmm.h"
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

    if (!g_heap || size == 0)
        return NULL;

    req_size = size + sizeof(heap_alloc_header_t);
    if (req_size < size)
        return NULL;

    spin_lock(&g_lock);
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

    spin_unlock(&g_lock);
    return ptr;
}

void kfree(void *ptr)
{
    heap_alloc_header_t *hdr;

    if (!g_heap || !ptr)
        return;

    spin_lock(&g_lock);
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
    spin_unlock(&g_lock);
}

void heap_get_stats(heap_stats_t *out)
{
    if (!out)
        return;

    spin_lock(&g_lock);
    out->pool_bytes = g_pool_bytes;
    out->allocated_bytes = g_allocated_bytes;
    out->peak_allocated_bytes = g_peak_allocated_bytes;
    spin_unlock(&g_lock);
}
