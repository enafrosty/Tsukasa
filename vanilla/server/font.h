/*
 * Project Tsukasa — Scalable Vector Typography Engine Header
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

#ifndef _VANILLA_FONT_H
#define _VANILLA_FONT_H

#include <stdint.h>
#include <stddef.h>
#include "../include/surface.h"

#define MAX_CACHED_GLYPHS 256

typedef struct {
    uint32_t codepoint;
    float    pixel_height;
    uint8_t *bitmap;
    int32_t  width;
    int32_t  height;
    int32_t  xoff;
    int32_t  yoff;
    int32_t  xadvance;
    uint32_t last_used_tick;
} vanilla_glyph_t;

typedef struct {
    const uint8_t   *data;
    size_t           size;
    int              is_owned;
    void            *info;
    vanilla_glyph_t  glyph_cache[MAX_CACHED_GLYPHS];
    int              glyph_count;
    uint32_t         current_tick;
    int              ascent;
    int              descent;
    int              line_gap;
} vanilla_font_t;

extern const uint8_t default_font_data[];
extern const size_t  default_font_size;

/* Font lifecycle */
int  font_init(vanilla_font_t *font, const uint8_t *ttf_data, size_t ttf_size);
void font_destroy(vanilla_font_t *font);

/* Glyph cache lookup and rasterization */
const vanilla_glyph_t *font_get_glyph(vanilla_font_t *font, uint32_t codepoint, float size_px);

/* Typography drawing and metrics */
void font_draw_text(uint32_t *dst, uint32_t pitch_px, const vanilla_rect_t *clip,
                    vanilla_font_t *font, const char *text, int32_t x, int32_t y,
                    float size_px, uint32_t color);

void font_measure_text(vanilla_font_t *font, const char *text, float size_px,
                       int32_t *out_w, int32_t *out_h);

#endif /* _VANILLA_FONT_H */
