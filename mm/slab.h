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

#ifndef TSUKASA_SLAB_H
#define TSUKASA_SLAB_H

#include <stddef.h>

/* Requests above this go to the TLSF heap (mm/heap.c routes automatically). */
#define SLAB_MAX_SIZE 512u

typedef struct slab_stats {
    size_t allocs;
    size_t frees;
    size_t pages;
} slab_stats_t;

/* Allocate from the size class covering `size` (zero-filled slot). */
void *slab_kmalloc(size_t size);

/* If `ptr` belongs to a slab page, handle it (free it — or reject it loudly when it is a... */
int slab_kfree_if_owned(void *ptr);

/* Size class index for `size`, or -1 when it exceeds SLAB_MAX_SIZE. */
int slab_class_for_size(size_t size);

void slab_get_stats(slab_stats_t *out);

/* Boot-time self-tests ([guide09] serial tags); call once from kernel_main after the heap probe. */
void slab_run_selftests(void);

#endif /* TSUKASA_SLAB_H */
