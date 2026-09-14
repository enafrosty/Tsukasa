/*
 * Project Tsukasa — High-Performance SIMD ARGB32 Blitter Implementation
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

#include "blitter.h"
#include <string.h>

#if defined(__SSE2__)
#include <emmintrin.h>
#endif

int vanilla_rect_intersect(const vanilla_rect_t *a, const vanilla_rect_t *b, vanilla_rect_t *out)
{
    if (!a || !b || !out)
        return 0;

    int32_t x1 = a->x > b->x ? a->x : b->x;
    int32_t y1 = a->y > b->y ? a->y : b->y;
    int32_t x2 = (a->x + a->w) < (b->x + b->w) ? (a->x + a->w) : (b->x + b->w);
    int32_t y2 = (a->y + a->h) < (b->y + b->h) ? (a->y + a->h) : (b->y + b->h);

    if (x2 <= x1 || y2 <= y1) {
        out->x = 0;
        out->y = 0;
        out->w = 0;
        out->h = 0;
        return 0;
    }

    out->x = x1;
    out->y = y1;
    out->w = x2 - x1;
    out->h = y2 - y1;
    return 1;
}

void vanilla_rect_union(const vanilla_rect_t *a, const vanilla_rect_t *b, vanilla_rect_t *out)
{
    if (!out)
        return;

    if (!a || a->w <= 0 || a->h <= 0) {
        if (b && b->w > 0 && b->h > 0)
            *out = *b;
        else
            memset(out, 0, sizeof(*out));
        return;
    }

    if (!b || b->w <= 0 || b->h <= 0) {
        *out = *a;
        return;
    }

    int32_t x1 = a->x < b->x ? a->x : b->x;
    int32_t y1 = a->y < b->y ? a->y : b->y;
    int32_t x2 = (a->x + a->w) > (b->x + b->w) ? (a->x + a->w) : (b->x + b->w);
    int32_t y2 = (a->y + a->h) > (b->y + b->h) ? (a->y + a->h) : (b->y + b->h);

    out->x = x1;
    out->y = y1;
    out->w = x2 - x1;
    out->h = y2 - y1;
}

void blt_fill_rect(uint32_t *dst, uint32_t pitch_px, const vanilla_rect_t *rect, uint32_t color)
{
    if (!dst || !rect || rect->w <= 0 || rect->h <= 0)
        return;

    int32_t rx = rect->x;
    int32_t ry = rect->y;
    int32_t rw = rect->w;
    int32_t rh = rect->h;

#if defined(__SSE2__)
    __m128i vcolor = _mm_set1_epi32((int)color);
#endif
    uint64_t color64 = ((uint64_t)color << 32) | (uint64_t)color;

    for (int32_t y = 0; y < rh; y++) {
        uint32_t *row = dst + (ry + y) * pitch_px + rx;
        int32_t x = 0;

#if defined(__SSE2__)
        for (; x + 4 <= rw; x += 4)
            _mm_storeu_si128((__m128i *)(row + x), vcolor);
#endif
        for (; x + 2 <= rw; x += 2)
            *(uint64_t *)(row + x) = color64;

        for (; x < rw; x++)
            row[x] = color;
    }
}

void blt_copy_subrect(uint32_t *dst, uint32_t dst_pitch, int32_t dst_x, int32_t dst_y,
                      const uint32_t *src, uint32_t src_pitch, int32_t src_x, int32_t src_y,
                      int32_t w, int32_t h)
{
    if (!dst || !src || w <= 0 || h <= 0)
        return;

    size_t copy_bytes = (size_t)w * sizeof(uint32_t);

    for (int32_t y = 0; y < h; y++) {
        uint32_t *d = dst + (dst_y + y) * dst_pitch + dst_x;
        const uint32_t *s = src + (src_y + y) * src_pitch + src_x;
        memcpy(d, s, copy_bytes);
    }
}

void blt_copy_rect(uint32_t *dst, uint32_t dst_pitch, const uint32_t *src, uint32_t src_pitch,
                   const vanilla_rect_t *rect)
{
    if (!rect)
        return;
    blt_copy_subrect(dst, dst_pitch, rect->x, rect->y,
                     src, src_pitch, rect->x, rect->y,
                     rect->w, rect->h);
}

static inline uint32_t blend_pixel_scalar(uint32_t d, uint32_t s, uint32_t g_alpha)
{
    uint32_t sa = (s >> 24) & 0xFF;
    if (g_alpha < 255)
        sa = (sa * g_alpha + 127) / 255;

    if (sa == 0)
        return d;
    if (sa == 255)
        return s;

    uint32_t inv_a = 255 - sa;

    uint32_t sr = (s >> 16) & 0xFF;
    uint32_t sg = (s >> 8) & 0xFF;
    uint32_t sb = s & 0xFF;

    uint32_t dr = (d >> 16) & 0xFF;
    uint32_t dg = (d >> 8) & 0xFF;
    uint32_t db = d & 0xFF;

    uint32_t r = (sr * sa + dr * inv_a + 127) / 255;
    uint32_t g = (sg * sa + dg * inv_a + 127) / 255;
    uint32_t b = (sb * sa + db * inv_a + 127) / 255;
    uint32_t da = (d >> 24) & 0xFF;
    uint32_t a = sa + (da * inv_a + 127) / 255;
    if (a > 255)
        a = 255;

    return (a << 24) | (r << 16) | (g << 8) | b;
}

void blt_blend_subrect(uint32_t *dst, uint32_t dst_pitch, int32_t dst_x, int32_t dst_y,
                       const uint32_t *src, uint32_t src_pitch, int32_t src_x, int32_t src_y,
                       int32_t w, int32_t h, uint8_t global_alpha)
{
    if (!dst || !src || w <= 0 || h <= 0 || global_alpha == 0)
        return;

    for (int32_t y = 0; y < h; y++) {
        uint32_t *d_row = dst + (dst_y + y) * dst_pitch + dst_x;
        const uint32_t *s_row = src + (src_y + y) * src_pitch + src_x;
        int32_t x = 0;

#if defined(__SSE2__)
        __m128i zero = _mm_setzero_si128();
        __m128i round_const = _mm_set1_epi16(127);
        __m128i v_255 = _mm_set1_epi16(255);

        for (; x + 2 <= w; x += 2) {
            uint32_t s0 = s_row[x];
            uint32_t s1 = s_row[x + 1];

            uint32_t a0 = (s0 >> 24) & 0xFF;
            uint32_t a1 = (s1 >> 24) & 0xFF;

            if (global_alpha < 255) {
                a0 = (a0 * global_alpha + 127) / 255;
                a1 = (a1 * global_alpha + 127) / 255;
            }

            /* Fast paths for fully transparent and fully opaque pairs */
            if (a0 == 0 && a1 == 0)
                continue;

            if (a0 == 255 && a1 == 255) {
                d_row[x] = s0;
                d_row[x + 1] = s1;
                continue;
            }

            /* Mixed or semi-transparent: SSE2 2-pixel unpack and blend */
            __m128i s_vec = _mm_loadl_epi64((const __m128i *)(s_row + x));
            __m128i d_vec = _mm_loadl_epi64((const __m128i *)(d_row + x));

            __m128i s16 = _mm_unpacklo_epi8(s_vec, zero);
            __m128i d16 = _mm_unpacklo_epi8(d_vec, zero);

            __m128i a_vec = _mm_set_epi16((short)a1, (short)a1, (short)a1, (short)a1,
                                          (short)a0, (short)a0, (short)a0, (short)a0);
            __m128i inv_a = _mm_sub_epi16(v_255, a_vec);

            __m128i sa_term = _mm_mullo_epi16(s16, a_vec);
            __m128i da_term = _mm_mullo_epi16(d16, inv_a);

            __m128i sum = _mm_add_epi16(_mm_add_epi16(sa_term, da_term), round_const);
            /* Fast divide by 255: (x + 1 + (x >> 8)) >> 8 */
            __m128i sum_shifted = _mm_srli_epi16(sum, 8);
            __m128i res16 = _mm_srli_epi16(_mm_add_epi16(sum, _mm_add_epi16(sum_shifted, _mm_set1_epi16(1))), 8);

            __m128i res8 = _mm_packus_epi16(res16, zero);
            res8 = _mm_or_si128(res8, _mm_set1_epi32((int)0xFF000000U));
            _mm_storel_epi64((__m128i *)(d_row + x), res8);
        }
