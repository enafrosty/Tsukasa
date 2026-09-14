/*
 * Project Tsukasa — Scalable Vector Typography Engine Implementation
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

#include "font.h"
#include "blitter.h"

#include <stdlib.h>
#include <string.h>
#include <math.h>

#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wunused-function"
#pragma clang diagnostic ignored "-Wunused-parameter"
#pragma clang diagnostic ignored "-Wsign-compare"

#define STB_TRUETYPE_IMPLEMENTATION
#define STBTT_STATIC
#define STBTT_malloc(x,u)  ((void)(u),malloc(x))
#define STBTT_free(x,u)    ((void)(u),free(x))
#define STBTT_assert(x)    ((void)0)
#define STBTT_strlen(x)    strlen(x)
#define STBTT_memcpy       memcpy
#define STBTT_memset       memset
#define STBTT_cos(x)       cos(x)
#define STBTT_acos(x)      acos(x)
#define STBTT_fabs(x)      fabs(x)
#define STBTT_sqrt(x)      sqrt(x)
#define STBTT_pow(x,y)     pow(x,y)
#define STBTT_fmod(x,y)    fmod(x,y)
#define STBTT_floor(x)     floor(x)
#define STBTT_ceil(x)      ceil(x)

#include "../include/stb_truetype.h"

#pragma clang diagnostic pop

int font_init(vanilla_font_t *font, const uint8_t *ttf_data, size_t ttf_size)
{
    if (!font)
        return -1;

    memset(font, 0, sizeof(*font));

    if (!ttf_data || ttf_size == 0) {
        font->data = default_font_data;
        font->size = default_font_size;
        font->is_owned = 0;
    } else {
        font->data = ttf_data;
        font->size = ttf_size;
        font->is_owned = 0;
    }

    stbtt_fontinfo *info = (stbtt_fontinfo *)malloc(sizeof(stbtt_fontinfo));
    if (!info)
        return -1;

    if (!stbtt_InitFont(info, font->data, 0)) {
        free(info);
        return -1;
    }

    font->info = info;
    stbtt_GetFontVMetrics(info, &font->ascent, &font->descent, &font->line_gap);
    return 0;
}

void font_destroy(vanilla_font_t *font)
{
    if (!font)
        return;

    for (int i = 0; i < font->glyph_count; i++) {
        if (font->glyph_cache[i].bitmap) {
            free(font->glyph_cache[i].bitmap);
            font->glyph_cache[i].bitmap = NULL;
        }
    }
    font->glyph_count = 0;

    if (font->info) {
        free(font->info);
        font->info = NULL;
    }

    if (font->is_owned && font->data) {
        free((void *)font->data);
        font->data = NULL;
    }
}

const vanilla_glyph_t *font_get_glyph(vanilla_font_t *font, uint32_t codepoint, float size_px)
{
    if (!font || !font->info || size_px <= 0.0f)
        return NULL;

    /* Check LRU cache */
    for (int i = 0; i < font->glyph_count; i++) {
        if (font->glyph_cache[i].codepoint == codepoint &&
            fabs(font->glyph_cache[i].pixel_height - size_px) < 0.01f) {
            font->glyph_cache[i].last_used_tick = ++font->current_tick;
            return &font->glyph_cache[i];
        }
    }

    /* Cache miss: rasterize glyph */
    stbtt_fontinfo *info = (stbtt_fontinfo *)font->info;
    float scale = stbtt_ScaleForPixelHeight(info, size_px);

    int advance = 0, lsb = 0;
    stbtt_GetCodepointHMetrics(info, (int)codepoint, &advance, &lsb);

    int width = 0, height = 0, xoff = 0, yoff = 0;
    uint8_t *bmp = stbtt_GetCodepointBitmap(info, 0.0f, scale, (int)codepoint,
                                            &width, &height, &xoff, &yoff);

    int slot = 0;
    if (font->glyph_count < MAX_CACHED_GLYPHS) {
        slot = font->glyph_count++;
    } else {
        /* Evict least-recently used slot */
        uint32_t min_tick = font->glyph_cache[0].last_used_tick;
        slot = 0;
        for (int i = 1; i < font->glyph_count; i++) {
            if (font->glyph_cache[i].last_used_tick < min_tick) {
                min_tick = font->glyph_cache[i].last_used_tick;
                slot = i;
            }
        }
        if (font->glyph_cache[slot].bitmap)
            free(font->glyph_cache[slot].bitmap);
    }

    font->glyph_cache[slot].codepoint = codepoint;
    font->glyph_cache[slot].pixel_height = size_px;
    font->glyph_cache[slot].bitmap = bmp;
    font->glyph_cache[slot].width = width;
    font->glyph_cache[slot].height = height;
    font->glyph_cache[slot].xoff = xoff;
    font->glyph_cache[slot].yoff = yoff;
    font->glyph_cache[slot].xadvance = (int32_t)(advance * scale + 0.5f);
    font->glyph_cache[slot].last_used_tick = ++font->current_tick;

    return &font->glyph_cache[slot];
}

