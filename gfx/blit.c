/*
 * Project Tsukasa — Bit-blit engine implementation with compositing support
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

#include "blit.h"
#include "../drv/fb.h"
#include <stddef.h>
#include <stdint.h>

static inline uint32_t *pixel_ptr(int x, int y)
{
    struct fb_info *fb = &fb_info;
    if (!fb->addr || fb->bpp != 32)
        return NULL;
    if (x < 0 || x >= (int)fb->width || y < 0 || y >= (int)fb->height)
        return NULL;
    return (uint32_t *)((char *)fb->addr + (uint32_t)y * fb->pitch + (uint32_t)x * 4u);
}

/* Clamp a value to [lo, hi]. */
static inline int clampi(int v, int lo, int hi)
{
    return v < lo ? lo : (v > hi ? hi : v);
}

/* Blend a src pixel over a dst pixel using src's alpha. */
static inline uint32_t blend32(uint32_t src, uint32_t dst)
{
    uint32_t sa = (src >> 24) & 0xFF;
    if (sa == 0)   return dst;
    if (sa == 255) return (src & 0x00FFFFFF) | 0xFF000000u;

    uint32_t ia  = 255u - sa;
    uint8_t  sr  = (src >> 16) & 0xFF;
    uint8_t  sg  = (src >>  8) & 0xFF;
    uint8_t  sb  =  src        & 0xFF;
    uint8_t  dr  = (dst >> 16) & 0xFF;
    uint8_t  dg  = (dst >>  8) & 0xFF;
    uint8_t  db  =  dst        & 0xFF;

    uint8_t or_ = (uint8_t)((sr * sa + dr * ia) >> 8);
    uint8_t og  = (uint8_t)((sg * sa + dg * ia) >> 8);
    uint8_t ob  = (uint8_t)((sb * sa + db * ia) >> 8);

    return 0xFF000000u | ((uint32_t)or_ << 16) | ((uint32_t)og << 8) | ob;
}

void fb_putpixel(int x, int y, color_t color)
{
    uint32_t *p = pixel_ptr(x, y);
    if (p) *p = color | 0xFF000000u;
}

void fb_blend_pixel(int x, int y, color_t color)
{
    uint32_t *p = pixel_ptr(x, y);
    if (p) *p = blend32(color, *p);
}

void fb_fill_rect(int x, int y, int w, int h, color_t color)
{
    struct fb_info *fb = &fb_info;
    if (!fb->addr || fb->bpp != 32 || w <= 0 || h <= 0)
        return;

    int x1 = x + w, y1 = y + h;
    if (x  < 0) x  = 0;
    if (y  < 0) y  = 0;
    if (x1 > (int)fb->width)  x1 = (int)fb->width;
    if (y1 > (int)fb->height) y1 = (int)fb->height;
    if (x >= x1 || y >= y1) return;

    uint32_t c = color | 0xFF000000u;

    for (int row = y; row < y1; row++) {
        uint32_t *p = (uint32_t *)((char *)fb->addr +
                                   (uint32_t)row * fb->pitch +
                                   (uint32_t)x   * 4u);
        for (int col = x; col < x1; col++)
            *p++ = c;
    }
}

void fb_fill_rect_alpha(int x, int y, int w, int h, color_t color)
{
    struct fb_info *fb = &fb_info;
    if (!fb->addr || fb->bpp != 32 || w <= 0 || h <= 0)
        return;

    uint32_t sa = (color >> 24) & 0xFF;
    if (sa == 255) { fb_fill_rect(x, y, w, h, color); return; }
    if (sa == 0)   return;

    int x1 = x + w, y1 = y + h;
    if (x  < 0) x  = 0;
    if (y  < 0) y  = 0;
    if (x1 > (int)fb->width)  x1 = (int)fb->width;
    if (y1 > (int)fb->height) y1 = (int)fb->height;
    if (x >= x1 || y >= y1) return;

    for (int row = y; row < y1; row++) {
        uint32_t *p = (uint32_t *)((char *)fb->addr +
                                   (uint32_t)row * fb->pitch +
                                   (uint32_t)x   * 4u);
        for (int col = x; col < x1; col++, p++)
            *p = blend32(color, *p);
    }
}

void fb_draw_hline(int x, int y, int len, color_t color)
{
    if (len > 0)
        fb_fill_rect(x, y, len, 1, color);
}

void fb_draw_vline(int x, int y, int len, color_t color)
{
    if (len > 0)
        fb_fill_rect(x, y, 1, len, color);
}

void fb_fill_gradient_v(int x, int y, int w, int h,
                        color_t top_col, color_t bot_col)
{
    if (h <= 0 || w <= 0) return;

    uint8_t tr = (top_col >> 16) & 0xFF,
            tg = (top_col >>  8) & 0xFF,
            tb =  top_col        & 0xFF;
    uint8_t br = (bot_col >> 16) & 0xFF,
            bg = (bot_col >>  8) & 0xFF,
            bb =  bot_col        & 0xFF;

    for (int row = 0; row < h; row++) {
        uint32_t t  = (h > 1) ? (uint32_t)(row * 255) / (uint32_t)(h - 1) : 0;
        uint32_t it = 255u - t;
        uint8_t  r  = (uint8_t)((tr * it + br * t) >> 8);
        uint8_t  g  = (uint8_t)((tg * it + bg * t) >> 8);
        uint8_t  b  = (uint8_t)((tb * it + bb * t) >> 8);
        fb_fill_rect(x, y + row, w, 1, rgb(r, g, b));
    }
}

