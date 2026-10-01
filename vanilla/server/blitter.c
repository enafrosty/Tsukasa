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
#include <stdlib.h>
#include <math.h>

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

            if (a0 == 0 && a1 == 0)
                continue;

            if (a0 == 255 && a1 == 255) {
                d_row[x] = s0;
                d_row[x + 1] = s1;
                continue;
            }

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

static inline void blt_blend_solid_row(uint32_t *dst, uint32_t color, int32_t w)
{
    uint32_t a = (color >> 24) & 0xFF;
    if (a == 0 || w <= 0)
        return;
    if (a == 255) {
        for (int32_t x = 0; x < w; x++)
            dst[x] = color;
        return;
    }
    uint32_t inv_a = 255 - a;
    uint32_t sr = (color >> 16) & 0xFF;
    uint32_t sg = (color >> 8) & 0xFF;
    uint32_t sb = color & 0xFF;
    uint32_t sa_term_r = sr * a + 127;
    uint32_t sa_term_g = sg * a + 127;
    uint32_t sa_term_b = sb * a + 127;

#if defined(__SSE2__)
    __m128i zero = _mm_setzero_si128();
    __m128i inv_a_vec = _mm_set1_epi16((short)inv_a);
    __m128i round_const = _mm_set1_epi16(127);
    __m128i a_vec = _mm_set1_epi16((short)a);
    __m128i s_val = _mm_set1_epi32((int)color);
    __m128i s16 = _mm_unpacklo_epi8(s_val, zero);
    __m128i sa_term = _mm_mullo_epi16(s16, a_vec);

    int32_t x = 0;
    for (; x + 4 <= w; x += 4) {
        __m128i d_vec = _mm_loadu_si128((const __m128i *)(dst + x));
        __m128i d_lo = _mm_unpacklo_epi8(d_vec, zero);
        __m128i d_hi = _mm_unpackhi_epi8(d_vec, zero);

        __m128i d_term_lo = _mm_mullo_epi16(d_lo, inv_a_vec);
        __m128i d_term_hi = _mm_mullo_epi16(d_hi, inv_a_vec);

        __m128i sum_lo = _mm_add_epi16(_mm_add_epi16(sa_term, d_term_lo), round_const);
        __m128i sum_hi = _mm_add_epi16(_mm_add_epi16(sa_term, d_term_hi), round_const);

        __m128i s_lo = _mm_srli_epi16(sum_lo, 8);
        __m128i s_hi = _mm_srli_epi16(sum_hi, 8);

        __m128i res_lo = _mm_srli_epi16(_mm_add_epi16(sum_lo, _mm_add_epi16(s_lo, _mm_set1_epi16(1))), 8);
        __m128i res_hi = _mm_srli_epi16(_mm_add_epi16(sum_hi, _mm_add_epi16(s_hi, _mm_set1_epi16(1))), 8);

        __m128i res8 = _mm_packus_epi16(res_lo, res_hi);
        res8 = _mm_or_si128(res8, _mm_set1_epi32((int)0xFF000000U));
        _mm_storeu_si128((__m128i *)(dst + x), res8);
    }
    for (; x < w; x++) {
        uint32_t d = dst[x];
        uint32_t dr = (d >> 16) & 0xFF;
        uint32_t dg = (d >> 8) & 0xFF;
        uint32_t db = d & 0xFF;
        uint32_t r = (sa_term_r + dr * inv_a) / 255;
        uint32_t g = (sa_term_g + dg * inv_a) / 255;
        uint32_t b = (sa_term_b + db * inv_a) / 255;
        dst[x] = 0xFF000000 | (r << 16) | (g << 8) | b;
    }
#else
    for (int32_t x = 0; x < w; x++) {
        uint32_t d = dst[x];
        uint32_t dr = (d >> 16) & 0xFF;
        uint32_t dg = (d >> 8) & 0xFF;
        uint32_t db = d & 0xFF;
        uint32_t r = (sa_term_r + dr * inv_a) / 255;
        uint32_t g = (sa_term_g + dg * inv_a) / 255;
        uint32_t b = (sa_term_b + db * inv_a) / 255;
        dst[x] = 0xFF000000 | (r << 16) | (g << 8) | b;
    }
#endif
}

