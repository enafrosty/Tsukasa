/*
 * Project Tsukasa — Project Vanilla Client Common UI Toolkit Implementation
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

#include "app_common.h"
#include <string.h>

static vanilla_font_t g_app_font;
static int g_app_font_state = 0; /* 0 = uninitialized, 1 = ready, -1 = error */

vanilla_font_t *app_get_font(void)
{
    if (g_app_font_state == 0) {
        if (font_init(&g_app_font, NULL, 0) == 0)
            g_app_font_state = 1;
        else
            g_app_font_state = -1;
    }
    return (g_app_font_state == 1) ? &g_app_font : NULL;
}

float app_default_font_size(void)
{
    if (g_theme && g_theme->title_font_size > 0.0f)
        return g_theme->title_font_size;
    return 13.0f;
}

static inline uint32_t app_surface_pitch_px(const vanilla_surface_t *surf)
{
    if (!surf)
        return 0;
    if (surf->pitch >= surf->width * (uint32_t)sizeof(uint32_t))
        return surf->pitch / (uint32_t)sizeof(uint32_t);
    return surf->width;
}

static vanilla_font_weight_t app_clamp_weight(app_font_weight_t weight)
{
    if (weight == APP_FONT_MEDIUM)
        return FONT_WEIGHT_MEDIUM;
    if (weight == APP_FONT_BOLD)
        return FONT_WEIGHT_BOLD;
    return FONT_WEIGHT_REGULAR;
}

void app_fill_rect(vanilla_surface_t *surf, int32_t x, int32_t y, int32_t w, int32_t h, uint32_t color)
{
    if (!surf || !surf->pixels || w <= 0 || h <= 0)
        return;

    int32_t surf_w = (int32_t)surf->width;
    int32_t surf_h = (int32_t)surf->height;

    int32_t x0 = x < 0 ? 0 : x;
    int32_t y0 = y < 0 ? 0 : y;
    int32_t x1 = x + w;
    int32_t y1 = y + h;

    if (x1 > surf_w) x1 = surf_w;
    if (y1 > surf_h) y1 = surf_h;

    if (x0 >= x1 || y0 >= y1)
        return;

    uint32_t pitch_px = app_surface_pitch_px(surf);

    for (int32_t cy = y0; cy < y1; cy++) {
        uint32_t *row = surf->pixels + (size_t)cy * pitch_px;
        for (int32_t cx = x0; cx < x1; cx++) {
            row[cx] = color;
        }
    }
}

void app_draw_rect(vanilla_surface_t *surf, int32_t x, int32_t y, int32_t w, int32_t h, uint32_t color)
{
    if (w <= 0 || h <= 0)
        return;

    app_fill_rect(surf, x, y, w, 1, color);
    app_fill_rect(surf, x, y + h - 1, w, 1, color);
    app_fill_rect(surf, x, y, 1, h, color);
    app_fill_rect(surf, x + w - 1, y, 1, h, color);
}

void app_draw_char_scale(vanilla_surface_t *surf, int32_t x, int32_t y, char c, int scale, uint32_t color)
{
    if (!surf || !surf->pixels || scale <= 0)
        return;

    uint8_t uc = (uint8_t)c;
    if (uc == 0 || uc == ' ')
        return;
    if (uc < 0x20 || uc >= 128)
        uc = '?';

    vanilla_font_t *font = app_get_font();
    if (!font)
        return;

    float size_px = 11.0f * (float)scale;
    const vanilla_glyph_t *g = font_get_glyph(font, (uint32_t)uc, size_px);
    if (!g)
        return;

    int32_t cell_w = 8 * scale;
    int32_t draw_x = x + (cell_w - g->xadvance) / 2;
    if (draw_x < x)
        draw_x = x;

    vanilla_rect_t clip = { 0, 0, (int32_t)surf->width, (int32_t)surf->height };
    uint32_t pitch_px = app_surface_pitch_px(surf);
    font_draw_glyph(surf->pixels, pitch_px, &clip, font, (uint32_t)uc,
                    draw_x, y, size_px, FONT_WEIGHT_REGULAR, color);
}

void app_draw_char(vanilla_surface_t *surf, int32_t x, int32_t y, char c, uint32_t color)
{
    if (!surf || !surf->pixels)
        return;

    int32_t font_scale = (g_theme && g_theme->scale_factor >= 1.0f) ? (int32_t)g_theme->scale_factor : 1;
    if (font_scale < 1)
        font_scale = 1;

    app_draw_char_scale(surf, x, y, c, font_scale, color);
}