void fb_draw_shadow_rect(int x, int y, int w, int h, int radius)
{
    for (int i = radius; i >= 1; i--) {
        uint8_t alpha = (uint8_t)(80u / (uint32_t)i);
        color_t shadow = rgba(0, 0, 0, alpha);
        fb_fill_rect_alpha(x + i, y + h + (radius - i),
                           w, 1, shadow);
        fb_fill_rect_alpha(x + w + (radius - i), y + i,
                           1, h, shadow);
    }
    (void)radius;
}

/* Approximate corner rounding: mask out corners pixel by pixel. */
static inline int in_rounded_rect(int px, int py,
                                   int x, int y, int w, int h, int r)
{
    if (px < x || px >= x + w || py < y || py >= y + h) return 0;

    int dx = 0, dy = 0;

    if (px < x + r)         dx = x + r - px;
    else if (px >= x + w - r) dx = px - (x + w - r - 1);

    if (py < y + r)         dy = y + r - py;
    else if (py >= y + h - r) dy = py - (y + h - r - 1);

    if (dx > 0 && dy > 0) {
        return (dx * dx + dy * dy) <= r * r;
    }
    return 1;
}

void fb_fill_rounded_rect(int x, int y, int w, int h, int r, color_t color)
{
    struct fb_info *fb = &fb_info;
    if (!fb->addr || w <= 0 || h <= 0) return;

    r = clampi(r, 0, clampi(w, 0, h) / 2);

    int x1 = x + w, y1 = y + h;
    int cx0 = clampi(x,  0, (int)fb->width);
    int cy0 = clampi(y,  0, (int)fb->height);
    int cx1 = clampi(x1, 0, (int)fb->width);
    int cy1 = clampi(y1, 0, (int)fb->height);

    uint32_t c = color | 0xFF000000u;

    for (int py = cy0; py < cy1; py++) {
        for (int px = cx0; px < cx1; px++) {
            if (in_rounded_rect(px, py, x, y, w, h, r)) {
                uint32_t *p = pixel_ptr(px, py);
                if (p) *p = c;
            }
        }
    }
}

void fb_draw_rounded_rect(int x, int y, int w, int h, int r, color_t color)
{
    fb_draw_hline(x + r, y,         w - 2 * r, color);
    fb_draw_hline(x + r, y + h - 1, w - 2 * r, color);
    fb_draw_vline(x,         y + r, h - 2 * r, color);
    fb_draw_vline(x + w - 1, y + r, h - 2 * r, color);

    int f  = 1 - r, ddF_x = 1, ddF_y = -2 * r;
    int cx = 0, cy = r;
    int ox1 = x + r,         oy1 = y + r;
    int ox2 = x + w - 1 - r;
    int ox3 = x + r,         oy3 = y + h - 1 - r;
    int ox4 = x + w - 1 - r, oy4 = y + h - 1 - r;

    while (cx <= cy) {
        fb_putpixel(ox2 + cy, oy1 - cx, color);
        fb_putpixel(ox2 + cx, oy1 - cy, color);
        fb_putpixel(ox1 - cx, oy1 - cy, color);
        fb_putpixel(ox1 - cy, oy1 - cx, color);
        fb_putpixel(ox3 - cy, oy3 + cx, color);
        fb_putpixel(ox3 - cx, oy3 + cy, color);
        fb_putpixel(ox4 + cx, oy4 + cy, color);
        fb_putpixel(ox4 + cy, oy4 + cx, color);

        if (f >= 0) { cy--; ddF_y += 2; f += ddF_y; }
        cx++; ddF_x += 2; f += ddF_x;
    }
}

void fb_fill_circle(int cx, int cy, int radius, color_t color)
{
    if (radius <= 0) return;
    for (int y = -radius; y <= radius; y++) {
        int xspan = 0;
        for (int xx = 0; xx <= radius; xx++) {
            if (xx * xx + y * y <= radius * radius)
                xspan = xx;
        }
        fb_fill_rect(cx - xspan, cy + y, 2 * xspan + 1, 1, color);
    }
}

void fb_blit(const void *src, void *dst, int w, int h, int pitch)
{
    if (!src || !dst || w <= 0 || h <= 0 || pitch <= 0) return;
    const char *s = (const char *)src;
    char *d       = (char *)dst;
    int row_bytes = w * 4;
    if (row_bytes > pitch) row_bytes = pitch;
    for (int i = 0; i < h; i++) {
        const uint32_t *sp = (const uint32_t *)s;
        uint32_t       *dp = (uint32_t *)d;
        for (int j = 0; j < row_bytes / 4; j++)
            dp[j] = sp[j];
        s += pitch;
        d += pitch;
    }
}

void fb_blit_alpha(int x, int y, const uint32_t *pixels, int w, int h)
{
    struct fb_info *fb = &fb_info;
    if (!fb->addr || !pixels || w <= 0 || h <= 0) return;
    for (int row = 0; row < h; row++) {
        for (int col = 0; col < w; col++) {
            uint32_t *p = pixel_ptr(x + col, y + row);
            if (p) *p = blend32(pixels[row * w + col], *p);
        }
    }
}