void blt_rounded_rect_clipped(uint32_t *dst, uint32_t dst_pitch,
                              int32_t x, int32_t y, int32_t w, int32_t h,
                              int32_t radius, uint32_t color,
                              uint32_t corner_mask, const vanilla_rect_t *clip)
{
    if (!dst || w <= 0 || h <= 0)
        return;

    uint32_t base_alpha = (color >> 24) & 0xFF;
    if (base_alpha == 0)
        return;

    int32_t max_r = (w < h ? w : h) / 2;
    if (radius > max_r)
        radius = max_r;

    int32_t rx1 = x;
    int32_t ry1 = y;
    int32_t rx2 = x + w;
    int32_t ry2 = y + h;

    if (clip) {
        if (rx1 < clip->x) rx1 = clip->x;
        if (ry1 < clip->y) ry1 = clip->y;
        if (rx2 > clip->x + clip->w) rx2 = clip->x + clip->w;
        if (ry2 > clip->y + clip->h) ry2 = clip->y + clip->h;
    }

    if (rx2 <= rx1 || ry2 <= ry1)
        return;

    if (radius <= 0 || (corner_mask & BLT_CORNER_ALL) == 0) {
        if (base_alpha == 255) {
            vanilla_rect_t fill_r = { rx1, ry1, rx2 - rx1, ry2 - ry1 };
            blt_fill_rect(dst, dst_pitch, &fill_r, color);
        } else {
            for (int32_t py = ry1; py < ry2; py++)
                blt_blend_solid_row(dst + py * dst_pitch + rx1, color, rx2 - rx1);
        }
        return;
    }

    float r = (float)radius;
    float inner_r = r - 0.5f;
    float outer_r = r + 0.5f;
    float inner_r2 = inner_r > 0.0f ? inner_r * inner_r : 0.0f;
    float outer_r2 = outer_r * outer_r;

    float cx_left  = (float)(x + radius);
    float cx_right = (float)(x + w - radius);
    float cy_top   = (float)(y + radius);
    float cy_bot   = (float)(y + h - radius);

    uint32_t rgb_only = color & 0x00FFFFFF;

    int32_t top_band_end = y + radius;
    int32_t bot_band_start = y + h - radius;

    int32_t mid_y1 = top_band_end > ry1 ? top_band_end : ry1;
    int32_t mid_y2 = bot_band_start < ry2 ? bot_band_start : ry2;

    if (mid_y2 > mid_y1) {
        if (base_alpha == 255) {
            vanilla_rect_t mid_r = { rx1, mid_y1, rx2 - rx1, mid_y2 - mid_y1 };
            blt_fill_rect(dst, dst_pitch, &mid_r, color);
        } else {
            for (int32_t py = mid_y1; py < mid_y2; py++)
                blt_blend_solid_row(dst + py * dst_pitch + rx1, color, rx2 - rx1);
        }
    }

    for (int32_t py = ry1; py < ry2; py++) {
        if (py >= mid_y1 && py < mid_y2)
            continue;

        uint32_t *row = dst + py * dst_pitch;
        int is_top = (py < top_band_end);

        int32_t left_limit = x + radius;
        int32_t right_start = x + w - radius;

        int32_t seg1_end = left_limit < rx2 ? left_limit : rx2;
        int32_t seg2_start = left_limit > rx1 ? left_limit : rx1;
        int32_t seg2_end = right_start < rx2 ? right_start : rx2;
        int32_t seg3_start = right_start > rx1 ? right_start : rx1;

        /* Left quadrant */
        if (seg1_end > rx1) {
            uint32_t cflag = is_top ? BLT_CORNER_TL : BLT_CORNER_BL;
            if (corner_mask & cflag) {
                float cy = is_top ? cy_top : cy_bot;
                for (int32_t px = rx1; px < seg1_end; px++) {
                    float dx = cx_left - ((float)px + 0.5f);
                    float dy = is_top ? (cy - ((float)py + 0.5f)) : (((float)py + 0.5f) - cy);
                    float dist2 = dx * dx + dy * dy;
                    if (dist2 >= outer_r2)
                        continue;
                    if (dist2 <= inner_r2) {
                        if (base_alpha == 255)
                            row[px] = color;
                        else
                            row[px] = blend_pixel_scalar(row[px], color, 255);
                    } else {
                        float dist = sqrtf(dist2);
                        float coverage = outer_r - dist;
                        if (coverage <= 0.0f)
                            continue;
                        if (coverage > 1.0f)
                            coverage = 1.0f;
                        uint8_t a = (uint8_t)(coverage * (float)base_alpha + 0.5f);
                        if (a == 0)
                            continue;
                        row[px] = blend_pixel_scalar(row[px], ((uint32_t)a << 24) | rgb_only, 255);
                    }
                }
            } else {
                if (base_alpha == 255) {
                    for (int32_t px = rx1; px < seg1_end; px++)
                        row[px] = color;
                } else {
                    blt_blend_solid_row(row + rx1, color, seg1_end - rx1);
                }
            }
        }

        /* Middle interior span */
        if (seg2_end > seg2_start) {
            if (base_alpha == 255) {
                for (int32_t px = seg2_start; px < seg2_end; px++)
                    row[px] = color;
            } else {
                blt_blend_solid_row(row + seg2_start, color, seg2_end - seg2_start);
            }
        }

        /* Right quadrant */
        if (rx2 > seg3_start) {
            uint32_t cflag = is_top ? BLT_CORNER_TR : BLT_CORNER_BR;
            if (corner_mask & cflag) {
                float cy = is_top ? cy_top : cy_bot;
                for (int32_t px = seg3_start; px < rx2; px++) {
                    float dx = ((float)px + 0.5f) - cx_right;
                    float dy = is_top ? (cy - ((float)py + 0.5f)) : (((float)py + 0.5f) - cy);
                    float dist2 = dx * dx + dy * dy;
                    if (dist2 >= outer_r2)
                        continue;
                    if (dist2 <= inner_r2) {
                        if (base_alpha == 255)
                            row[px] = color;
                        else
                            row[px] = blend_pixel_scalar(row[px], color, 255);
                    } else {
                        float dist = sqrtf(dist2);
                        float coverage = outer_r - dist;
                        if (coverage <= 0.0f)
                            continue;
                        if (coverage > 1.0f)
                            coverage = 1.0f;
                        uint8_t a = (uint8_t)(coverage * (float)base_alpha + 0.5f);
                        if (a == 0)
                            continue;
                        row[px] = blend_pixel_scalar(row[px], ((uint32_t)a << 24) | rgb_only, 255);
                    }
                }
            } else {
                if (base_alpha == 255) {
                    for (int32_t px = seg3_start; px < rx2; px++)
                        row[px] = color;
                } else {
                    blt_blend_solid_row(row + seg3_start, color, rx2 - seg3_start);
                }
            }
        }
    }
}