void app_draw_text_clipped_n(vanilla_surface_t *surf, const vanilla_rect_t *clip,
                             int32_t x, int32_t y, const char *text, size_t max_bytes,
                             float size_px, app_font_weight_t weight, uint32_t color)
{
    if (!surf || !surf->pixels || !text || max_bytes == 0 || text[0] == '\0')
        return;

    vanilla_font_t *font = app_get_font();
    if (!font)
        return;

    float eff_size = (size_px > 0.0f) ? size_px : app_default_font_size();

    vanilla_rect_t final_clip = { 0, 0, (int32_t)surf->width, (int32_t)surf->height };
    if (clip) {
        int32_t x1 = clip->x > 0 ? clip->x : 0;
        int32_t y1 = clip->y > 0 ? clip->y : 0;
        int32_t x2 = (clip->x + clip->w) < final_clip.w ? (clip->x + clip->w) : final_clip.w;
        int32_t y2 = (clip->y + clip->h) < final_clip.h ? (clip->y + clip->h) : final_clip.h;
        if (x2 <= x1 || y2 <= y1)
            return;
        final_clip.x = x1;
        final_clip.y = y1;
        final_clip.w = x2 - x1;
        final_clip.h = y2 - y1;
    }

    uint32_t pitch_px = app_surface_pitch_px(surf);
    font_draw_text_n(surf->pixels, pitch_px, &final_clip, font, text, max_bytes,
                     x, y, eff_size, app_clamp_weight(weight), color);
}

void app_draw_text_clipped(vanilla_surface_t *surf, const vanilla_rect_t *clip,
                           int32_t x, int32_t y, const char *text,
                           float size_px, app_font_weight_t weight, uint32_t color)
{
    app_draw_text_clipped_n(surf, clip, x, y, text, (size_t)-1, size_px, weight, color);
}

void app_draw_text_ex(vanilla_surface_t *surf, int32_t x, int32_t y, const char *text,
                      float size_px, app_font_weight_t weight, uint32_t color)
{
    app_draw_text_clipped(surf, NULL, x, y, text, size_px, weight, color);
}

void app_draw_text(vanilla_surface_t *surf, int32_t x, int32_t y, const char *text, uint32_t color)
{
    app_draw_text_ex(surf, x, y, text, app_default_font_size(), APP_FONT_REGULAR, color);
}

void app_draw_text_scale(vanilla_surface_t *surf, int32_t x, int32_t y, const char *text, int scale, uint32_t color)
{
    if (!text || scale <= 0)
        return;

    float size_px = 13.0f * (float)scale;
    app_draw_text_ex(surf, x, y, text, size_px, APP_FONT_REGULAR, color);
}

void app_measure_text_n(const char *text, size_t max_bytes, float size_px,
                        app_font_weight_t weight, int32_t *out_w, int32_t *out_h)
{
    if (out_w)
        *out_w = 0;
    if (out_h)
        *out_h = 0;

    if (!text)
        return;

    vanilla_font_t *font = app_get_font();
    if (!font)
        return;

    float eff_size = (size_px > 0.0f) ? size_px : app_default_font_size();
    font_measure_text_n(font, text, max_bytes, eff_size,
                        app_clamp_weight(weight), out_w, out_h);
}

void app_measure_text_ex(const char *text, float size_px, app_font_weight_t weight,
                         int32_t *out_w, int32_t *out_h)
{
    app_measure_text_n(text, (size_t)-1, size_px, weight, out_w, out_h);
}

void app_measure_text(const char *text, int32_t *out_w, int32_t *out_h)
{
    app_measure_text_ex(text, app_default_font_size(), APP_FONT_REGULAR, out_w, out_h);
}

void app_draw_button(vanilla_surface_t *surf, int32_t x, int32_t y, int32_t w, int32_t h,
                     const char *text, int is_pressed)
{
    uint32_t bg_col = is_pressed ?
                      (g_theme ? g_theme->accent_pressed : 0xFF5E81ACu) :
                      (g_theme ? g_theme->bg_elevated : 0xFF3B4252u);
    uint32_t border_col = is_pressed ?
                          (g_theme ? g_theme->accent : 0xFF88C0D0u) :
                          (g_theme ? g_theme->border : 0xFF4C566Au);
    uint32_t text_col = g_theme ? g_theme->fg_primary : 0xFFECEFF4u;

    app_fill_rect(surf, x, y, w, h, bg_col);
    app_draw_rect(surf, x, y, w, h, border_col);

    if (text && text[0] != '\0') {
        float fsize = app_default_font_size();
        int32_t text_w = 0;
        int32_t text_h = 0;
        app_measure_text_ex(text, fsize, APP_FONT_MEDIUM, &text_w, &text_h);
        int32_t tx = x + (w - text_w) / 2;
        int32_t ty = y + (h - text_h) / 2;
        if (is_pressed) {
            tx++;
            ty++;
        }
        vanilla_rect_t clip = { x + 1, y + 1, w > 2 ? w - 2 : 0, h > 2 ? h - 2 : 0 };
        app_draw_text_clipped(surf, &clip, tx, ty, text, fsize, APP_FONT_MEDIUM, text_col);
    }
}

void app_draw_progress_bar(vanilla_surface_t *surf, int32_t x, int32_t y, int32_t w, int32_t h,
                           int progress_pct, uint32_t bar_color, uint32_t bg_color)
{
    if (progress_pct < 0) progress_pct = 0;
    if (progress_pct > 100) progress_pct = 100;

    uint32_t border_col = g_theme ? g_theme->border : 0xFF4C566Au;
    app_fill_rect(surf, x, y, w, h, bg_color);
    app_draw_rect(surf, x, y, w, h, border_col);

    int fill_w = ((w - 2) * progress_pct) / 100;
    if (fill_w > 0)
        app_fill_rect(surf, x + 1, y + 1, fill_w, h - 2, bar_color);
}
