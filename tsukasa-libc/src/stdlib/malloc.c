/*
 * Project Tsukasa — Dynamic Memory Allocator Implementation
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

#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <stdint.h>

#define ALIGNMENT 16
#define ALIGN(size) (((size) + (ALIGNMENT - 1)) & ~(ALIGNMENT - 1))
#define CHUNK_HDR_SIZE (sizeof(chunk_t))

#define CHUNK_USED 0x1
#define CHUNK_SIZE(c) ((c)->size_and_flags & ~0xFUL)
#define CHUNK_IS_USED(c) ((c)->size_and_flags & CHUNK_USED)

typedef struct chunk {
    size_t size_and_flags;
    struct chunk *prev;
    struct chunk *next;
    size_t _pad;
} chunk_t;

static chunk_t *g_free_list = NULL;

static void insert_free_chunk(chunk_t *c)
{
    chunk_t *prev = NULL;
    chunk_t *curr = g_free_list;

    while (curr && curr < c) {
        prev = curr;
        curr = curr->next;
    }

    c->next = curr;
    c->prev = prev;
    if (prev)
        prev->next = c;
    else
        g_free_list = c;

    if (curr)
        curr->prev = c;

    /* Coalesce with forward adjacent block if contiguous */
    if (curr && (char *)c + CHUNK_SIZE(c) == (char *)curr) {
        c->size_and_flags += CHUNK_SIZE(curr);
        c->next = curr->next;
        if (curr->next)
            curr->next->prev = c;
    }

    /* Coalesce with backward adjacent block if contiguous */
    if (prev && (char *)prev + CHUNK_SIZE(prev) == (char *)c) {
        prev->size_and_flags += CHUNK_SIZE(c);
        prev->next = c->next;
        if (c->next)
            c->next->prev = prev;
    }
}

void *malloc(size_t size)
{
    if (size == 0)
        return NULL;

    size_t needed = ALIGN(size + CHUNK_HDR_SIZE);
    if (needed < 48)
        needed = 48;

    /* Search free list for first-fit block */
    chunk_t *curr = g_free_list;
    while (curr && CHUNK_SIZE(curr) < needed)
        curr = curr->next;

    if (!curr) {
        /* Request new page block from kernel via mmap */
        size_t alloc_size = (needed < 65536) ? 65536 : ((needed + 4095) & ~4095UL);
        void *p = mmap(NULL, alloc_size, PROT_READ | PROT_WRITE,
                       MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
        if (p == MAP_FAILED)
            return NULL;

        curr = (chunk_t *)p;
        curr->prev = NULL;
        curr->next = NULL;
        curr->_pad = 0;

        if (alloc_size >= needed + CHUNK_HDR_SIZE + 16) {
            chunk_t *rem = (chunk_t *)((char *)curr + needed);
            rem->size_and_flags = alloc_size - needed;
            rem->prev = NULL;
            rem->next = NULL;
            rem->_pad = 0;
            insert_free_chunk(rem);
            curr->size_and_flags = needed | CHUNK_USED;
        } else {
            curr->size_and_flags = alloc_size | CHUNK_USED;
        }

        return (void *)((char *)curr + CHUNK_HDR_SIZE);
    }

    /* Remove or split chunk */
    if (CHUNK_SIZE(curr) >= needed + CHUNK_HDR_SIZE + 16) {
        size_t rem_size = CHUNK_SIZE(curr) - needed;
        chunk_t *rem = (chunk_t *)((char *)curr + needed);
        rem->size_and_flags = rem_size;
        rem->prev = curr->prev;
        rem->next = curr->next;
        rem->_pad = 0;

        if (rem->prev)
            rem->prev->next = rem;
        else
            g_free_list = rem;

        if (rem->next)
            rem->next->prev = rem;

        curr->size_and_flags = needed | CHUNK_USED;
    } else {
        if (curr->prev)
            curr->prev->next = curr->next;
        else
            g_free_list = curr->next;

        if (curr->next)
            curr->next->prev = curr->prev;

        curr->size_and_flags |= CHUNK_USED;
    }

    curr->prev = NULL;
    curr->next = NULL;
    return (void *)((char *)curr + CHUNK_HDR_SIZE);
}

void free(void *ptr)
{
    if (!ptr)
        return;

    chunk_t *c = (chunk_t *)((char *)ptr - CHUNK_HDR_SIZE);
    if (!CHUNK_IS_USED(c))
        return;

    c->size_and_flags &= ~CHUNK_USED;
    insert_free_chunk(c);
}

void *calloc(size_t nmemb, size_t size)
{
    if (nmemb != 0 && size > (size_t)-1 / nmemb)
        return NULL;

    size_t total = nmemb * size;
    void *ptr = malloc(total);
    if (ptr)
        memset(ptr, 0, total);
    return ptr;
}

void *realloc(void *ptr, size_t size)
{
    if (!ptr)
        return malloc(size);
    if (size == 0) {
        free(ptr);
        return NULL;
    }

    chunk_t *c = (chunk_t *)((char *)ptr - CHUNK_HDR_SIZE);
    size_t current_payload = CHUNK_SIZE(c) - CHUNK_HDR_SIZE;

    if (current_payload >= size)
        return ptr;

    void *new_ptr = malloc(size);
    if (!new_ptr)
        return NULL;

    memcpy(new_ptr, ptr, current_payload < size ? current_payload : size);
    free(ptr);
    return new_ptr;
}
