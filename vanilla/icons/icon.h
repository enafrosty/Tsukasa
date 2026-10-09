/*
 * Project Tsukasa — Scalable Vector Icon Format and Rasteriser Definitions
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

#ifndef _VANILLA_ICONS_ICON_H
#define _VANILLA_ICONS_ICON_H

#include <stdint.h>
#include <stddef.h>

#define VICO_MAGIC    0x5649434Fu  /* "VICO" in little endian */
#define VICO_VERSION  1

typedef struct {
    uint32_t magic;
    uint8_t  version;
    uint8_t  command_count;   /* number of draw commands following */
    uint8_t  _pad[2];
} __attribute__((packed)) vico_hdr_t;

typedef enum {
    VCMD_END        = 0,  /* terminates the command stream */
    VCMD_MOVE       = 1,  /* set current point: x8, y8 (coordinate in [0,255]) */
    VCMD_LINE       = 2,  /* line from current to: x8, y8 */
    VCMD_QUAD       = 3,  /* quadratic Bezier: cx8, cy8, x8, y8 */
    VCMD_CLOSE      = 4,  /* close path */
    VCMD_FILL_SOLID = 5,  /* fill accumulated path with ARGB colour: a8 r8 g8 b8 */
    VCMD_FILL_GRAD  = 6,  /* linear gradient fill: x08 y08 x18 y18 c0_argb c1_argb */
    VCMD_STROKE     = 7,  /* stroke accumulated path: width8 a8 r8 g8 b8 */
    VCMD_CIRCLE     = 8,  /* filled circle: cx8 cy8 r8 a8 r8 g8 b8 */
    VCMD_RECT       = 9,  /* filled rect: x8 y8 w8 h8 a8 r8 g8 b8 */
    VCMD_RRECT      = 10, /* rounded rect: x8 y8 w8 h8 corner8 a8 r8 g8 b8 */
} vico_cmd_t;

typedef struct {
    uint8_t  *data;       /* opaque command bytes, after the header */
    uint32_t  data_len;
    int       is_loaded;
} vico_icon_t;

/* Rasterise VICO byte stream into ARGB buffer (target_size x target_size). */
int vico_rasterize(const uint8_t *data, size_t data_len, int target_size,
                   uint32_t *out_pixels, int stride_px);

#endif /* _VANILLA_ICONS_ICON_H */
