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
ui_widget_t *ui_text_field(ui_ctx_t *ctx, const char *initial, ui_event_cb_t cb, void *ud);
ui_widget_t *ui_checkbox(ui_ctx_t *ctx, const char *label, int32_t checked,
                         ui_event_cb_t cb, void *ud);
ui_widget_t *ui_radio(ui_ctx_t *ctx, const char *label, int32_t group_id, int32_t checked,
                      ui_event_cb_t cb, void *ud);
ui_widget_t *ui_slider(ui_ctx_t *ctx, float value, float min_val, float max_val,
                       ui_event_cb_t cb, void *ud);
ui_widget_t *ui_list(ui_ctx_t *ctx, const char **items, int32_t count,
                     ui_event_cb_t cb, void *ud);
ui_widget_t *ui_scroll_view(ui_ctx_t *ctx);
ui_widget_t *ui_tab_bar(ui_ctx_t *ctx, const char **labels, int32_t count,
                        ui_event_cb_t cb, void *ud);
ui_widget_t *ui_modal_dialog(ui_ctx_t *ctx, const char *title, const char *body,
                             const char *ok_label, ui_event_cb_t cb, void *ud);
ui_widget_t *ui_toast(ui_ctx_t *ctx, const char *message, int32_t timeout_ms);
ui_widget_t *ui_box(ui_ctx_t *ctx, vanilla_flex_dir_t dir);
ui_widget_t *ui_menu(ui_ctx_t *ctx, const char **items, int32_t count,
                     ui_event_cb_t cb, void *ud);

#endif /* _VANILLA_UI_WIDGETS_H */
