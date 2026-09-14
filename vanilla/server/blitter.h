/*
 * Project Tsukasa — High-Performance SIMD ARGB32 Blitter Header
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

#ifndef _VANILLA_BLITTER_H
#define _VANILLA_BLITTER_H

#include <stdint.h>
#include <stddef.h>
#include "../include/surface.h"

/* Solid color fill into ARGB32 buffer */
void blt_fill_rect(uint32_t *dst, uint32_t pitch_px, const vanilla_rect_t *rect, uint32_t color);

/* Fast opaque row copy using 64-bit / 128-bit moves */
void blt_copy_rect(uint32_t *dst, uint32_t dst_pitch, const uint32_t *src, uint32_t src_pitch,
                   const vanilla_rect_t *rect);

/* Sub-rectangle copy with explicit source and destination offsets */
void blt_copy_subrect(uint32_t *dst, uint32_t dst_pitch, int32_t dst_x, int32_t dst_y,
                      const uint32_t *src, uint32_t src_pitch, int32_t src_x, int32_t src_y,
                      int32_t w, int32_t h);

/* Alpha-blended blit with per-pixel and global alpha */
void blt_blend_rect(uint32_t *dst, uint32_t dst_pitch, const uint32_t *src, uint32_t src_pitch,
                    int32_t w, int32_t h, uint8_t global_alpha);

/* Alpha-blended blit with explicit source and destination offsets */
void blt_blend_subrect(uint32_t *dst, uint32_t dst_pitch, int32_t dst_x, int32_t dst_y,
                       const uint32_t *src, uint32_t src_pitch, int32_t src_x, int32_t src_y,
                       int32_t w, int32_t h, uint8_t global_alpha);

/* Ambient soft-edge drop shadow around floating windows */
void blt_drop_shadow(uint32_t *dst, uint32_t dst_pitch, uint32_t dst_w, uint32_t dst_h,
                     const vanilla_rect_t *win_rect, const vanilla_rect_t *clip,
                     int shadow_radius, uint8_t shadow_alpha);

/* Rectangle geometry helpers */
int  vanilla_rect_intersect(const vanilla_rect_t *a, const vanilla_rect_t *b, vanilla_rect_t *out);
void vanilla_rect_union(const vanilla_rect_t *a, const vanilla_rect_t *b, vanilla_rect_t *out);

#endif /* _VANILLA_BLITTER_H */
