/*
 * Project Tsukasa — Window manager (z-ordered windows with drag/close/focus)
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

#ifndef WM_H
#define WM_H

#include "blit.h"
#include "../input/event.h"
#include <stdint.h>
#include <stddef.h>

#define WM_MAX_WINDOWS    16
#define WM_TITLE_MAX      48

#define WM_FLAG_VISIBLE   (1u << 0)
#define WM_FLAG_ACTIVE    (1u << 1)
#define WM_FLAG_DRAGGING  (1u << 2)
#define WM_FLAG_CLOSABLE  (1u << 3)
#define WM_FLAG_RESIZABLE (1u << 4)
#define WM_FLAG_AUTOCLOSE (1u << 5)

/* Must match UI_TITLE_H / UI_BORDER in ui.h */
#define WM_TITLE_BAR_H    28
#define WM_BORDER_PX       2
#define WM_BTN_W          18
#define WM_BTN_H          16
#define WM_BTN_GAP         2

/* Forward declaration. */
struct wm_window;
typedef struct wm_window wm_window_t;

/* App callback: draw content into window client area. */
typedef void (*wm_draw_fn)(wm_window_t *win);

/* App callback: handle an input event forwarded to this window. */
typedef void (*wm_event_fn)(wm_window_t *win, const void *event);

struct wm_window {
    int x, y, w, h;
    int min_w, min_h;
    char title[WM_TITLE_MAX];
    uint32_t flags;

    uint8_t  opacity;
    uint32_t accent;

    wm_draw_fn   draw_content;
    wm_event_fn  handle_event;
    void         *app_data;

    wm_window_t *prev;
    wm_window_t *next;
};

typedef struct wm_dirty_rect {
    int x;
    int y;
    int w;
    int h;
} wm_dirty_rect_t;

void wm_init(void);

/* Create a new window and add it to the top of the z-order. @return Pointer to the window, or NULL on failure. */
wm_window_t *wm_create_window(int x, int y, int w, int h, const char *title,
                               wm_draw_fn draw_fn, wm_event_fn event_fn,
                               void *app_data);

/* Destroy a window and remove it from the z-order. */
void wm_destroy_window(wm_window_t *win);

/* Bring a window to the top of the z-order and set it active. */
void wm_bring_to_front(wm_window_t *win);

/* Find the topmost window containing point (px, py). @return Window pointer, or NULL if no window is under... */
wm_window_t *wm_find_window_at(int px, int py);

/* Handle a mouse event at (mx, my) with button state. Returns 1 if a window was affected, 0 otherwise. */
int wm_handle_mouse(int mx, int my, int buttons, int btn_changed);

/* Handle a normalized input event. Returns 1 if WM consumed the event, 0 otherwise. */
int wm_handle_input(const struct gui_event *ev);

void wm_redraw_all(void);

void wm_redraw_region(int x, int y, int w, int h);

void wm_set_resizable(wm_window_t *win, int resizable);

/* Update a window title. */
void wm_set_title(wm_window_t *win, const char *title);

/* Configure close button behavior. autoclose=1 keeps legacy behavior (destroy on close click). */
void wm_set_autoclose(wm_window_t *win, int autoclose);

/* Mark a screen-space region as dirty. */
void wm_mark_dirty_rect(int x, int y, int w, int h);

/* Drain pending dirty regions into caller buffer. Returns number of rectangles copied. */
int wm_collect_dirty_regions(wm_dirty_rect_t *out, int max);

/* Get the head (bottom) of the z-order list. */
wm_window_t *wm_get_bottom(void);

/* Get the tail (top/active) of the z-order list. */
wm_window_t *wm_get_top(void);

/* Get the client area coordinates for a window. */
void wm_client_rect(const wm_window_t *win,
                    int *cx, int *cy, int *cw, int *ch);

#endif /* WM_H */
