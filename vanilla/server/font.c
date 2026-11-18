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

#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wunused-function"
#pragma clang diagnostic ignored "-Wunused-parameter"
#pragma clang diagnostic ignored "-Wsign-compare"
#elif defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-function"
#pragma GCC diagnostic ignored "-Wunused-parameter"
#pragma GCC diagnostic ignored "-Wsign-compare"
#endif

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

#if defined(__clang__)
#pragma clang diagnostic pop
#elif defined(__GNUC__)
#pragma GCC diagnostic pop
#endif

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

static uint32_t font_utf8_next(const char *s, size_t max_bytes, size_t *pos)
{
    size_t i = *pos;
    if (i >= max_bytes || s[i] == '\0')
        return 0;

    uint8_t b0 = (uint8_t)s[i];
    if (b0 < 0x80) {
        *pos = i + 1;
        return (uint32_t)b0;
    }

    if ((b0 & 0xE0) == 0xC0 && i + 1 < max_bytes &&
        ((uint8_t)s[i + 1] & 0xC0) == 0x80) {
        uint32_t cp = ((uint32_t)(b0 & 0x1F) << 6) |
                      (uint32_t)((uint8_t)s[i + 1] & 0x3F);
        if (cp >= 0x80) {
            *pos = i + 2;
            return cp;
        }
    } else if ((b0 & 0xF0) == 0xE0 && i + 2 < max_bytes &&
               ((uint8_t)s[i + 1] & 0xC0) == 0x80 &&
               ((uint8_t)s[i + 2] & 0xC0) == 0x80) {
        uint32_t cp = ((uint32_t)(b0 & 0x0F) << 12) |
                      ((uint32_t)((uint8_t)s[i + 1] & 0x3F) << 6) |
                      (uint32_t)((uint8_t)s[i + 2] & 0x3F);
        if (cp >= 0x800) {
            *pos = i + 3;
            return cp;
        }
    } else if ((b0 & 0xF8) == 0xF0 && i + 3 < max_bytes &&
               ((uint8_t)s[i + 1] & 0xC0) == 0x80 &&
               ((uint8_t)s[i + 2] & 0xC0) == 0x80 &&
               ((uint8_t)s[i + 3] & 0xC0) == 0x80) {
        uint32_t cp = ((uint32_t)(b0 & 0x07) << 18) |
                      ((uint32_t)((uint8_t)s[i + 1] & 0x3F) << 12) |
                      ((uint32_t)((uint8_t)s[i + 2] & 0x3F) << 6) |
                      (uint32_t)((uint8_t)s[i + 3] & 0x3F);
        if (cp >= 0x10000 && cp <= 0x10FFFF) {
            *pos = i + 4;
            return cp;
        }
    }

    *pos = i + 1;
    return (uint32_t)b0;
}

static uint32_t font_utf8_peek(const char *s, size_t max_bytes, size_t pos)
{
    while (pos < max_bytes && s[pos] == '\r')
        pos++;
    if (pos >= max_bytes || s[pos] == '\0')
        return 0;
    size_t tmp = pos;
    return font_utf8_next(s, max_bytes, &tmp);
}

static int32_t font_line_gap_px(const vanilla_font_t *font, float scale, float size_px)
{
    if (font->line_gap > 0) {
        int32_t gap = (int32_t)(font->line_gap * scale + 0.5f);
        if (gap > 0)
            return gap;
    }
    return size_px >= 12.0f ? 2 : 1;
}

static int32_t font_weight_extra_adv(const vanilla_glyph_t *g, vanilla_font_weight_t weight)
{
    if (weight == FONT_WEIGHT_BOLD && g && g->width > 0)
        return 1;
    return 0;
}

static int32_t font_kern_px(stbtt_fontinfo *info, float scale, uint32_t cp, uint32_t next_cp)
{
    if (next_cp == 0 || next_cp == '\n' || next_cp == '\r')
        return 0;
    int kern = stbtt_GetCodepointKernAdvance(info, (int)cp, (int)next_cp);
    if (kern == 0)
        return 0;
    float fk = (float)kern * scale;
    return (fk >= 0.0f) ? (int32_t)(fk + 0.5f) : (int32_t)(fk - 0.5f);
}

