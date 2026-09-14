/*
 * Project Tsukasa — libwidget: retained-mode widget kit (guide 16)
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

#ifndef USER_LIBWIDGET_H
#define USER_LIBWIDGET_H

#include <stdbool.h>
#include <stdint.h>

#define UI_GRADIENT_MAX_STOPS 4

typedef struct ui_gradient_stop {
    uint32_t color;
    int pos_pct;
} ui_gradient_stop_t;

typedef struct ui_gradient {
    bool horizontal;
    int stop_count;
    ui_gradient_stop_t stops[UI_GRADIENT_MAX_STOPS];
} ui_gradient_t;

void ui_gradient_init(ui_gradient_t *g, bool horizontal);
int  ui_gradient_add_stop(ui_gradient_t *g, int pos_pct, uint32_t color);
/* Integer channel-lerped color at pos_pct (0..100). */
uint32_t ui_gradient_sample(const ui_gradient_t *g, int pos_pct);

typedef struct {
    void *user_data;
    void (*draw_rect)(void *user_data, int x, int y, int w, int h, uint32_t color);
    void (*draw_rounded_rect_filled)(void *user_data, int x, int y, int w, int h, int radius, uint32_t color);
    void (*draw_string)(void *user_data, int x, int y, const char *str, uint32_t color);
    int (*measure_string_width)(void *user_data, const char *str);
    void (*mark_dirty)(void *user_data, int x, int y, int w, int h);
    bool use_light_theme;
    void (*draw_line)(void *user_data, int x0, int y0, int x1, int y1, uint32_t color);
    void (*fill_ellipse)(void *user_data, int cx, int cy, int rx, int ry, uint32_t color);
    void (*draw_gradient)(void *user_data, int x, int y, int w, int h, const ui_gradient_t *g);
} widget_context_t;

void widget_context_init(widget_context_t *ctx,
                         void *user_data,
                         void (*draw_rect)(void *user_data, int x, int y, int w, int h, uint32_t color),
                         void (*draw_rounded_rect_filled)(void *user_data, int x, int y, int w, int h, int radius, uint32_t color),
                         void (*draw_string)(void *user_data, int x, int y, const char *str, uint32_t color),
                         int (*measure_string_width)(void *user_data, const char *str),
                         void (*mark_dirty)(void *user_data, int x, int y, int w, int h),
                         bool use_light_theme);

/* Optional painter extension setter (leaves the classic five untouched). */
void widget_context_set_painter_ex(widget_context_t *ctx,
                                   void (*draw_line)(void *, int, int, int, int, uint32_t),
                                   void (*fill_ellipse)(void *, int, int, int, int, uint32_t),
                                   void (*draw_gradient)(void *, int, int, int, int, const ui_gradient_t *));

#define UI_TK_EV_POINTER 1
#define UI_TK_EV_KEY     2

typedef struct ui_tk_event {
    int type;
    int x, y;
    uint32_t buttons;
    bool down;
    bool clicked;
    uint32_t keycode;
    bool pressed;
} ui_tk_event_t;

/* signals (enum-typed — deliberate divergence from NTK's strings) */

typedef enum ui_signal {
    UI_SIGNAL_CLICKED = 0,
    UI_SIGNAL_CHANGED,
    UI_SIGNAL_TOGGLED,
    UI_SIGNAL_SELECTED,
    UI_SIGNAL_COUNT
} ui_signal_t;

struct ui_widget;
typedef void (*ui_signal_fn)(struct ui_widget *w, void *userdata);

#define UI_SIGNAL_SLOTS 8   /* listener slots per widget, across all signals */

typedef struct ui_signal_slot {
    bool in_use;
    ui_signal_t sig;
    ui_signal_fn fn;
    void *userdata;
} ui_signal_slot_t;

/* widget base + tree */

#define UI_WIDGET_MAX_CHILDREN 8

typedef enum ui_widget_type {
    UI_WIDGET_PLAIN = 0,
    UI_WIDGET_BUTTON,
    UI_WIDGET_SCROLLBAR,
    UI_WIDGET_TEXTBOX,
    UI_WIDGET_DROPDOWN,
    UI_WIDGET_CHECKBOX,
    UI_WIDGET_BOX,
    UI_WIDGET_GRID
} ui_widget_type_t;

/* The retained base every widget embeds as its FIRST member (named `base`), so &widget == &widget->base and... */
typedef struct ui_widget {
    int x, y, w, h;
    int pref_w, pref_h;
    bool visible;
    ui_widget_type_t type;
    struct ui_widget *parent;
    struct ui_widget *children[UI_WIDGET_MAX_CHILDREN];
    int child_count;
    void (*draw)(struct ui_widget *w, widget_context_t *ctx);
    bool (*handle_event)(struct ui_widget *w, const ui_tk_event_t *ev, void *user_data);
    ui_signal_slot_t slots[UI_SIGNAL_SLOTS];
    void *user_data;
} ui_widget_t;

void ui_widget_base_init(ui_widget_t *w, ui_widget_type_t type,
                         int x, int y, int width, int height);
int  ui_widget_add_child(ui_widget_t *parent, ui_widget_t *child);
void ui_widget_set_visible(ui_widget_t *w, bool visible);

