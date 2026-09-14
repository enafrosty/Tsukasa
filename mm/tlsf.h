/*
 * Project Tsukasa — Two-Level Segregated Fit memory allocator
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

#ifndef TLSF_H
#define TLSF_H

#include <stddef.h>
#include <stdint.h>

typedef struct tlsf_s tlsf_t;

/* Initialise a TLSF heap control structure inside `mem`. */
tlsf_t *tlsf_create(void *mem, size_t size);

/* Add an additional memory region to an existing TLSF heap. Used to grow the heap from the PMM on demand. */
void tlsf_add_pool(tlsf_t *t, void *mem, size_t size);

void *tlsf_malloc(tlsf_t *t, size_t size);

void *tlsf_calloc(tlsf_t *t, size_t nmemb, size_t size);

void tlsf_free(tlsf_t *t, void *ptr);

/* Reallocate a block. */
void *tlsf_realloc(tlsf_t *t, void *ptr, size_t size);

#endif /* TLSF_H */
