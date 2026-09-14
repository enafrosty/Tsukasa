/*
 * Project Tsukasa — Shared Memory Surface Allocation and Management
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

#include "../include/surface.h"
#include "../include/protocol.h"
#include <sys/shm.h>
#include <string.h>

int surface_create_shm(vanilla_surface_t *surf, uint32_t width, uint32_t height)
{
    uint32_t pitch;
    size_t size;
    int shm_id;
    void *pixels;

    if (!surf || width == 0 || height == 0)
        return -1;

    if (width > VANILLA_MAX_WIDTH || height > VANILLA_MAX_HEIGHT)
        return -1;

    pitch = width * (uint32_t)sizeof(uint32_t);
    size = (size_t)pitch * height;

    shm_id = shm_create(size);
    if (shm_id < 0)
        return -1;

    pixels = shm_attach(shm_id);
    if (!pixels) {
        shm_destroy(shm_id);
        return -1;
    }

    memset(pixels, 0, size);

    surf->shm_id = shm_id;
    surf->width = width;
    surf->height = height;
    surf->pitch = pitch;
    surf->size = size;
    surf->pixels = (uint32_t *)pixels;

    return 0;
}

int surface_attach_shm(vanilla_surface_t *surf, int shm_id, uint32_t width,
                       uint32_t height, uint32_t pitch, size_t size)
{
    void *pixels;

    if (!surf || shm_id <= 0 || width == 0 || height == 0 || size == 0)
        return -1;

    if (width > VANILLA_MAX_WIDTH || height > VANILLA_MAX_HEIGHT)
        return -1;

    pixels = shm_attach(shm_id);
    if (!pixels)
        return -1;

    surf->shm_id = shm_id;
    surf->width = width;
    surf->height = height;
    surf->pitch = pitch;
    surf->size = size;
    surf->pixels = (uint32_t *)pixels;

    return 0;
}

void surface_detach(vanilla_surface_t *surf)
{
    if (!surf)
        return;

    if (surf->pixels) {
        shm_detach(surf->pixels);
        surf->pixels = NULL;
    }
    surf->shm_id = -1;
    surf->size = 0;
}

void surface_destroy(vanilla_surface_t *surf)
{
    if (!surf)
        return;

    if (surf->pixels) {
        shm_detach(surf->pixels);
        surf->pixels = NULL;
    }
    if (surf->shm_id > 0) {
        shm_destroy(surf->shm_id);
        surf->shm_id = -1;
    }
    surf->size = 0;
}
