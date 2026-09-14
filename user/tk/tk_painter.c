/*
 * Project Tsukasa — tk_painter: client-side pixel painter for shm surfaces
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

#include "tk_painter.h"

#include "../../gfx/font_8x8.h"

#include <stddef.h>

static int tk_str_len(const char *s)
{
    int n = 0;
    while (s && s[n]) n++;
    return n;
}

static void put_px(tk_surface_t *s, int x, int y, uint32_t color)
{
    if (!s || !s->px) return;
    if (x < 0 || y < 0 || x >= (int)s->w || y >= (int)s->h) return;
    s->px[(uint32_t)y * s->w + (uint32_t)x] = color | 0xFF000000u;
}

void tk_paint_fill_rect(tk_surface_t *s, int x, int y, int w, int h, uint32_t color)
{
    if (!s || !s->px) return;
    for (int row = 0; row < h; row++) {
        int py = y + row;
        if (py < 0 || py >= (int)s->h) continue;
        for (int col = 0; col < w; col++) {
            int px = x + col;
            if (px < 0 || px >= (int)s->w) continue;
            s->px[(uint32_t)py * s->w + (uint32_t)px] = color | 0xFF000000u;
        }
    }
}

/* Same corner-distance shape as gfx/gui_srv.c gui_srv_draw_rounded_rect. */
static void fill_rounded(tk_surface_t *s, int x, int y, int w, int h,
                         int radius, uint32_t color)
{
    if (!s || w <= 0 || h <= 0) return;
    if (radius < 0) radius = 0;
    if (radius > w / 2) radius = w / 2;
    if (radius > h / 2) radius = h / 2;

    for (int py = y; py < y + h; py++) {
        for (int px = x; px < x + w; px++) {
            int dx = 0;
            int dy = 0;
            if (px < x + radius)
                dx = x + radius - px;
            else if (px >= x + w - radius)
                dx = px - (x + w - radius - 1);
            if (py < y + radius)
                dy = y + radius - py;
            else if (py >= y + h - radius)
                dy = py - (y + h - radius - 1);
            if (dx == 0 || dy == 0 || (dx * dx + dy * dy) <= radius * radius)
                put_px(s, px, py, color);
        }
    }
}

/* Integer Bresenham (all octants). */
void tk_paint_line(tk_surface_t *s, int x0, int y0, int x1, int y1, uint32_t color)
{
    int dx = x1 > x0 ? x1 - x0 : x0 - x1;
    int dy = y1 > y0 ? y1 - y0 : y0 - y1;
    int sx = x0 < x1 ? 1 : -1;
    int sy = y0 < y1 ? 1 : -1;
    int err = dx - dy;

    for (;;) {
        put_px(s, x0, y0, color);
        if (x0 == x1 && y0 == y1)
            break;
        {
            int e2 = 2 * err;
            if (e2 > -dy) {
                err -= dy;
                x0 += sx;
            }
            if (e2 < dx) {
                err += dx;
                y0 += sy;
            }
        }
    }
}

/* Filled ellipse by per-row span test: (px*ry)^2 + (py*rx)^2 <= (rx*ry)^2, pure integer (64-bit... */
void tk_paint_ellipse(tk_surface_t *s, int cx, int cy, int rx, int ry, uint32_t color)
{
    if (rx <= 0 || ry <= 0) return;
    for (int py = -ry; py <= ry; py++) {
        for (int px = -rx; px <= rx; px++) {
            int64_t a = (int64_t)px * ry;
            int64_t b = (int64_t)py * rx;
            int64_t r = (int64_t)rx * ry;
            if (a * a + b * b <= r * r)
                put_px(s, cx + px, cy + py, color);
        }
    }
}

void tk_paint_gradient(tk_surface_t *s, int x, int y, int w, int h,
                       const ui_gradient_t *g)
{
    if (!s || !g || w <= 0 || h <= 0) return;
    if (g->horizontal) {
        for (int col = 0; col < w; col++) {
            int pct = (w > 1) ? (col * 100) / (w - 1) : 0;
            uint32_t c = ui_gradient_sample(g, pct);
            for (int row = 0; row < h; row++)
                put_px(s, x + col, y + row, c);
        }
    } else {
        for (int row = 0; row < h; row++) {
            int pct = (h > 1) ? (row * 100) / (h - 1) : 0;
            uint32_t c = ui_gradient_sample(g, pct);
            for (int col = 0; col < w; col++)
                put_px(s, x + col, y + row, c);
        }
    }
}

