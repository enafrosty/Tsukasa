/*
 * Project Tsukasa — libvanilla Icon Loading and Rendering API Definitions
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

#ifndef _VANILLA_LIBVANILLA_ICON_H
#define _VANILLA_LIBVANILLA_ICON_H

#include <stdint.h>
#include <stddef.h>
#include "../include/surface.h"
#include "../icons/icon.h"

typedef struct {
    /* BMP fallback */
    uint32_t   *pixels;     /* ARGB, bmp_w × bmp_h */
    int         bmp_w;
    int         bmp_h;
    int         is_bmp;
    vico_icon_t vico;
} icon_source_t;

typedef struct {
    icon_source_t source;
    char          name[32];
} icon_t;

/*
 * icon_load: Load a named icon.
 *   Searches for <name>.vco (VICO format) in the icon search path first,
 *   then <name>.bmp as a fallback. Returns NULL if neither is found.
 *   The returned icon_t* is owned by the caller; call icon_free() when done.
 *
 * Icon search path: "/icons/", "/fat12/ICONS/", then the assets directory.
 * FAT12 8.3 note: file names are probed in lowercase then uppercase variants.
 */
icon_t *icon_load(const char *name);

/*
 * icon_render: Rasterise icon into a caller-supplied ARGB buffer.
 *   out_pixels: ARGB pixels, row-major, stride = target_size × 4 bytes.
 *   target_size: the pixel dimension (square: target_size × target_size).
 *   Returns 0 on success, negative errno on failure.
 */
int icon_render(const icon_t *icon, int target_size,
                uint32_t *out_pixels, int stride_px);

/*
 * icon_render_to_surface: Convenience wrapper that blits the icon
 *   into an app_common vanilla_surface_t at position (x, y).
 */
int icon_render_to_surface(const icon_t *icon, vanilla_surface_t *surf,
                           int32_t x, int32_t y, int target_size);

/*
 * icon_free: Release resources associated with an icon.
 */
void icon_free(icon_t *icon);

#endif /* _VANILLA_LIBVANILLA_ICON_H */