static void font_blend_glyph(uint32_t *dst, uint32_t pitch_px, const vanilla_rect_t *clip,
                             const vanilla_glyph_t *g, int32_t base_x, int32_t baseline_y,
                             vanilla_font_weight_t weight, uint32_t color)
{
    if (!dst || !g || !g->bitmap || g->width <= 0 || g->height <= 0)
        return;

    uint32_t text_a = (color >> 24) & 0xFF;
    if (text_a == 0)
        return;

    uint32_t text_r = (color >> 16) & 0xFF;
    uint32_t text_g = (color >> 8) & 0xFF;
    uint32_t text_b = color & 0xFF;

    int32_t gx = base_x + g->xoff;
    int32_t gy = baseline_y + g->yoff;
    int32_t eff_w = (weight == FONT_WEIGHT_MEDIUM || weight == FONT_WEIGHT_BOLD)
                    ? (g->width + 1) : g->width;

    for (int32_t r = 0; r < g->height; r++) {
        int32_t py = gy + r;
        if (py < 0)
            continue;
        if (clip && (py < clip->y || py >= clip->y + clip->h))
            continue;

        for (int32_t c = 0; c < eff_w; c++) {
            int32_t px = gx + c;
            if (px < 0 || (uint32_t)px >= pitch_px)
                continue;
            if (clip && (px < clip->x || px >= clip->x + clip->w))
                continue;

            uint8_t cur = (c < g->width) ? g->bitmap[r * g->width + c] : 0;
            uint8_t prev = (c > 0 && (c - 1) < g->width) ? g->bitmap[r * g->width + (c - 1)] : 0;
            uint8_t mask_val = cur;

            if (weight == FONT_WEIGHT_MEDIUM) {
                uint32_t m = (uint32_t)cur + (((uint32_t)prev * 96u + 127u) / 255u);
                mask_val = (uint8_t)(m > 255u ? 255u : m);
            } else if (weight == FONT_WEIGHT_BOLD) {
                uint32_t m = (uint32_t)cur + (((uint32_t)prev * 216u + 127u) / 255u);
                mask_val = (uint8_t)(m > 255u ? 255u : m);
            }

            if (mask_val == 0)
                continue;

            uint32_t sa = (mask_val * text_a + 127) / 255;
            if (sa == 0)
                continue;

            uint32_t *d_ptr = dst + (size_t)py * pitch_px + px;
            if (sa == 255) {
                *d_ptr = (0xFFu << 24) | (text_r << 16) | (text_g << 8) | text_b;
            } else {
                uint32_t d = *d_ptr;
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

int32_t font_line_height(vanilla_font_t *font, float size_px)
{
    if (!font || !font->info || size_px <= 0.0f)
        return 0;

    stbtt_fontinfo *info = (stbtt_fontinfo *)font->info;
    float scale = stbtt_ScaleForPixelHeight(info, size_px);
    int32_t h = (int32_t)((font->ascent - font->descent) * scale + 0.5f);
    if (h <= 0)
        h = (int32_t)(size_px + 0.5f);
    return h > 0 ? h : 1;
}

int32_t font_glyph_advance(vanilla_font_t *font, uint32_t codepoint,
                           uint32_t next_codepoint, float size_px,
                           vanilla_font_weight_t weight)
{
    if (!font || !font->info || size_px <= 0.0f)
        return 0;

    const vanilla_glyph_t *g = font_get_glyph(font, codepoint, size_px);
    if (!g)
        return 0;

    stbtt_fontinfo *info = (stbtt_fontinfo *)font->info;
    float scale = stbtt_ScaleForPixelHeight(info, size_px);
    int32_t adv = g->xadvance + font_weight_extra_adv(g, weight);
    if (next_codepoint != 0)
        adv += font_kern_px(info, scale, codepoint, next_codepoint);
    return adv;
}

void font_draw_glyph(uint32_t *dst, uint32_t pitch_px, const vanilla_rect_t *clip,
                     vanilla_font_t *font, uint32_t codepoint,
                     int32_t x, int32_t y, float size_px,
                     vanilla_font_weight_t weight, uint32_t color)
{
    if (!dst || !font || !font->info || size_px <= 0.0f)
        return;

    const vanilla_glyph_t *g = font_get_glyph(font, codepoint, size_px);
    if (!g)
        return;

    stbtt_fontinfo *info = (stbtt_fontinfo *)font->info;
    float scale = stbtt_ScaleForPixelHeight(info, size_px);
    int32_t baseline_y = y + (int32_t)(font->ascent * scale + 0.5f);

    font_blend_glyph(dst, pitch_px, clip, g, x, baseline_y, weight, color);
}

void font_measure_text_n(vanilla_font_t *font, const char *text, size_t max_bytes,
                         float size_px, vanilla_font_weight_t weight,
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
    int32_t line_h = font_line_height(font, size_px);
    int32_t gap = font_line_gap_px(font, scale, size_px);

    if (max_bytes == 0 || text[0] == '\0') {
        if (out_h)
            *out_h = line_h;
        return;
    }

    int32_t max_w = 0;
    int32_t cur_w = 0;
    int32_t lines = 1;
    size_t pos = 0;

    while (pos < max_bytes && text[pos] != '\0') {
        uint32_t cp = font_utf8_next(text, max_bytes, &pos);
        if (cp == '\r')
            continue;
        if (cp == '\n') {
            if (cur_w > max_w)
                max_w = cur_w;
            cur_w = 0;
            lines++;
            continue;
        }

        const vanilla_glyph_t *g = font_get_glyph(font, cp, size_px);
        if (g) {
            cur_w += g->xadvance + font_weight_extra_adv(g, weight);
            uint32_t next_cp = font_utf8_peek(text, max_bytes, pos);
            cur_w += font_kern_px(info, scale, cp, next_cp);
        }
    }

    if (cur_w > max_w)
        max_w = cur_w;

    int32_t total_h = lines * line_h + (lines > 1 ? (lines - 1) * gap : 0);
    if (out_w)
        *out_w = max_w;
    if (out_h)
        *out_h = total_h;
}

void font_measure_text(vanilla_font_t *font, const char *text, float size_px,
                       int32_t *out_w, int32_t *out_h)
{
    font_measure_text_n(font, text, (size_t)-1, size_px, FONT_WEIGHT_REGULAR, out_w, out_h);
}

void font_draw_text_n(uint32_t *dst, uint32_t pitch_px, const vanilla_rect_t *clip,
                      vanilla_font_t *font, const char *text, size_t max_bytes,
                      int32_t x, int32_t y, float size_px,
                      vanilla_font_weight_t weight, uint32_t color)
{
    if (!dst || !font || !font->info || !text || max_bytes == 0 || size_px <= 0.0f)
        return;

    uint32_t text_a = (color >> 24) & 0xFF;
    if (text_a == 0)
        return;

    stbtt_fontinfo *info = (stbtt_fontinfo *)font->info;
    float scale = stbtt_ScaleForPixelHeight(info, size_px);
    int32_t ascent_px = (int32_t)(font->ascent * scale + 0.5f);
    int32_t line_h = font_line_height(font, size_px);
    int32_t line_step = line_h + font_line_gap_px(font, scale, size_px);

    int32_t cur_x = x;
    int32_t cur_y = y;
    size_t pos = 0;

    while (pos < max_bytes && text[pos] != '\0') {
        uint32_t cp = font_utf8_next(text, max_bytes, &pos);
        if (cp == '\r')
            continue;
        if (cp == '\n') {
            cur_x = x;
            cur_y += line_step;
            continue;
        }

        const vanilla_glyph_t *g = font_get_glyph(font, cp, size_px);
        if (!g)
            continue;

        font_blend_glyph(dst, pitch_px, clip, g, cur_x, cur_y + ascent_px, weight, color);

        cur_x += g->xadvance + font_weight_extra_adv(g, weight);
        uint32_t next_cp = font_utf8_peek(text, max_bytes, pos);
        cur_x += font_kern_px(info, scale, cp, next_cp);
    }
}

void font_draw_text_ex(uint32_t *dst, uint32_t pitch_px, const vanilla_rect_t *clip,
                       vanilla_font_t *font, const char *text, int32_t x, int32_t y,
                       float size_px, vanilla_font_weight_t weight, uint32_t color)
{
    font_draw_text_n(dst, pitch_px, clip, font, text, (size_t)-1,
                     x, y, size_px, weight, color);
}

void font_draw_text(uint32_t *dst, uint32_t pitch_px, const vanilla_rect_t *clip,
                    vanilla_font_t *font, const char *text, int32_t x, int32_t y,
                    float size_px, uint32_t color)
{
    font_draw_text_ex(dst, pitch_px, clip, font, text, x, y, size_px,
                      FONT_WEIGHT_REGULAR, color);
}
