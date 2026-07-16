/*
 * heap.h - Kernel heap allocator.
 * Provides kmalloc/kfree for kernel data structures.
 */

#ifndef HEAP_H
#define HEAP_H

#include <stddef.h>

typedef struct heap_stats {
    size_t pool_bytes;
    size_t allocated_bytes;
    size_t peak_allocated_bytes;
} heap_stats_t;

/**
 * Initialize the kernel heap. Must be called after pmm_init and paging_init.
 */
void heap_init(void);

/**
 * Allocate memory from the kernel heap.
 *
 * @param size Number of bytes to allocate.
 * @return Pointer to allocated memory, or NULL on failure.
 */
void *kmalloc(size_t size);

/**
 * Free memory previously allocated with kmalloc.
 *
 * @param ptr Pointer returned by kmalloc (NULL is safe).
 */
void kfree(void *ptr);

/**
 * Snapshot heap accounting.
 */
void heap_get_stats(heap_stats_t *out);

#endif /* HEAP_H */
