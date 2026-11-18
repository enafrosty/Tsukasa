/*
 * Project Tsukasa — Display Server Widget Toolkit Internal Header
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

#ifndef _VANILLA_UI_INTERNAL_H
#define _VANILLA_UI_INTERNAL_H

#include "../include/ui.h"
#include "../include/ui_widgets.h"
#include "../apps/app_common.h"

#define UI_SCROLLBAR_W 10
#define UI_LIST_ROW_H  24
#define UI_MENU_ROW_H  24
#define UI_MENU_SEP_H  9

struct ui_ctx_t {
    uint8_t              *arena_base;
    size_t                arena_size;
    size_t                offset;
    vanilla_layout_t     *layout_ctx;
    ui_widget_t          *focused_widget;
    ui_widget_t          *pressed_widget;
    ui_widget_t          *hovered_widget;
    ui_widget_t          *active_popup;
    ui_widget_t          *active_modal;
    int32_t               overlay_dirty;
    int32_t               mouse_x;
    int32_t               mouse_y;
    int                   mouse_down;
    int                   shift_down;
    int                   ctrl_down;
    int                   alt_down;
    ui_clipboard_set_fn_t clipboard_set_fn;
    ui_clipboard_get_fn_t clipboard_get_fn;
};

void   *ui_alloc(ui_ctx_t *ctx, size_t size);
int64_t ui_current_time_ms(void);

void ui_text_field_handle_key(ui_widget_t *w, const struct input_event *ev, int shift, int ctrl);
void ui_text_field_handle_key_ctx(ui_ctx_t *ctx, ui_widget_t *w, const struct input_event *ev, int shift, int ctrl);
void ui_text_field_handle_click(ui_widget_t *w, int32_t click_x, int32_t click_y, int shift);
void ui_text_field_handle_drag(ui_widget_t *w, int32_t mx, int32_t my);
void ui_text_field_handle_release(ui_widget_t *w);
void ui_text_field_handle_wheel(ui_widget_t *w, int32_t delta);
void ui_text_field_update_blink(ui_widget_t *w);
void ui_text_field_emit_draw(ui_widget_t *w, vanilla_draw_cmd_array_t *out);

void ui_widget_emit_draw(ui_widget_t *w, vanilla_draw_cmd_array_t *out);
void ui_widget_emit_scroll_overlay(ui_widget_t *w, vanilla_draw_cmd_array_t *out);

int32_t ui_scroll_view_effective_content_h(const ui_widget_t *w);
void    ui_scrollbar_metrics(int32_t x, int32_t y, int32_t w, int32_t h,
                             int32_t content_h, int32_t scroll_y,
                             int32_t *track_x, int32_t *track_y,
                             int32_t *track_w, int32_t *track_h,
                             int32_t *thumb_y, int32_t *thumb_h,
                             int32_t *max_scroll);

int32_t ui_menu_is_separator(const ui_widget_t *w, int32_t idx);
int32_t ui_menu_is_disabled(const ui_widget_t *w, int32_t idx);
void    ui_menu_row_rect(const ui_widget_t *w, int32_t idx, int32_t *out_y, int32_t *out_h);
int32_t ui_menu_hit_item(const ui_widget_t *w, int32_t mx, int32_t my);

void ui_modal_compute_layout(const ui_widget_t *w, int32_t *cx, int32_t *cy,
                             int32_t *cw, int32_t *ch);

#endif /* _VANILLA_UI_INTERNAL_H */