void blt_rounded_rect(uint32_t *dst, uint32_t dst_pitch,
                      int32_t x, int32_t y, int32_t w, int32_t h,
                      int32_t radius, uint32_t color)
{
    blt_rounded_rect_clipped(dst, dst_pitch, x, y, w, h, radius, color, BLT_CORNER_ALL, NULL);
}

struct vanilla_shadow_tex_t {
    int32_t  radius;
    uint8_t  alpha;
    uint32_t last_used;
    uint32_t *pixels;
    uint32_t *edge_alpha;
    uint32_t *edge_left;
    uint32_t *edge_right;
};

#define SHADOW_CACHE_MAX_ENTRIES 4

static vanilla_shadow_tex_t g_shadow_cache[SHADOW_CACHE_MAX_ENTRIES];
static int g_shadow_cache_count = 0;
static uint32_t g_shadow_cache_clock = 0;

static void shadow_tex_compute(vanilla_shadow_tex_t *tex, int32_t r, uint8_t alpha)
{
    int32_t sz = 2 * r;
    for (int32_t y = 0; y < sz; y++) {
        for (int32_t x = 0; x < sz; x++) {
            float dx = (float)x - (float)r + 0.5f;
            float dy = (float)y - (float)r + 0.5f;
            float dist = sqrtf(dx * dx + dy * dy);
            float falloff = 1.0f - (dist / (float)r);
            if (falloff < 0.0f)
                falloff = 0.0f;
            float g = falloff * falloff * (3.0f - 2.0f * falloff);
            uint8_t a = (uint8_t)(g * (float)alpha + 0.5f);
            tex->pixels[y * sz + x] = (uint32_t)a << 24;
        }
    }
    for (int32_t k = 0; k < r; k++) {
        float dist = (float)(r - 1 - k) + 0.5f;
        float falloff = 1.0f - (dist / (float)r);
        if (falloff < 0.0f)
            falloff = 0.0f;
        float g = falloff * falloff * (3.0f - 2.0f * falloff);
        uint8_t a = (uint8_t)(g * (float)alpha + 0.5f);
        uint32_t val = (uint32_t)a << 24;
        tex->edge_alpha[k] = val;
        tex->edge_left[k] = val;
        tex->edge_right[r - 1 - k] = val;
    }
}

