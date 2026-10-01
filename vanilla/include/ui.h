/*
 * Project Tsukasa — Display Server Retained Widget Toolkit API
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

#ifndef _VANILLA_UI_H
#define _VANILLA_UI_H

#include <stdint.h>
#include <stddef.h>
#include "layout.h"
#include "draw_cmd.h"
#include "theme.h"
#include "surface.h"

struct input_event;

typedef struct ui_widget_t ui_widget_t;
typedef struct ui_ctx_t ui_ctx_t;

typedef enum {
    UI_WIDGET_BOX = 0,
    UI_WIDGET_BUTTON,
    UI_WIDGET_LABEL,
    UI_WIDGET_TEXT_FIELD,
    UI_WIDGET_CHECKBOX,
    UI_WIDGET_RADIO,
    UI_WIDGET_SLIDER,
    UI_WIDGET_LIST,
    UI_WIDGET_SCROLL_VIEW,
    UI_WIDGET_MENU,
    UI_WIDGET_TAB_BAR,
    UI_WIDGET_MODAL_DIALOG,
    UI_WIDGET_TOAST,
} ui_widget_type_t;

typedef enum {
    UI_EVENT_CLICK,
    UI_EVENT_VALUE_CHANGED,
    UI_EVENT_TEXT_CHANGED,
    UI_EVENT_FOCUS_GAINED,
    UI_EVENT_FOCUS_LOST,
    UI_EVENT_KEY_DOWN,
} ui_event_type_t;

typedef struct {
    ui_event_type_t type;
    ui_widget_t    *source;
    union {
        struct { int32_t x; int32_t y; } click;
        struct { float value; }          slider;
        struct { int32_t state; }        toggle;
        struct { uint16_t code; int shift; int ctrl; } key;
    };
} ui_event_t;

typedef void (*ui_event_cb_t)(ui_widget_t *widget, const ui_event_t *ev, void *userdata);

struct ui_widget_t {
    ui_widget_type_t type;
    vanilla_elem_t  *layout_elem;

    ui_widget_t     *parent;
    ui_widget_t     *first_child;
    ui_widget_t     *next_sibling;

    int32_t          focusable;
    int32_t          focused;

    int32_t          dirty;
    vanilla_draw_rect_t dirty_rect;

    ui_event_cb_t    on_event;
    void            *userdata;

    union {
        struct { const char *label; int32_t pressed; }                    button;
        struct { const char *text; uint32_t color; }                      label;
        struct {
            char    buf[256];
            int32_t cursor_pos;
            int32_t sel_start;
            int32_t sel_end;
            int32_t scroll_x;
            int64_t caret_ms;
        }                                                                  text_field;
        struct { int32_t checked; const char *label; }                     checkbox;
        struct { int32_t checked; const char *label; int32_t group_id; }   radio;
        struct { float value; float min_val; float max_val; }              slider;
        struct {
            const char **items;
            int32_t      count;
            int32_t      selected;
            int32_t      scroll_top;
        }                                                                  list;
        struct { int32_t tab_count; int32_t active_tab; const char **labels; } tab_bar;
        struct { const char *message; int32_t timeout_ms; int64_t shown_ms; } toast;
        struct { const char *title; const char *body; const char *ok_label; } modal;
        struct { int32_t scroll_y; int32_t content_h; }                      scroll_view;
        struct { const char **items; int32_t count; int32_t selected; }      menu;
    };
};

ui_ctx_t *ui_ctx_init(void *arena, size_t arena_size, vanilla_layout_t *layout_ctx);

ui_widget_t *ui_alloc_widget(ui_ctx_t *ctx, ui_widget_type_t type);

void ui_widget_add_child(ui_widget_t *parent, ui_widget_t *child);

void ui_widget_invalidate(ui_widget_t *w);

void ui_handle_event(ui_ctx_t *ctx, ui_widget_t *root, const struct input_event *ev);

void ui_render(ui_ctx_t *ctx, ui_widget_t *root,
               vanilla_surface_t *target,
               const vanilla_rect_t *dirty_clip);

void ui_focus_next(ui_ctx_t *ctx, ui_widget_t *root);
void ui_focus_prev(ui_ctx_t *ctx, ui_widget_t *root);
void ui_widget_set_focus(ui_ctx_t *ctx, ui_widget_t *w);
ui_widget_t *ui_get_focused(ui_ctx_t *ctx);
ui_widget_t *ui_hit_test(ui_widget_t *root, int32_t x, int32_t y);

#endif /* _VANILLA_UI_H */
