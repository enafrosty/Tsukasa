/*
 * Project Tsukasa — Display Server Widget Toolkit Constructors
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

#ifndef _VANILLA_UI_WIDGETS_H
#define _VANILLA_UI_WIDGETS_H

#include "ui.h"

ui_widget_t *ui_button(ui_ctx_t *ctx, const char *label, ui_event_cb_t cb, void *ud);
ui_widget_t *ui_label(ui_ctx_t *ctx, const char *text, uint32_t color);
ui_widget_t *ui_label_ex(ui_ctx_t *ctx, const char *text, uint32_t color,
                         float font_size, int32_t font_weight);
void         ui_label_set_text(ui_widget_t *w, const char *text);
void         ui_widget_set_font(ui_widget_t *w, float font_size, int32_t font_weight);

ui_widget_t *ui_text_field(ui_ctx_t *ctx, const char *initial, ui_event_cb_t cb, void *ud);
ui_widget_t *ui_text_area(ui_ctx_t *ctx, const char *initial, int32_t show_line_numbers,
                          ui_event_cb_t cb, void *ud);
const char  *ui_text_field_get_text(const ui_widget_t *w);
void         ui_text_field_set_text(ui_widget_t *w, const char *text);
void         ui_text_field_get_cursor_lc(const ui_widget_t *w, int32_t *out_line, int32_t *out_col);
int32_t      ui_text_field_get_line_count(const ui_widget_t *w);
void         ui_text_field_select_all(ui_widget_t *w);
void         ui_text_field_free(ui_widget_t *w);

ui_widget_t *ui_checkbox(ui_ctx_t *ctx, const char *label, int32_t checked,
                         ui_event_cb_t cb, void *ud);
ui_widget_t *ui_radio(ui_ctx_t *ctx, const char *label, int32_t group_id, int32_t checked,
                      ui_event_cb_t cb, void *ud);
ui_widget_t *ui_slider(ui_ctx_t *ctx, float value, float min_val, float max_val,
                       ui_event_cb_t cb, void *ud);
ui_widget_t *ui_list(ui_ctx_t *ctx, const char **items, int32_t count,
                     ui_event_cb_t cb, void *ud);
void         ui_list_set_items(ui_widget_t *w, const char **items, int32_t count);
void         ui_list_scroll_to_selected(ui_widget_t *w);

ui_widget_t *ui_scroll_view(ui_ctx_t *ctx);
void         ui_scroll_view_scroll_to(ui_widget_t *w, int32_t scroll_y);

ui_widget_t *ui_tab_bar(ui_ctx_t *ctx, const char **labels, int32_t count,
                        ui_event_cb_t cb, void *ud);

ui_widget_t *ui_modal_dialog(ui_ctx_t *ctx, const char *title, const char *body,
                             const char *ok_label, ui_event_cb_t cb, void *ud);
ui_widget_t *ui_dialog_confirm(ui_ctx_t *ctx, const char *title, const char *body,
                               const char *ok_label, const char *cancel_label,
                               ui_event_cb_t cb, void *ud);
ui_widget_t *ui_dialog_prompt(ui_ctx_t *ctx, const char *title, const char *body,
                              const char *initial_input, const char *ok_label,
                              const char *cancel_label, ui_event_cb_t cb, void *ud);
const char  *ui_dialog_get_input(const ui_widget_t *dlg);

ui_widget_t *ui_toast(ui_ctx_t *ctx, const char *message, int32_t timeout_ms);
ui_widget_t *ui_box(ui_ctx_t *ctx, vanilla_flex_dir_t dir);

ui_widget_t *ui_menu(ui_ctx_t *ctx, const char **items, int32_t count,
                     ui_event_cb_t cb, void *ud);
ui_widget_t *ui_menu_ex(ui_ctx_t *ctx, const ui_menu_item_t *items, int32_t count,
                        ui_event_cb_t cb, void *ud);
ui_widget_t *ui_menubar(ui_ctx_t *ctx, const char **labels, int32_t count,
                        ui_event_cb_t cb, void *ud);
void         ui_menu_set_rich_items(ui_widget_t *w, const ui_menu_item_t *items, int32_t count);
int32_t      ui_menubar_item_x(const ui_widget_t *bar, int32_t idx);

ui_widget_t *ui_progress_bar(ui_ctx_t *ctx, float value, float max_val,
                             uint32_t bar_color, int32_t show_label);
ui_widget_t *ui_progress_bar_create(ui_ctx_t *ctx, float value);
void         ui_progress_bar_set(ui_widget_t *w, float value);
void         ui_progress_bar_set_color(ui_widget_t *w, uint32_t bar_color);

#endif /* _VANILLA_UI_WIDGETS_H */