/* 8x8 glyph blit — the gfx/gui_srv.c draw_char_to_buffer shape. */
static void draw_char(tk_surface_t *s, int x, int y, char c, uint32_t color)
{
    if ((unsigned char)c >= 128) return;
    for (int row = 0; row < FONT_HEIGHT; row++) {
        uint8_t bits = font_8x8[(unsigned char)c][row];
        for (int col = 0; col < FONT_WIDTH; col++) {
            if (bits & (1u << (7 - col)))
                put_px(s, x + col, y + row, color);
        }
    }
}

void tk_paint_text(tk_surface_t *s, int x, int y, const char *str, uint32_t color)
{
    if (!str) return;
    for (int i = 0; str[i]; i++)
        draw_char(s, x + i * FONT_WIDTH, y, str[i], color);
}

static void accumulate_dirty(tk_paint_target_t *t, int x, int y, int w, int h)
{
    int x1, y1;
    if (!t || w <= 0 || h <= 0) return;
    if (!t->dirty) {
        t->dirty = 1;
        t->dx = x;
        t->dy = y;
        t->dw = w;
        t->dh = h;
        return;
    }
    x1 = (t->dx + t->dw > x + w) ? t->dx + t->dw : x + w;
    y1 = (t->dy + t->dh > y + h) ? t->dy + t->dh : y + h;
    if (x < t->dx) t->dx = x;
    if (y < t->dy) t->dy = y;
    t->dw = x1 - t->dx;
    t->dh = y1 - t->dy;
}

static void cb_rect(void *ud, int x, int y, int w, int h, uint32_t color)
{
    tk_paint_target_t *t = (tk_paint_target_t *)ud;
    tk_paint_fill_rect(t->surf, x, y, w, h, color);
    accumulate_dirty(t, x, y, w, h);
}

static void cb_rounded(void *ud, int x, int y, int w, int h, int radius, uint32_t color)
{
    tk_paint_target_t *t = (tk_paint_target_t *)ud;
    fill_rounded(t->surf, x, y, w, h, radius, color);
    accumulate_dirty(t, x, y, w, h);
}

static void cb_string(void *ud, int x, int y, const char *str, uint32_t color)
{
    tk_paint_target_t *t = (tk_paint_target_t *)ud;
    tk_paint_text(t->surf, x, y, str, color);
    accumulate_dirty(t, x, y, tk_str_len(str) * FONT_WIDTH, FONT_HEIGHT);
}

static int cb_measure(void *ud, const char *str)
{
    (void)ud;
    return tk_str_len(str) * FONT_WIDTH;
}

static void cb_dirty(void *ud, int x, int y, int w, int h)
{
    accumulate_dirty((tk_paint_target_t *)ud, x, y, w, h);
}

static void cb_line(void *ud, int x0, int y0, int x1, int y1, uint32_t color)
{
    tk_paint_target_t *t = (tk_paint_target_t *)ud;
    int lx = x0 < x1 ? x0 : x1;
    int ly = y0 < y1 ? y0 : y1;
    int lw = (x0 > x1 ? x0 - x1 : x1 - x0) + 1;
    int lh = (y0 > y1 ? y0 - y1 : y1 - y0) + 1;
    tk_paint_line(t->surf, x0, y0, x1, y1, color);
    accumulate_dirty(t, lx, ly, lw, lh);
}

static void cb_ellipse(void *ud, int cx, int cy, int rx, int ry, uint32_t color)
{
    tk_paint_target_t *t = (tk_paint_target_t *)ud;
    tk_paint_ellipse(t->surf, cx, cy, rx, ry, color);
    accumulate_dirty(t, cx - rx, cy - ry, 2 * rx + 1, 2 * ry + 1);
}

static void cb_gradient(void *ud, int x, int y, int w, int h, const ui_gradient_t *g)
{
    tk_paint_target_t *t = (tk_paint_target_t *)ud;
    tk_paint_gradient(t->surf, x, y, w, h, g);
    accumulate_dirty(t, x, y, w, h);
}

void tk_paint_target_reset(tk_paint_target_t *t)
{
    if (!t) return;
    t->dirty = 0;
    t->dx = t->dy = t->dw = t->dh = 0;
}

void tk_painter_bind(widget_context_t *ctx, tk_paint_target_t *target,
                     bool light_theme)
{
    if (!ctx || !target) return;
    widget_context_init(ctx, target,
                        cb_rect, cb_rounded, cb_string, cb_measure, cb_dirty,
                        light_theme);
    widget_context_set_painter_ex(ctx, cb_line, cb_ellipse, cb_gradient);
}
