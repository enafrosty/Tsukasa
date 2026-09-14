/*
 * Project Tsukasa — Kernel heap allocator
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

#ifndef HEAP_H
#define HEAP_H

#include <stddef.h>

typedef struct heap_stats {
    size_t pool_bytes;
    size_t allocated_bytes;
    size_t peak_allocated_bytes;
    size_t slab_allocs;
    size_t slab_frees;
    size_t slab_pages;
} heap_stats_t;

void heap_init(void);

/* Allocate memory from the kernel heap. */
void *kmalloc(size_t size);

/* Free memory previously allocated with kmalloc. @param ptr Pointer returned by kmalloc (NULL is safe). */
void kfree(void *ptr);

/* Snapshot heap accounting. */
void heap_get_stats(heap_stats_t *out);

#endif /* HEAP_H */
