/*
 * Project Tsukasa — Shared Memory ARGB32 Surface Abstraction
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

#ifndef _VANILLA_SURFACE_H
#define _VANILLA_SURFACE_H

#include <stdint.h>
#include <stddef.h>

typedef struct {
    int32_t x;
    int32_t y;
    int32_t w;
    int32_t h;
} vanilla_rect_t;

typedef struct {
    int       shm_id;
    uint32_t  width;
    uint32_t  height;
    uint32_t  pitch;
    size_t    size;
    uint32_t *pixels;
} vanilla_surface_t;

/* Server-side SHM surface allocation and attachment */
int surface_create_shm(vanilla_surface_t *surf, uint32_t width, uint32_t height);

/* Client-side SHM surface attachment */
int surface_attach_shm(vanilla_surface_t *surf, int shm_id, uint32_t width,
                       uint32_t height, uint32_t pitch, size_t size);

/* Client-side SHM detachment without destruction */
void surface_detach(vanilla_surface_t *surf);

/* Server-side SHM detachment and region deallocation */
void surface_destroy(vanilla_surface_t *surf);

/* Helper for constructing ARGB32 pixel values */
static inline uint32_t vanilla_make_argb(uint8_t a, uint8_t r, uint8_t g, uint8_t b)
{
    return ((uint32_t)a << 24) | ((uint32_t)r << 16) | ((uint32_t)g << 8) | (uint32_t)b;
}

#endif /* _VANILLA_SURFACE_H */
