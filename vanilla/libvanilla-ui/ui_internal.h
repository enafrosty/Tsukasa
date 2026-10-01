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

struct ui_ctx_t {
    uint8_t          *arena_base;
    size_t            arena_size;
    size_t            offset;
    vanilla_layout_t *layout_ctx;
    ui_widget_t      *focused_widget;
    ui_widget_t      *pressed_widget;
    int32_t           mouse_x;
    int32_t           mouse_y;
    int               mouse_down;
    int               shift_down;
    int               ctrl_down;
    int               alt_down;
};

void *ui_alloc(ui_ctx_t *ctx, size_t size);
int64_t ui_current_time_ms(void);

void ui_text_field_handle_key(ui_widget_t *w, const struct input_event *ev, int shift, int ctrl);
void ui_text_field_handle_click(ui_widget_t *w, int32_t click_x, int32_t click_y, int shift);
void ui_text_field_update_blink(ui_widget_t *w);
void ui_text_field_emit_draw(ui_widget_t *w, vanilla_draw_cmd_array_t *out);

void ui_widget_emit_draw(ui_widget_t *w, vanilla_draw_cmd_array_t *out);

#endif /* _VANILLA_UI_INTERNAL_H */