#endif
        for (; x < w; x++)
            d_row[x] = blend_pixel_scalar(d_row[x], s_row[x], global_alpha);
    }
}

void blt_blend_rect(uint32_t *dst, uint32_t dst_pitch, const uint32_t *src, uint32_t src_pitch,
                    int32_t w, int32_t h, uint8_t global_alpha)
{
    blt_blend_subrect(dst, dst_pitch, 0, 0, src, src_pitch, 0, 0, w, h, global_alpha);
}

static inline uint32_t isqrt(uint32_t n)
{
    uint32_t root = 0;
    uint32_t bit = 1u << 30;

    while (bit > n)
        bit >>= 2;

    while (bit != 0) {
        if (n >= root + bit) {
            n -= root + bit;
            root = (root >> 1) + bit;
        } else {
            root >>= 1;
        }
        bit >>= 2;
    }
    return root;
}

void blt_drop_shadow(uint32_t *dst, uint32_t dst_pitch, uint32_t dst_w, uint32_t dst_h,
                     const vanilla_rect_t *win_rect, const vanilla_rect_t *clip,
                     int shadow_radius, uint8_t shadow_alpha)
{
    if (!dst || !win_rect || shadow_radius <= 0 || shadow_alpha == 0)
        return;

    int32_t offset_y = shadow_radius / 3;
    int32_t min_x = win_rect->x - shadow_radius;
    int32_t min_y = win_rect->y - shadow_radius + offset_y;
    int32_t max_x = win_rect->x + win_rect->w + shadow_radius;
    int32_t max_y = win_rect->y + win_rect->h + shadow_radius + offset_y;

    if (min_x < 0) min_x = 0;
    if (min_y < 0) min_y = 0;
    if (max_x > (int32_t)dst_w) max_x = (int32_t)dst_w;
    if (max_y > (int32_t)dst_h) max_y = (int32_t)dst_h;

    if (clip) {
        int32_t clip_x2 = clip->x + clip->w;
        int32_t clip_y2 = clip->y + clip->h;
        if (min_x < clip->x) min_x = clip->x;
        if (min_y < clip->y) min_y = clip->y;
        if (max_x > clip_x2) max_x = clip_x2;
        if (max_y > clip_y2) max_y = clip_y2;
    }

    if (min_x >= max_x || min_y >= max_y)
        return;

    int32_t wx1 = win_rect->x;
    int32_t wy1 = win_rect->y;
    int32_t wx2 = win_rect->x + win_rect->w - 1;
    int32_t wy2 = win_rect->y + win_rect->h - 1;

    for (int32_t y = min_y; y < max_y; y++) {
        uint32_t *row = dst + y * dst_pitch;
        for (int32_t x = min_x; x < max_x; x++) {
            if (x >= wx1 && x <= wx2 && y >= wy1 && y <= wy2)
                continue;

            int32_t dx = 0;
            if (x < wx1)
                dx = wx1 - x;
            else if (x > wx2)
                dx = x - wx2;

            int32_t dy = 0;
            if (y < wy1)
                dy = wy1 - y;
            else if (y > wy2)
                dy = y - wy2;

            uint32_t dist = (dx > 0 && dy > 0) ? isqrt((uint32_t)(dx * dx + dy * dy))
                                               : (uint32_t)(dx > dy ? dx : dy);

            if (dist <= (uint32_t)shadow_radius) {
                uint32_t falloff = ((uint32_t)shadow_radius - dist) * (uint32_t)shadow_alpha / (uint32_t)shadow_radius;
                if (falloff > 0) {
                    uint32_t p = row[x];
                    uint32_t inv = 255 - falloff;
                    uint32_t r = (((p >> 16) & 0xFF) * inv + 127) / 255;
                    uint32_t g = (((p >> 8) & 0xFF) * inv + 127) / 255;
                    uint32_t b = ((p & 0xFF) * inv + 127) / 255;
                    row[x] = (p & 0xFF000000) | (r << 16) | (g << 8) | b;
                }
            }
        }
    }
}
