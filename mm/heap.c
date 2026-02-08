/*
 * heap.c - Kernel heap implementation.
 * Simple first-fit allocator with PMM backend.
 */

#include "heap.h"
#include "pmm.h"
#include <stddef.h>
#include <stdint.h>

/** Block header: size (including header), LSB = 1 if allocated. */
#define BLOCK_INUSE 1u
#define BLOCK_MASK  ~1u

/** Minimum allocation unit (must fit free list next pointer). */
#define MIN_BLOCK_SIZE 8

/** Initial heap size in pages. */
#define HEAP_INIT_PAGES 4

struct block {
    uint32_t size;
    struct block *next;
};

static struct block *heap_free_list;

static void *virt_from_phys(uintptr_t phys)
{
    /* Identity-mapped for first 4 MiB. */
    return (void *)phys;
}

static void heap_add_region(uintptr_t phys, size_t size)
{
    struct block *b = (struct block *)virt_from_phys(phys);
    b->size = (uint32_t)(size & BLOCK_MASK);
    b->next = heap_free_list;
    heap_free_list = b;
}

void heap_init(void)
{
    heap_free_list = NULL;

    /* Allocate initial pages from PMM. */
    uintptr_t phys = pmm_alloc_pages(HEAP_INIT_PAGES);
    if (phys == 0)
        return;

    size_t total = HEAP_INIT_PAGES * 4096;
    heap_add_region(phys, total);
}

void *kmalloc(size_t size)
{
    if (size == 0)
        return NULL;

    /* Round up to minimum block size, add header. */
    size_t total = size + sizeof(uint32_t);
    if (total < MIN_BLOCK_SIZE)
        total = MIN_BLOCK_SIZE;
    total = (total + 3) & ~3u;

    struct block *prev = NULL;
    struct block *curr = heap_free_list;

    while (curr) {
        if ((curr->size & BLOCK_MASK) >= total) {
            size_t rest = (curr->size & BLOCK_MASK) - total;
            if (rest >= MIN_BLOCK_SIZE) {
                /* Split block. */
                struct block *rest_block = (struct block *)((char *)curr + total);
                rest_block->size = (uint32_t)(rest & BLOCK_MASK);
                rest_block->next = curr->next;
                curr->size = (uint32_t)(total | BLOCK_INUSE);
                if (prev)
                    prev->next = rest_block;
                else
                    heap_free_list = rest_block;
            } else {
                /* Use entire block. */
                curr->size |= BLOCK_INUSE;
                if (prev)
                    prev->next = curr->next;
                else
                    heap_free_list = curr->next;
            }
            return (void *)((uint32_t *)curr + 1);
        }
        prev = curr;
        curr = curr->next;
    }

    /* No suitable block - try to get more from PMM. */
    uintptr_t phys = pmm_alloc();
    if (phys == 0)
        return NULL;
    heap_add_region(phys, 4096);
    return kmalloc(size);
}

void kfree(void *ptr)
{
    if (ptr == NULL)
        return;

    struct block *b = (struct block *)((uint32_t *)ptr - 1);
    b->size &= BLOCK_MASK;
    b->next = heap_free_list;
    heap_free_list = b;

    /* Simple coalescing: try to merge with immediate next block if adjacent.
     * Full coalescing would require a size field in free blocks - we have it. */
}