void font_measure_text(vanilla_font_t *font, const char *text, float size_px,
                       int32_t *out_w, int32_t *out_h)
{
    if (out_w)
        *out_w = 0;
    if (out_h)
        *out_h = 0;

    if (!font || !font->info || !text || size_px <= 0.0f)
        return;

    stbtt_fontinfo *info = (stbtt_fontinfo *)font->info;
    float scale = stbtt_ScaleForPixelHeight(info, size_px);

    int32_t total_w = 0;
    size_t len = strlen(text);

    for (size_t i = 0; i < len; i++) {
        const vanilla_glyph_t *g = font_get_glyph(font, (uint8_t)text[i], size_px);
        if (g)
            total_w += g->xadvance;

        if (i + 1 < len) {
            int kern = stbtt_GetCodepointKernAdvance(info, (int)text[i], (int)text[i + 1]);
            total_w += (int32_t)(kern * scale + 0.5f);
        }
    }

    int32_t h = (int32_t)((font->ascent - font->descent) * scale + 0.5f);
    if (out_w)
        *out_w = total_w;
    if (out_h)
        *out_h = h > 0 ? h : (int32_t)size_px;
}

void font_draw_text(uint32_t *dst, uint32_t pitch_px, const vanilla_rect_t *clip,
                    vanilla_font_t *font, const char *text, int32_t x, int32_t y,
                    float size_px, uint32_t color)
{
    if (!dst || !font || !font->info || !text || size_px <= 0.0f)
        return;

    uint32_t text_a = (color >> 24) & 0xFF;
    if (text_a == 0)
        return;

    uint32_t text_r = (color >> 16) & 0xFF;
    uint32_t text_g = (color >> 8) & 0xFF;
    uint32_t text_b = color & 0xFF;

    stbtt_fontinfo *info = (stbtt_fontinfo *)font->info;
    float scale = stbtt_ScaleForPixelHeight(info, size_px);
    int32_t baseline_y = y + (int32_t)(font->ascent * scale + 0.5f);

    int32_t cur_x = x;
    size_t len = strlen(text);

    for (size_t i = 0; i < len; i++) {
        const vanilla_glyph_t *g = font_get_glyph(font, (uint8_t)text[i], size_px);
        if (!g)
            continue;

        if (g->bitmap && g->width > 0 && g->height > 0) {
            int32_t gx = cur_x + g->xoff;
            int32_t gy = baseline_y + g->yoff;

            for (int32_t r = 0; r < g->height; r++) {
                int32_t py = gy + r;
                if (py < 0)
                    continue;
                if (clip && (py < clip->y || py >= clip->y + clip->h))
                    continue;

                for (int32_t c = 0; c < g->width; c++) {
                    int32_t px = gx + c;
                    if (px < 0 || (uint32_t)px >= pitch_px)
                        continue;
                    if (clip && (px < clip->x || px >= clip->x + clip->w))
                        continue;

                    uint8_t mask_val = g->bitmap[r * g->width + c];
                    if (mask_val == 0)
                        continue;

                    uint32_t sa = (mask_val * text_a + 127) / 255;
                    uint32_t *d_ptr = dst + py * pitch_px + px;
                    uint32_t d = *d_ptr;

                    if (sa == 255) {
                        *d_ptr = (0xFF << 24) | (text_r << 16) | (text_g << 8) | text_b;
                    } else {
                        uint32_t inv_a = 255 - sa;
                        uint32_t dr = (d >> 16) & 0xFF;
                        uint32_t dg = (d >> 8) & 0xFF;
                        uint32_t db = d & 0xFF;
                        uint32_t da = (d >> 24) & 0xFF;

                        uint32_t out_r = (text_r * sa + dr * inv_a + 127) / 255;
                        uint32_t out_g = (text_g * sa + dg * inv_a + 127) / 255;
                        uint32_t out_b = (text_b * sa + db * inv_a + 127) / 255;
                        uint32_t out_a = sa + (da * inv_a + 127) / 255;
                        if (out_a > 255)
                            out_a = 255;

                        *d_ptr = (out_a << 24) | (out_r << 16) | (out_g << 8) | out_b;
                    }
                }
            }
        }

        cur_x += g->xadvance;
        if (i + 1 < len) {
            int kern = stbtt_GetCodepointKernAdvance(info, (int)text[i], (int)text[i + 1]);
            cur_x += (int32_t)(kern * scale + 0.5f);
        }
    }
}
