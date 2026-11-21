/*
 * Project Tsukasa — Project Vanilla Client Common UI Toolkit
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

#ifndef _VANILLA_APP_COMMON_H
#define _VANILLA_APP_COMMON_H

#include <stdint.h>
#include <stddef.h>
#include "../include/vanilla.h"
#include "../include/theme.h"
#include "../server/font.h"

typedef enum {
    APP_FONT_REGULAR = FONT_WEIGHT_REGULAR,
    APP_FONT_MEDIUM  = FONT_WEIGHT_MEDIUM,
    APP_FONT_BOLD    = FONT_WEIGHT_BOLD,
} app_font_weight_t;

/* Shared TrueType font access */
vanilla_font_t *app_get_font(void);
float           app_default_font_size(void);

/* 2D Drawing Primitives */
void app_fill_rect(vanilla_surface_t *surf, int32_t x, int32_t y, int32_t w, int32_t h, uint32_t color);
void app_draw_rect(vanilla_surface_t *surf, int32_t x, int32_t y, int32_t w, int32_t h, uint32_t color);
void app_draw_char(vanilla_surface_t *surf, int32_t x, int32_t y, char c, uint32_t color);
void app_draw_text(vanilla_surface_t *surf, int32_t x, int32_t y, const char *text, uint32_t color);
void app_draw_char_scale(vanilla_surface_t *surf, int32_t x, int32_t y, char c, int scale, uint32_t color);
void app_draw_text_scale(vanilla_surface_t *surf, int32_t x, int32_t y, const char *text, int scale, uint32_t color);
void app_draw_text_ex(vanilla_surface_t *surf, int32_t x, int32_t y, const char *text,
                      float size_px, app_font_weight_t weight, uint32_t color);
void app_draw_text_clipped(vanilla_surface_t *surf, const vanilla_rect_t *clip,
                           int32_t x, int32_t y, const char *text,
                           float size_px, app_font_weight_t weight, uint32_t color);
void app_draw_text_clipped_n(vanilla_surface_t *surf, const vanilla_rect_t *clip,
                             int32_t x, int32_t y, const char *text, size_t max_bytes,
                             float size_px, app_font_weight_t weight, uint32_t color);
void app_measure_text(const char *text, int32_t *out_w, int32_t *out_h);
void app_measure_text_ex(const char *text, float size_px, app_font_weight_t weight,
                         int32_t *out_w, int32_t *out_h);
void app_measure_text_n(const char *text, size_t max_bytes, float size_px,
                        app_font_weight_t weight, int32_t *out_w, int32_t *out_h);
void app_draw_button(vanilla_surface_t *surf, int32_t x, int32_t y, int32_t w, int32_t h,
                     const char *text, int is_pressed);
void app_draw_progress_bar(vanilla_surface_t *surf, int32_t x, int32_t y, int32_t w, int32_t h,
                           int progress_pct, uint32_t bar_color, uint32_t bg_color);

/* Evdev Keycode Translation */
#include "../input/keymap.h"

#endif /* _VANILLA_APP_COMMON_H */
