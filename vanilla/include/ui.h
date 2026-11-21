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
    UI_WIDGET_PROGRESS_BAR,
} ui_widget_type_t;

typedef enum {
    UI_EVENT_CLICK,
    UI_EVENT_VALUE_CHANGED,
    UI_EVENT_TEXT_CHANGED,
    UI_EVENT_FOCUS_GAINED,
    UI_EVENT_FOCUS_LOST,
    UI_EVENT_KEY_DOWN,
} ui_event_type_t;

#define UI_MENU_ITEM_SEPARATOR (1u << 0)
#define UI_MENU_ITEM_DISABLED  (1u << 1)
#define UI_MENU_ITEM_CHECKED   (1u << 2)
#define UI_MENU_ITEM_SUBMENU   (1u << 3)

typedef struct {
    const char *label;
    const char *shortcut;
    uint32_t    flags;
    uint32_t    id;
} ui_menu_item_t;

typedef enum {
    UI_DIALOG_INFO = 0,
    UI_DIALOG_CONFIRM = 1,
    UI_DIALOG_INPUT = 2,
} ui_dialog_kind_t;

typedef enum {
    UI_DIALOG_RES_NONE = 0,
    UI_DIALOG_RES_OK = 1,
    UI_DIALOG_RES_CANCEL = 2,
} ui_dialog_result_t;

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
typedef int  (*ui_clipboard_set_fn_t)(const char *text, size_t len);
typedef int  (*ui_clipboard_get_fn_t)(char *out_buf, size_t max_len);

struct ui_widget_t {
    ui_widget_type_t type;
    vanilla_elem_t  *layout_elem;

    ui_widget_t     *parent;
    ui_widget_t     *first_child;
    ui_widget_t     *next_sibling;

    int32_t          focusable;
    int32_t          focused;
    int32_t          hidden;

    int32_t          dirty;
    vanilla_draw_rect_t dirty_rect;

    ui_event_cb_t    on_event;
    void            *userdata;

    union {
        struct { const char *label; int32_t pressed; int32_t hovered; } button;
        struct { const char *text; uint32_t color; }                      label;
        struct {
            char     buf[256];
            int32_t  cursor_pos;
            int32_t  sel_start;
            int32_t  sel_end;
            int32_t  scroll_x;
            int64_t  caret_ms;
            int32_t  multiline;
            int32_t  show_line_numbers;
            int32_t  read_only;
            int32_t  scroll_y;
            int32_t  sel_anchor;
            int32_t  sb_dragging;
            int32_t  sb_drag_off;
            int32_t  ensure_cursor_visible;
            float    font_size;
            char    *ml_buf;
            int32_t  ml_len;
            int32_t  ml_cap;
        }                                                                  text_field;
        struct { int32_t checked; const char *label; }                     checkbox;
        struct { int32_t checked; const char *label; int32_t group_id; }   radio;
        struct { float value; float min_val; float max_val; }              slider;
        struct {
            const char **items;
            int32_t      count;
            int32_t      selected;
            int32_t      scroll_top;
            int32_t      hovered;
            int32_t      sb_dragging;
            int32_t      sb_drag_off;
        }                                                                  list;
        struct {
            int32_t tab_count;
            int32_t active_tab;
            const char **labels;
            int32_t hovered_tab;
        }                                                                  tab_bar;
        struct { const char *message; int32_t timeout_ms; int64_t shown_ms; } toast;
        struct {
            const char        *title;
            const char        *body;
            const char        *ok_label;
            const char        *cancel_label;
            ui_dialog_kind_t   kind;
            ui_dialog_result_t result;
            ui_widget_t       *input_field;
            int32_t            hovered_btn;
            int32_t            pressed_btn;
        }                                                                  modal;
        struct {
            int32_t scroll_y;
            int32_t content_h;
            int32_t sb_dragging;
            int32_t sb_drag_off;
        }                                                                  scroll_view;
        struct {
            const char           **items;
            const ui_menu_item_t  *rich_items;
            int32_t                count;
            int32_t                selected;
            int32_t                hovered;
            int32_t                is_bar;
            int32_t                floating;
            int32_t                float_x;
            int32_t                float_y;
        }                                                                  menu;
        struct {
            float    value;
            float    max_val;
            uint32_t bar_color;
            int32_t  show_label;
            char     label_buf[32];
        }                                                                  progress_bar;
    };
};

ui_ctx_t *ui_ctx_init(void *arena, size_t arena_size, vanilla_layout_t *layout_ctx);

void ui_ctx_set_clipboard_ops(ui_ctx_t *ctx,
                              ui_clipboard_set_fn_t set_fn,
                              ui_clipboard_get_fn_t get_fn);

void ui_ctx_show_popup(ui_ctx_t *ctx, ui_widget_t *menu, int32_t x, int32_t y);
void ui_ctx_hide_popup(ui_ctx_t *ctx);
ui_widget_t *ui_ctx_get_popup(ui_ctx_t *ctx);

void ui_ctx_show_modal(ui_ctx_t *ctx, ui_widget_t *modal);
void ui_ctx_hide_modal(ui_ctx_t *ctx);
ui_widget_t *ui_ctx_get_modal(ui_ctx_t *ctx);

ui_widget_t *ui_alloc_widget(ui_ctx_t *ctx, ui_widget_type_t type);

void ui_widget_add_child(ui_widget_t *parent, ui_widget_t *child);
void ui_widget_set_hidden(ui_widget_t *w, int32_t hidden);
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
