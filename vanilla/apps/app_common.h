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

/* Nord Polar Night & Frost Palette */
#define APP_COLOR_BG        0xFF2E3440u
#define APP_COLOR_SURFACE   0xFF3B4252u
#define APP_COLOR_CARD      0xFF434C5Eu
#define APP_COLOR_BORDER    0xFF4C566Au
#define APP_COLOR_TEXT      0xFFECEFF4u
#define APP_COLOR_MUTED     0xFFD8DEE9u
#define APP_COLOR_DIM       0xFF98A2B3u
#define APP_COLOR_PRIMARY   0xFF88C0D0u
#define APP_COLOR_ACCENT    0xFF81A1C1u
#define APP_COLOR_SUCCESS   0xFFA3BE8Cu
#define APP_COLOR_WARNING   0xFFEBCB8Bu
#define APP_COLOR_DANGER    0xFFBF616Au
#define APP_COLOR_BLACK     0xFF000000u
#define APP_COLOR_WHITE     0xFFFFFFFFu

/* 2D Drawing Primitives */
void app_fill_rect(vanilla_surface_t *surf, int32_t x, int32_t y, int32_t w, int32_t h, uint32_t color);
void app_draw_rect(vanilla_surface_t *surf, int32_t x, int32_t y, int32_t w, int32_t h, uint32_t color);
void app_draw_char(vanilla_surface_t *surf, int32_t x, int32_t y, char c, uint32_t color);
void app_draw_text(vanilla_surface_t *surf, int32_t x, int32_t y, const char *text, uint32_t color);
void app_draw_char_scale(vanilla_surface_t *surf, int32_t x, int32_t y, char c, int scale, uint32_t color);
void app_draw_text_scale(vanilla_surface_t *surf, int32_t x, int32_t y, const char *text, int scale, uint32_t color);
void app_draw_button(vanilla_surface_t *surf, int32_t x, int32_t y, int32_t w, int32_t h,
                     const char *text, int is_pressed);
void app_draw_progress_bar(vanilla_surface_t *surf, int32_t x, int32_t y, int32_t w, int32_t h,
                           int progress_pct, uint32_t bar_color, uint32_t bg_color);

/* Evdev Keycode Translation */
char app_evdev_to_ascii(uint16_t code, int shift);

#endif /* _VANILLA_APP_COMMON_H */
