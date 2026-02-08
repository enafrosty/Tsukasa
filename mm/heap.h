/*
 * heap.h - Kernel heap allocator.
 * Provides kmalloc/kfree for kernel data structures.
 */

#ifndef HEAP_H
#define HEAP_H

#include <stddef.h>

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

#endif /* HEAP_H */
