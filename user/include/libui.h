/*
 * Project Tsukasa — User graphical interface client library header
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

#ifndef USER_LIBUI_H
#define USER_LIBUI_H

#include <stdbool.h>
#include <stdint.h>

typedef uint64_t ui_window_t;

enum {
    UI_OK = 0,
    UI_ERR_INVALID = -1,
    UI_ERR_NOTFOUND = -2,
    UI_ERR_NOMEM = -3,
    UI_ERR_PERM = -4,
    UI_ERR_FULL = -5,
    UI_ERR_AGAIN = -6,
};

enum {
    UI_EVENT_NONE = 0,
    UI_EVENT_PAINT = 1,
    UI_EVENT_CLICK = 2,
    UI_EVENT_RIGHT_CLICK = 3,
    UI_EVENT_CLOSE = 4,
    UI_EVENT_KEY = 5,
    UI_EVENT_MOUSE_DOWN = 6,
    UI_EVENT_MOUSE_UP = 7,
    UI_EVENT_MOUSE_MOVE = 8,
    UI_EVENT_MOUSE_WHEEL = 9,
    UI_EVENT_KEYUP = 10,
    UI_EVENT_RESIZE = 11,
};

typedef struct {
    int type;
    int window;
    int x;
    int y;
    int keycode;
    int data1;
    int data2;
} ui_event_t;

ui_window_t ui_window_create(const char *title, int x, int y, int w, int h);
int ui_window_destroy(ui_window_t win);
void ui_window_set_title(ui_window_t win, const char *title);
void ui_window_set_resizable(ui_window_t win, bool resizable);
void ui_get_screen_size(uint64_t *out_w, uint64_t *out_h);
int ui_window_set_title_ex(ui_window_t win, const char *title);
int ui_window_set_resizable_ex(ui_window_t win, bool resizable);
int ui_get_screen_size_ex(uint64_t *out_w, uint64_t *out_h);

void ui_draw_rect(ui_window_t win, int x, int y, int w, int h, uint32_t color);
void ui_draw_rounded_rect_filled(ui_window_t win, int x, int y, int w, int h, int radius, uint32_t color);
void ui_draw_string(ui_window_t win, int x, int y, const char *str, uint32_t color);
void ui_draw_image(ui_window_t win, int x, int y, int w, int h, const uint32_t *image_data);
void ui_mark_dirty(ui_window_t win, int x, int y, int w, int h);
int ui_draw_rect_ex(ui_window_t win, int x, int y, int w, int h, uint32_t color);
int ui_draw_rounded_rect_filled_ex(ui_window_t win, int x, int y, int w, int h, int radius, uint32_t color);
int ui_draw_string_ex(ui_window_t win, int x, int y, const char *str, uint32_t color);
int ui_draw_image_ex(ui_window_t win, int x, int y, int w, int h, const uint32_t *image_data);
int ui_mark_dirty_ex(ui_window_t win, int x, int y, int w, int h);

uint32_t ui_get_string_width(const char *str);
uint32_t ui_get_font_height(void);
bool ui_get_event(ui_window_t win, ui_event_t *ev);
int ui_get_event_ex(ui_window_t win, ui_event_t *ev);

#endif /* USER_LIBUI_H */