const vanilla_shadow_tex_t *vanilla_shadow_tex_get(int32_t radius, uint8_t alpha)
{
    if (radius <= 0 || alpha == 0)
        return NULL;

    for (int i = 0; i < g_shadow_cache_count; i++) {
        if (g_shadow_cache[i].radius == radius && g_shadow_cache[i].alpha == alpha) {
            g_shadow_cache[i].last_used = ++g_shadow_cache_clock;
            return &g_shadow_cache[i];
        }
    }

    int idx = -1;
    if (g_shadow_cache_count < SHADOW_CACHE_MAX_ENTRIES) {
        idx = g_shadow_cache_count++;
    } else {
        uint32_t oldest = 0xFFFFFFFF;
        idx = 0;
        for (int i = 0; i < SHADOW_CACHE_MAX_ENTRIES; i++) {
            if (g_shadow_cache[i].last_used < oldest) {
                oldest = g_shadow_cache[i].last_used;
                idx = i;
            }
        }
        if (g_shadow_cache[idx].pixels) {
            free(g_shadow_cache[idx].pixels);
            g_shadow_cache[idx].pixels = NULL;
        }
        if (g_shadow_cache[idx].edge_alpha) {
            free(g_shadow_cache[idx].edge_alpha);
            g_shadow_cache[idx].edge_alpha = NULL;
        }
        if (g_shadow_cache[idx].edge_left) {
            free(g_shadow_cache[idx].edge_left);
            g_shadow_cache[idx].edge_left = NULL;
        }
        if (g_shadow_cache[idx].edge_right) {
            free(g_shadow_cache[idx].edge_right);
            g_shadow_cache[idx].edge_right = NULL;
        }
    }

    int32_t sz = 2 * radius;
    uint32_t *pix = (uint32_t *)malloc((size_t)sz * (size_t)sz * sizeof(uint32_t));
    uint32_t *edge = (uint32_t *)malloc((size_t)radius * sizeof(uint32_t));
    uint32_t *e_l = (uint32_t *)malloc((size_t)radius * sizeof(uint32_t));
    uint32_t *e_r = (uint32_t *)malloc((size_t)radius * sizeof(uint32_t));

    if (!pix || !edge || !e_l || !e_r) {
        if (pix) free(pix);
        if (edge) free(edge);
        if (e_l) free(e_l);
        if (e_r) free(e_r);
        return NULL;
    }

    vanilla_shadow_tex_t *tex = &g_shadow_cache[idx];
    tex->radius = radius;
    tex->alpha = alpha;
    tex->last_used = ++g_shadow_cache_clock;
    tex->pixels = pix;
    tex->edge_alpha = edge;
    tex->edge_left = e_l;
    tex->edge_right = e_r;

    shadow_tex_compute(tex, radius, alpha);

    return tex;
}

void vanilla_shadow_cache_clear(void)
{
    for (int i = 0; i < g_shadow_cache_count; i++) {
        if (g_shadow_cache[i].pixels) {
            free(g_shadow_cache[i].pixels);
            g_shadow_cache[i].pixels = NULL;
        }
        if (g_shadow_cache[i].edge_alpha) {
            free(g_shadow_cache[i].edge_alpha);
            g_shadow_cache[i].edge_alpha = NULL;
        }
        if (g_shadow_cache[i].edge_left) {
            free(g_shadow_cache[i].edge_left);
            g_shadow_cache[i].edge_left = NULL;
        }
        if (g_shadow_cache[i].edge_right) {
            free(g_shadow_cache[i].edge_right);
            g_shadow_cache[i].edge_right = NULL;
        }
        g_shadow_cache[i].radius = 0;
        g_shadow_cache[i].alpha = 0;
        g_shadow_cache[i].last_used = 0;
    }
    g_shadow_cache_count = 0;
    g_shadow_cache_clock = 0;
}

static inline void blit_shadow_patch(uint32_t *dst, uint32_t dst_pitch,
                                     int32_t dest_x, int32_t dest_y, int32_t dest_w, int32_t dest_h,
                                     const uint32_t *src_pixels, uint32_t src_pitch,
                                     int32_t src_x, int32_t src_y,
                                     int32_t cx1, int32_t cy1, int32_t cx2, int32_t cy2)
{
    if (dest_w <= 0 || dest_h <= 0)
        return;
    int32_t x1 = dest_x > cx1 ? dest_x : cx1;
    int32_t y1 = dest_y > cy1 ? dest_y : cy1;
    int32_t x2 = (dest_x + dest_w) < cx2 ? (dest_x + dest_w) : cx2;
    int32_t y2 = (dest_y + dest_h) < cy2 ? (dest_y + dest_h) : cy2;
    if (x2 <= x1 || y2 <= y1)
        return;
    int32_t off_x = x1 - dest_x;
    int32_t off_y = y1 - dest_y;
    blt_blend_subrect(dst, dst_pitch, x1, y1,
                      src_pixels, src_pitch,
                      src_x + off_x, src_y + off_y,
                      x2 - x1, y2 - y1, 255);
}

