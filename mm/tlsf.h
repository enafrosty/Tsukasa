/*
 * tlsf.h - Two-Level Segregated Fit memory allocator.
 * Self-contained, freestanding, O(1) alloc/free.
 *
 * Usage:
 *   static uint8_t pool[POOL_SIZE];
 *   tlsf_t *h = tlsf_create(pool, POOL_SIZE);
 *   void *p = tlsf_malloc(h, 128);
 *   tlsf_free(h, p);
 *   tlsf_add_pool(h, more_mem, more_size);  // grow on demand
 */

#ifndef TLSF_H
#define TLSF_H

#include <stddef.h>
#include <stdint.h>

typedef struct tlsf_s tlsf_t;

/**
 * Initialise a TLSF heap control structure inside `mem`.
 * `size` must be >= 256 bytes.
 * Returns a pointer to the heap handle (placed at start of mem), or NULL.
 */
tlsf_t *tlsf_create(void *mem, size_t size);

/**
 * Add an additional memory region to an existing TLSF heap.
 * Used to grow the heap from the PMM on demand.
 */
void tlsf_add_pool(tlsf_t *t, void *mem, size_t size);

/** Allocate `size` bytes.  Returns NULL on failure. */
void *tlsf_malloc(tlsf_t *t, size_t size);

/** Allocate and zero `size` bytes. */
void *tlsf_calloc(tlsf_t *t, size_t nmemb, size_t size);

/** Free a previously allocated block (NULL safe). */
void tlsf_free(tlsf_t *t, void *ptr);

/** Reallocate a block. */
void *tlsf_realloc(tlsf_t *t, void *ptr, size_t size);

#endif /* TLSF_H */