/* Draw w and its visible children, depth-first (children above parent). */
void ui_widget_draw_tree(ui_widget_t *w, widget_context_t *ctx);

/* Route an event to the deepest visible widget containing (ev->x, ev->y) (topmost sibling first, NTK's... */
bool ui_widget_dispatch(ui_widget_t *root, const ui_tk_event_t *ev);

/* Signals: multiple listeners, fired in registration order. */
int ui_signal_connect(ui_widget_t *w, ui_signal_t sig, ui_signal_fn fn, void *userdata);
int ui_signal_disconnect(ui_widget_t *w, ui_signal_t sig, ui_signal_fn fn);
int ui_signal_emit(ui_widget_t *w, ui_signal_t sig);

#define UI_HORIZONTAL 0
#define UI_VERTICAL   1

typedef struct ui_box {
    ui_widget_t base;
    int orientation;
    int spacing;
    bool homogeneous;
    struct {
        bool expand;
        bool fill;
        int padding;
    } pack[UI_WIDGET_MAX_CHILDREN];
} ui_box_t;

void ui_box_init(ui_box_t *box, int orientation, int x, int y, int w, int h);
int  ui_box_pack(ui_box_t *box, ui_widget_t *child, bool expand, bool fill, int padding);
void ui_box_relayout(ui_box_t *box);
void ui_box_set_geometry(ui_box_t *box, int x, int y, int w, int h);

typedef struct ui_grid {
    ui_widget_t base;
    int rows, cols;
    int spacing;
    struct {
        int row, col;
    } cell[UI_WIDGET_MAX_CHILDREN];
} ui_grid_t;

void ui_grid_init(ui_grid_t *grid, int rows, int cols, int spacing,
                  int x, int y, int w, int h);
int  ui_grid_attach(ui_grid_t *grid, ui_widget_t *child, int row, int col);
void ui_grid_relayout(ui_grid_t *grid);
void ui_grid_set_geometry(ui_grid_t *grid, int x, int y, int w, int h);

/* ---- the five classic widgets (guide-16 retrofit: `base` first member; */
/* every other field, init/draw/handle signature, and callback is intact) - */

typedef struct {
    ui_widget_t base;
    const char *text;
    bool pressed;
    bool hovered;
    void (*on_click)(void *user_data);
} widget_button_t;

void widget_button_init(widget_button_t *btn, int x, int y, int w, int h, const char *text);
void widget_button_draw(widget_context_t *ctx, widget_button_t *btn);
bool widget_button_handle_mouse(widget_button_t *btn, int mx, int my, bool mouse_down, bool mouse_clicked, void *user_data);

typedef struct {
    ui_widget_t base;
    int content_height;
    int scroll_y;
    bool is_dragging;
    int drag_start_my;
    int drag_start_scroll_y;
    void (*on_scroll)(void *user_data, int new_scroll_y);
} widget_scrollbar_t;

void widget_scrollbar_init(widget_scrollbar_t *sb, int x, int y, int w, int h);
void widget_scrollbar_update(widget_scrollbar_t *sb, int content_height, int scroll_y);
void widget_scrollbar_draw(widget_context_t *ctx, widget_scrollbar_t *sb);
bool widget_scrollbar_handle_mouse(widget_scrollbar_t *sb, int mx, int my, bool mouse_down, void *user_data);

typedef struct {
    ui_widget_t base;
    char *text;
    int max_len;
    int cursor_pos;
    bool focused;
    void (*on_change)(void *user_data);
} widget_textbox_t;

void widget_textbox_init(widget_textbox_t *tb, int x, int y, int w, int h, char *buffer, int max_len);
void widget_textbox_draw(widget_context_t *ctx, widget_textbox_t *tb);
bool widget_textbox_handle_mouse(widget_textbox_t *tb, int mx, int my, bool mouse_clicked, void *user_data);
bool widget_textbox_handle_key(widget_textbox_t *tb, char c, void *user_data);

typedef struct {
    ui_widget_t base;
    const char **items;
    int item_count;
    int selected_idx;
    bool is_open;
    void (*on_select)(void *user_data, int new_idx);
} widget_dropdown_t;

void widget_dropdown_init(widget_dropdown_t *dd, int x, int y, int w, int h, const char **items, int count);
void widget_dropdown_draw(widget_context_t *ctx, widget_dropdown_t *dd);
bool widget_dropdown_handle_mouse(widget_dropdown_t *dd, int mx, int my, bool mouse_clicked, void *user_data);

typedef struct {
    ui_widget_t base;
    const char *text;
    bool checked;
    bool is_radio;
    void (*on_toggle)(void *user_data, bool new_state);
} widget_checkbox_t;

void widget_checkbox_init(widget_checkbox_t *cb, int x, int y, int w, int h, const char *text, bool is_radio);
void widget_checkbox_draw(widget_context_t *ctx, widget_checkbox_t *cb);
bool widget_checkbox_handle_mouse(widget_checkbox_t *cb, int mx, int my, bool mouse_clicked, void *user_data);

#endif /* USER_LIBWIDGET_H */