void blt_draw_shadow_cached(uint32_t *dst, uint32_t dst_pitch,
                            uint32_t dst_w, uint32_t dst_h,
                            const vanilla_rect_t *win_rect,
                            const vanilla_rect_t *clip,
                            int32_t shadow_radius, uint8_t shadow_alpha)
{
    if (!dst || !win_rect || win_rect->w <= 0 || win_rect->h <= 0 || shadow_radius <= 0 || shadow_alpha == 0)
        return;

    const vanilla_shadow_tex_t *tex = vanilla_shadow_tex_get(shadow_radius, shadow_alpha);
    if (!tex)
        return;

    int32_t r = shadow_radius;
    int32_t sz = 2 * r;
    int32_t wx = win_rect->x;
    int32_t wy = win_rect->y;
    int32_t ww = win_rect->w;
    int32_t wh = win_rect->h;

    int32_t cx1 = 0;
    int32_t cy1 = 0;
    int32_t cx2 = (int32_t)dst_w;
    int32_t cy2 = (int32_t)dst_h;

    if (clip) {
        if (cx1 < clip->x) cx1 = clip->x;
        if (cy1 < clip->y) cy1 = clip->y;
        if (cx2 > clip->x + clip->w) cx2 = clip->x + clip->w;
        if (cy2 > clip->y + clip->h) cy2 = clip->y + clip->h;
    }

    if (cx2 <= cx1 || cy2 <= cy1)
        return;

    /* 4 Corner Patches */
    blit_shadow_patch(dst, dst_pitch, wx - r, wy - r, r, r,
                      tex->pixels, sz, 0, 0, cx1, cy1, cx2, cy2);
    blit_shadow_patch(dst, dst_pitch, wx + ww, wy - r, r, r,
                      tex->pixels, sz, r, 0, cx1, cy1, cx2, cy2);
    blit_shadow_patch(dst, dst_pitch, wx - r, wy + wh, r, r,
                      tex->pixels, sz, 0, r, cx1, cy1, cx2, cy2);
    blit_shadow_patch(dst, dst_pitch, wx + ww, wy + wh, r, r,
                      tex->pixels, sz, r, r, cx1, cy1, cx2, cy2);

    /* Left and Right Edges */
    blit_shadow_patch(dst, dst_pitch, wx - r, wy, r, wh,
                      tex->edge_left, 0, 0, 0, cx1, cy1, cx2, cy2);
    blit_shadow_patch(dst, dst_pitch, wx + ww, wy, r, wh,
                      tex->edge_right, 0, 0, 0, cx1, cy1, cx2, cy2);

    /* Top and Bottom Edges */
    int32_t top_x1 = wx > cx1 ? wx : cx1;
    int32_t top_x2 = (wx + ww) < cx2 ? (wx + ww) : cx2;
    int32_t top_y1 = (wy - r) > cy1 ? (wy - r) : cy1;
    int32_t top_y2 = wy < cy2 ? wy : cy2;

    if (top_x2 > top_x1 && top_y2 > top_y1) {
        for (int32_t y = top_y1; y < top_y2; y++) {
            int32_t ty = y - (wy - r);
            if (ty >= 0 && ty < r) {
                uint32_t color = tex->edge_alpha[ty];
                blt_blend_solid_row(dst + y * dst_pitch + top_x1, color, top_x2 - top_x1);
            }
        }
    }

    int32_t bot_x1 = wx > cx1 ? wx : cx1;
    int32_t bot_x2 = (wx + ww) < cx2 ? (wx + ww) : cx2;
    int32_t bot_y1 = (wy + wh) > cy1 ? (wy + wh) : cy1;
    int32_t bot_y2 = (wy + wh + r) < cy2 ? (wy + wh + r) : cy2;

    if (bot_x2 > bot_x1 && bot_y2 > bot_y1) {
        for (int32_t y = bot_y1; y < bot_y2; y++) {
            int32_t ty = y - (wy + wh);
            if (ty >= 0 && ty < r) {
                uint32_t color = tex->edge_alpha[r - 1 - ty];
                blt_blend_solid_row(dst + y * dst_pitch + bot_x1, color, bot_x2 - bot_x1);
            }
        }
    }
}

void blt_drop_shadow(uint32_t *dst, uint32_t dst_pitch, uint32_t dst_w, uint32_t dst_h,
                     const vanilla_rect_t *win_rect, const vanilla_rect_t *clip,
                     int shadow_radius, uint8_t shadow_alpha)
{
    blt_draw_shadow_cached(dst, dst_pitch, dst_w, dst_h, win_rect, clip, shadow_radius, shadow_alpha);
}
