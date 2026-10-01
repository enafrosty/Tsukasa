/*
 * Project Tsukasa — Display Server Widget Toolkit Component Implementations
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

#include "ui_internal.h"
#include <string.h>

static inline void push_cmd(vanilla_draw_cmd_array_t *out, vanilla_draw_cmd_t cmd)
{
    if (out && out->cmds && out->count < out->capacity)
        out->cmds[out->count++] = cmd;
}

ui_widget_t *ui_box(ui_ctx_t *ctx, vanilla_flex_dir_t dir)
{
    ui_widget_t *w = ui_alloc_widget(ctx, UI_WIDGET_BOX);
    if (!w)
        return NULL;

    if (w->layout_elem)
        w->layout_elem->direction = dir;

    return w;
}

ui_widget_t *ui_button(ui_ctx_t *ctx, const char *label, ui_event_cb_t cb, void *ud)
{
    ui_widget_t *w = ui_alloc_widget(ctx, UI_WIDGET_BUTTON);
    if (!w)
        return NULL;

    w->button.label = label;
    w->button.pressed = 0;
    w->on_event = cb;
    w->userdata = ud;

    if (w->layout_elem && label) {
        int len = (int)strlen(label);
        w->layout_elem->w_mode = VSIZE_FIXED;
        w->layout_elem->w_px = len * 8 + w->layout_elem->pad_left + w->layout_elem->pad_right;
    }

    return w;
}

ui_widget_t *ui_label(ui_ctx_t *ctx, const char *text, uint32_t color)
{
    ui_widget_t *w = ui_alloc_widget(ctx, UI_WIDGET_LABEL);
    if (!w)
        return NULL;

    w->label.text = text;
    w->label.color = color;

    if (w->layout_elem) {
        w->layout_elem->text = text;
        w->layout_elem->text_color = color;
    }

    return w;
}

ui_widget_t *ui_checkbox(ui_ctx_t *ctx, const char *label, int32_t checked,
                         ui_event_cb_t cb, void *ud)
{
    ui_widget_t *w = ui_alloc_widget(ctx, UI_WIDGET_CHECKBOX);
    if (!w)
        return NULL;

    w->checkbox.label = label;
    w->checkbox.checked = checked ? 1 : 0;
    w->on_event = cb;
    w->userdata = ud;

    if (w->layout_elem) {
        int len = label ? (int)strlen(label) : 0;
        w->layout_elem->w_mode = VSIZE_FIXED;
        w->layout_elem->w_px = 24 + len * 8 + 8;
    }

    return w;
}

ui_widget_t *ui_radio(ui_ctx_t *ctx, const char *label, int32_t group_id, int32_t checked,
                      ui_event_cb_t cb, void *ud)
{
    ui_widget_t *w = ui_alloc_widget(ctx, UI_WIDGET_RADIO);
    if (!w)
        return NULL;

    w->radio.label = label;
    w->radio.group_id = group_id;
    w->radio.checked = checked ? 1 : 0;
    w->on_event = cb;
    w->userdata = ud;

    if (w->layout_elem) {
        int len = label ? (int)strlen(label) : 0;
        w->layout_elem->w_mode = VSIZE_FIXED;
        w->layout_elem->w_px = 24 + len * 8 + 8;
    }

    return w;
}

ui_widget_t *ui_slider(ui_ctx_t *ctx, float value, float min_val, float max_val,
                       ui_event_cb_t cb, void *ud)
{
    ui_widget_t *w = ui_alloc_widget(ctx, UI_WIDGET_SLIDER);
    if (!w)
        return NULL;

    w->slider.value = value;
    w->slider.min_val = min_val;
    w->slider.max_val = max_val;
    w->on_event = cb;
    w->userdata = ud;

    return w;
}

ui_widget_t *ui_list(ui_ctx_t *ctx, const char **items, int32_t count,
                     ui_event_cb_t cb, void *ud)
{
    ui_widget_t *w = ui_alloc_widget(ctx, UI_WIDGET_LIST);
    if (!w)
        return NULL;

    w->list.items = items;
    w->list.count = count;
    w->list.selected = 0;
    w->list.scroll_top = 0;
    w->on_event = cb;
    w->userdata = ud;

    return w;
}

ui_widget_t *ui_scroll_view(ui_ctx_t *ctx)
{
    ui_widget_t *w = ui_alloc_widget(ctx, UI_WIDGET_SCROLL_VIEW);
    if (!w)
        return NULL;

    w->scroll_view.scroll_y = 0;
    w->scroll_view.content_h = 0;

    return w;
}

ui_widget_t *ui_tab_bar(ui_ctx_t *ctx, const char **labels, int32_t count,
                        ui_event_cb_t cb, void *ud)
{
    ui_widget_t *w = ui_alloc_widget(ctx, UI_WIDGET_TAB_BAR);
    if (!w)
        return NULL;

    w->tab_bar.labels = labels;
    w->tab_bar.tab_count = count;
    w->tab_bar.active_tab = 0;
    w->on_event = cb;
    w->userdata = ud;

    return w;
}

ui_widget_t *ui_modal_dialog(ui_ctx_t *ctx, const char *title, const char *body,
                             const char *ok_label, ui_event_cb_t cb, void *ud)
{
    ui_widget_t *w = ui_alloc_widget(ctx, UI_WIDGET_MODAL_DIALOG);
    if (!w)
        return NULL;

    w->modal.title = title;
    w->modal.body = body;
    w->modal.ok_label = ok_label;
    w->on_event = cb;
    w->userdata = ud;

    return w;
}

ui_widget_t *ui_toast(ui_ctx_t *ctx, const char *message, int32_t timeout_ms)
{
    ui_widget_t *w = ui_alloc_widget(ctx, UI_WIDGET_TOAST);
    if (!w)
        return NULL;

    w->toast.message = message;
    w->toast.timeout_ms = timeout_ms;
    w->toast.shown_ms = ui_current_time_ms();

    return w;
}

ui_widget_t *ui_menu(ui_ctx_t *ctx, const char **items, int32_t count,
                     ui_event_cb_t cb, void *ud)
{
    ui_widget_t *w = ui_alloc_widget(ctx, UI_WIDGET_MENU);
    if (!w)
        return NULL;

    w->menu.items = items;
    w->menu.count = count;
    w->menu.selected = -1;
    w->on_event = cb;
    w->userdata = ud;

    return w;
}

void ui_widget_emit_draw(ui_widget_t *w, vanilla_draw_cmd_array_t *out)
{
    if (!w || !w->layout_elem || !out)
        return;

    int32_t x = w->layout_elem->computed_x;
    int32_t y = w->layout_elem->computed_y;
    int32_t width = w->layout_elem->computed_w;
    int32_t height = w->layout_elem->computed_h;

    uint32_t bg_base = g_theme ? g_theme->bg_base : 0xFF2E3440u;
    uint32_t bg_elevated = g_theme ? g_theme->bg_elevated : 0xFF3B4252u;
    uint32_t bg_overlay = g_theme ? g_theme->bg_overlay : 0xCC1C2030u;
    uint32_t fg_primary = g_theme ? g_theme->fg_primary : 0xFFECEFF4u;
    uint32_t fg_muted = g_theme ? g_theme->fg_muted : 0xFFD8DEE9u;
    uint32_t border_col = g_theme ? g_theme->border : 0xFF4C566Au;
    uint32_t border_focus = g_theme ? g_theme->border_focus : 0xFF88C0D0u;
    uint32_t accent = g_theme ? g_theme->accent : 0xFF88C0D0u;
    uint32_t accent_pressed = g_theme ? g_theme->accent_pressed : 0xFF5E81ACu;
    uint32_t selection = g_theme ? g_theme->selection : 0x4488C0D0u;

    vanilla_draw_cmd_t cmd;

    switch (w->type) {
    case UI_WIDGET_BOX:
        if (w->layout_elem->bg_color != 0) {
            memset(&cmd, 0, sizeof(cmd));
            if (w->layout_elem->corner_radius > 0) {
                cmd.type = VCMD_ROUNDED_RECT;
                cmd.rounded_rect.color = w->layout_elem->bg_color;
                cmd.rounded_rect.radius = w->layout_elem->corner_radius;
            } else {
                cmd.type = VCMD_FILL_RECT;
                cmd.fill_rect.color = w->layout_elem->bg_color;
            }
            cmd.bounds.x = x;
            cmd.bounds.y = y;
            cmd.bounds.w = width;
            cmd.bounds.h = height;
            push_cmd(out, cmd);
        }
        if (w->layout_elem->border_width > 0 && w->layout_elem->border_color != 0) {
            memset(&cmd, 0, sizeof(cmd));
            cmd.type = VCMD_BORDER;
            cmd.bounds.x = x;
            cmd.bounds.y = y;
            cmd.bounds.w = width;
            cmd.bounds.h = height;
            cmd.border.color = w->layout_elem->border_color;
            cmd.border.width = w->layout_elem->border_width;
            push_cmd(out, cmd);
        }
        break;

    case UI_WIDGET_BUTTON: {
        uint32_t bg = w->button.pressed ? accent_pressed : bg_elevated;
        uint32_t bcol = w->focused ? border_focus : (w->button.pressed ? accent : border_col);

        memset(&cmd, 0, sizeof(cmd));
        cmd.type = VCMD_ROUNDED_RECT;
        cmd.bounds.x = x;
        cmd.bounds.y = y;
        cmd.bounds.w = width;
        cmd.bounds.h = height;
        cmd.rounded_rect.color = bg;
        cmd.rounded_rect.radius = 4;
        push_cmd(out, cmd);

        memset(&cmd, 0, sizeof(cmd));
        cmd.type = VCMD_BORDER;
        cmd.bounds.x = x;
        cmd.bounds.y = y;
        cmd.bounds.w = width;
        cmd.bounds.h = height;
        cmd.border.color = bcol;
        cmd.border.width = w->focused ? 2 : 1;
        push_cmd(out, cmd);

        if (w->button.label) {
            int len = (int)strlen(w->button.label);
            int tw = len * 8;
            int th = 8;
            memset(&cmd, 0, sizeof(cmd));
            cmd.type = VCMD_TEXT;
            cmd.bounds.x = x + (width - tw) / 2;
            cmd.bounds.y = y + (height - th) / 2;
            cmd.bounds.w = tw;
            cmd.bounds.h = th;
            cmd.text.text = w->button.label;
            cmd.text.color = fg_primary;
            cmd.text.font_size = 8.0f;
            push_cmd(out, cmd);
        }
        break;
    }

    case UI_WIDGET_LABEL:
        if (w->label.text) {
            float fsize = (w->layout_elem && w->layout_elem->font_size > 0.0f) ?
                          w->layout_elem->font_size : 8.0f;
            int scale = (int)fsize / 8;
            if (scale < 1) scale = 1;
            int len = (int)strlen(w->label.text);
            int tw = len * 8 * scale;
            int th = 8 * scale;
            memset(&cmd, 0, sizeof(cmd));
            cmd.type = VCMD_TEXT;
            cmd.bounds.x = x;
            cmd.bounds.y = y + (height > th ? (height - th) / 2 : 0);
            cmd.bounds.w = tw;
            cmd.bounds.h = th;
            cmd.text.text = w->label.text;
            cmd.text.color = w->label.color ? w->label.color : fg_primary;
            cmd.text.font_size = fsize;
            push_cmd(out, cmd);
        }
        break;

    case UI_WIDGET_TEXT_FIELD:
        ui_text_field_emit_draw(w, out);
        break;

    case UI_WIDGET_CHECKBOX: {
        int32_t bs = 16;
        int32_t by = y + (height - bs) / 2;
        memset(&cmd, 0, sizeof(cmd));
        cmd.type = VCMD_ROUNDED_RECT;
        cmd.bounds.x = x;
        cmd.bounds.y = by;
        cmd.bounds.w = bs;
        cmd.bounds.h = bs;
        cmd.rounded_rect.color = bg_elevated;
        cmd.rounded_rect.radius = 3;
        push_cmd(out, cmd);

        memset(&cmd, 0, sizeof(cmd));
        cmd.type = VCMD_BORDER;
        cmd.bounds.x = x;
        cmd.bounds.y = by;
        cmd.bounds.w = bs;
        cmd.bounds.h = bs;
        cmd.border.color = w->focused ? border_focus : border_col;
        cmd.border.width = 1;
        push_cmd(out, cmd);

        if (w->checkbox.checked) {
            memset(&cmd, 0, sizeof(cmd));
            cmd.type = VCMD_FILL_RECT;
            cmd.bounds.x = x + 3;
            cmd.bounds.y = by + 3;
            cmd.bounds.w = bs - 6;
            cmd.bounds.h = bs - 6;
            cmd.fill_rect.color = accent;
            push_cmd(out, cmd);
        }

        if (w->checkbox.label) {
            int len = (int)strlen(w->checkbox.label);
            memset(&cmd, 0, sizeof(cmd));
            cmd.type = VCMD_TEXT;
            cmd.bounds.x = x + bs + 8;
            cmd.bounds.y = y + (height - 8) / 2;
            cmd.bounds.w = len * 8;
            cmd.bounds.h = 8;
            cmd.text.text = w->checkbox.label;
            cmd.text.color = fg_primary;
            cmd.text.font_size = 8.0f;
            push_cmd(out, cmd);
        }
        break;
    }

    case UI_WIDGET_RADIO: {
        int32_t bs = 16;
        int32_t by = y + (height - bs) / 2;
        memset(&cmd, 0, sizeof(cmd));
        cmd.type = VCMD_ROUNDED_RECT;
        cmd.bounds.x = x;
        cmd.bounds.y = by;
        cmd.bounds.w = bs;
        cmd.bounds.h = bs;
        cmd.rounded_rect.color = bg_elevated;
        cmd.rounded_rect.radius = 8;
        push_cmd(out, cmd);

        memset(&cmd, 0, sizeof(cmd));
        cmd.type = VCMD_BORDER;
        cmd.bounds.x = x;
        cmd.bounds.y = by;
        cmd.bounds.w = bs;
        cmd.bounds.h = bs;
        cmd.border.color = w->focused ? border_focus : border_col;
        cmd.border.width = 1;
        push_cmd(out, cmd);

        if (w->radio.checked) {
            memset(&cmd, 0, sizeof(cmd));
            cmd.type = VCMD_ROUNDED_RECT;
            cmd.bounds.x = x + 4;
            cmd.bounds.y = by + 4;
            cmd.bounds.w = 8;
            cmd.bounds.h = 8;
            cmd.rounded_rect.color = accent;
            cmd.rounded_rect.radius = 4;
            push_cmd(out, cmd);
        }

        if (w->radio.label) {
            int len = (int)strlen(w->radio.label);
            memset(&cmd, 0, sizeof(cmd));
            cmd.type = VCMD_TEXT;
            cmd.bounds.x = x + bs + 8;
            cmd.bounds.y = y + (height - 8) / 2;
            cmd.bounds.w = len * 8;
            cmd.bounds.h = 8;
            cmd.text.text = w->radio.label;
            cmd.text.color = fg_primary;
            cmd.text.font_size = 8.0f;
            push_cmd(out, cmd);
        }
        break;
    }

    case UI_WIDGET_SLIDER: {
        int32_t th = 4;
        int32_t ty = y + (height - th) / 2;
        memset(&cmd, 0, sizeof(cmd));
        cmd.type = VCMD_ROUNDED_RECT;
        cmd.bounds.x = x;
        cmd.bounds.y = ty;
        cmd.bounds.w = width;
        cmd.bounds.h = th;
        cmd.rounded_rect.color = bg_overlay;
        cmd.rounded_rect.radius = 2;
        push_cmd(out, cmd);

        float range = (w->slider.max_val > w->slider.min_val) ?
                      (w->slider.max_val - w->slider.min_val) : 1.0f;
        float frac = (w->slider.value - w->slider.min_val) / range;
        if (frac < 0.0f) frac = 0.0f;
        if (frac > 1.0f) frac = 1.0f;

        int32_t thumb_x = x + (int32_t)(frac * (float)(width - 14));
        memset(&cmd, 0, sizeof(cmd));
        cmd.type = VCMD_ROUNDED_RECT;
        cmd.bounds.x = x;
        cmd.bounds.y = ty;
        cmd.bounds.w = (thumb_x - x) + 7;
        cmd.bounds.h = th;
        cmd.rounded_rect.color = accent;
        cmd.rounded_rect.radius = 2;
        push_cmd(out, cmd);

        int32_t thumb_y = y + (height - 14) / 2;
        memset(&cmd, 0, sizeof(cmd));
        cmd.type = VCMD_ROUNDED_RECT;
        cmd.bounds.x = thumb_x;
        cmd.bounds.y = thumb_y;
        cmd.bounds.w = 14;
        cmd.bounds.h = 14;
        cmd.rounded_rect.color = fg_primary;
        cmd.rounded_rect.radius = 7;
        push_cmd(out, cmd);

        memset(&cmd, 0, sizeof(cmd));
        cmd.type = VCMD_BORDER;
        cmd.bounds.x = thumb_x;
        cmd.bounds.y = thumb_y;
        cmd.bounds.w = 14;
        cmd.bounds.h = 14;
        cmd.border.color = w->focused ? border_focus : accent;
        cmd.border.width = 1;
        push_cmd(out, cmd);
        break;
    }

    case UI_WIDGET_LIST: {
        memset(&cmd, 0, sizeof(cmd));
        cmd.type = VCMD_ROUNDED_RECT;
        cmd.bounds.x = x;
        cmd.bounds.y = y;
        cmd.bounds.w = width;
        cmd.bounds.h = height;
        cmd.rounded_rect.color = bg_elevated;
        cmd.rounded_rect.radius = 4;
        push_cmd(out, cmd);

        memset(&cmd, 0, sizeof(cmd));
        cmd.type = VCMD_BORDER;
        cmd.bounds.x = x;
        cmd.bounds.y = y;
        cmd.bounds.w = width;
        cmd.bounds.h = height;
        cmd.border.color = w->focused ? border_focus : border_col;
        cmd.border.width = 1;
        push_cmd(out, cmd);

        memset(&cmd, 0, sizeof(cmd));
        cmd.type = VCMD_PUSH_CLIP;
        cmd.bounds.x = x + 1;
        cmd.bounds.y = y + 1;
        cmd.bounds.w = width - 2;
        cmd.bounds.h = height - 2;
        push_cmd(out, cmd);

        int32_t row_h = 24;
        for (int i = 0; i < w->list.count; i++) {
            int32_t ry = y + i * row_h - w->list.scroll_top;
            if (ry + row_h < y || ry > y + height)
                continue;

            if (i == w->list.selected) {
                memset(&cmd, 0, sizeof(cmd));
                cmd.type = VCMD_FILL_RECT;
                cmd.bounds.x = x + 2;
                cmd.bounds.y = ry + 1;
                cmd.bounds.w = width - 4;
                cmd.bounds.h = row_h - 2;
                cmd.fill_rect.color = selection;
                push_cmd(out, cmd);
            }

            if (w->list.items && w->list.items[i]) {
                int len = (int)strlen(w->list.items[i]);
                memset(&cmd, 0, sizeof(cmd));
                cmd.type = VCMD_TEXT;
                cmd.bounds.x = x + 8;
                cmd.bounds.y = ry + (row_h - 8) / 2;
                cmd.bounds.w = len * 8;
                cmd.bounds.h = 8;
                cmd.text.text = w->list.items[i];
                cmd.text.color = fg_primary;
                cmd.text.font_size = 8.0f;
                push_cmd(out, cmd);
            }
        }

        memset(&cmd, 0, sizeof(cmd));
        cmd.type = VCMD_POP_CLIP;
        push_cmd(out, cmd);
        break;
    }

    case UI_WIDGET_SCROLL_VIEW:
        break;

    case UI_WIDGET_TAB_BAR: {
        memset(&cmd, 0, sizeof(cmd));
        cmd.type = VCMD_FILL_RECT;
        cmd.bounds.x = x;
        cmd.bounds.y = y;
        cmd.bounds.w = width;
        cmd.bounds.h = height;
        cmd.fill_rect.color = bg_elevated;
        push_cmd(out, cmd);

        memset(&cmd, 0, sizeof(cmd));
        cmd.type = VCMD_FILL_RECT;
        cmd.bounds.x = x;
        cmd.bounds.y = y + height - 1;
        cmd.bounds.w = width;
        cmd.bounds.h = 1;
        cmd.fill_rect.color = border_col;
        push_cmd(out, cmd);

        int32_t count = w->tab_bar.tab_count > 0 ? w->tab_bar.tab_count : 1;
        int32_t tab_w = width / count;

        for (int i = 0; i < w->tab_bar.tab_count; i++) {
            int32_t tx = x + i * tab_w;
            if (i == w->tab_bar.active_tab) {
                memset(&cmd, 0, sizeof(cmd));
                cmd.type = VCMD_FILL_RECT;
                cmd.bounds.x = tx + 4;
                cmd.bounds.y = y + height - 3;
                cmd.bounds.w = tab_w - 8;
                cmd.bounds.h = 3;
                cmd.fill_rect.color = accent;
                push_cmd(out, cmd);
            }

            if (w->tab_bar.labels && w->tab_bar.labels[i]) {
                int len = (int)strlen(w->tab_bar.labels[i]);
                int tw = len * 8;
                memset(&cmd, 0, sizeof(cmd));
                cmd.type = VCMD_TEXT;
                cmd.bounds.x = tx + (tab_w - tw) / 2;
                cmd.bounds.y = y + (height - 8) / 2;
                cmd.bounds.w = tw;
                cmd.bounds.h = 8;
                cmd.text.text = w->tab_bar.labels[i];
                cmd.text.color = (i == w->tab_bar.active_tab) ? accent : fg_muted;
                cmd.text.font_size = 8.0f;
                push_cmd(out, cmd);
            }
        }
        break;
    }

    case UI_WIDGET_MODAL_DIALOG: {
        memset(&cmd, 0, sizeof(cmd));
        cmd.type = VCMD_FILL_RECT;
        cmd.bounds.x = x;
        cmd.bounds.y = y;
        cmd.bounds.w = width;
        cmd.bounds.h = height;
        cmd.fill_rect.color = bg_overlay;
        push_cmd(out, cmd);

        int32_t cw = 360;
        int32_t ch = 180;
        int32_t cx = x + (width - cw) / 2;
        int32_t cy = y + (height - ch) / 2;

        memset(&cmd, 0, sizeof(cmd));
        cmd.type = VCMD_ROUNDED_RECT;
        cmd.bounds.x = cx;
        cmd.bounds.y = cy;
        cmd.bounds.w = cw;
        cmd.bounds.h = ch;
        cmd.rounded_rect.color = bg_elevated;
        cmd.rounded_rect.radius = 8;
        push_cmd(out, cmd);

        memset(&cmd, 0, sizeof(cmd));
        cmd.type = VCMD_BORDER;
        cmd.bounds.x = cx;
        cmd.bounds.y = cy;
        cmd.bounds.w = cw;
        cmd.bounds.h = ch;
        cmd.border.color = border_col;
        cmd.border.width = 1;
        push_cmd(out, cmd);

        if (w->modal.title) {
            memset(&cmd, 0, sizeof(cmd));
            cmd.type = VCMD_TEXT;
            cmd.bounds.x = cx + 16;
            cmd.bounds.y = cy + 16;
            cmd.bounds.w = (int)strlen(w->modal.title) * 8;
            cmd.bounds.h = 8;
            cmd.text.text = w->modal.title;
            cmd.text.color = fg_primary;
            cmd.text.font_size = 8.0f;
            push_cmd(out, cmd);
        }

        if (w->modal.body) {
            memset(&cmd, 0, sizeof(cmd));
            cmd.type = VCMD_TEXT;
            cmd.bounds.x = cx + 16;
            cmd.bounds.y = cy + 44;
            cmd.bounds.w = (int)strlen(w->modal.body) * 8;
            cmd.bounds.h = 8;
            cmd.text.text = w->modal.body;
            cmd.text.color = fg_muted;
            cmd.text.font_size = 8.0f;
            push_cmd(out, cmd);
        }

        const char *btn_txt = w->modal.ok_label ? w->modal.ok_label : "OK";
        int32_t btn_x = cx + cw - 96;
        int32_t btn_y = cy + ch - 40;

        memset(&cmd, 0, sizeof(cmd));
        cmd.type = VCMD_ROUNDED_RECT;
        cmd.bounds.x = btn_x;
        cmd.bounds.y = btn_y;
        cmd.bounds.w = 80;
        cmd.bounds.h = 28;
        cmd.rounded_rect.color = accent;
        cmd.rounded_rect.radius = 4;
        push_cmd(out, cmd);

        int len = (int)strlen(btn_txt);
        memset(&cmd, 0, sizeof(cmd));
        cmd.type = VCMD_TEXT;
        cmd.bounds.x = btn_x + (80 - len * 8) / 2;
        cmd.bounds.y = btn_y + (28 - 8) / 2;
        cmd.bounds.w = len * 8;
        cmd.bounds.h = 8;
        cmd.text.text = btn_txt;
        cmd.text.color = bg_base;
        cmd.text.font_size = 8.0f;
        push_cmd(out, cmd);
        break;
    }

    case UI_WIDGET_TOAST: {
        int64_t now = ui_current_time_ms();
        if (now < w->toast.shown_ms + w->toast.timeout_ms) {
            int32_t tw = 280;
            int32_t th = 36;
            int32_t tx = x + (width - tw) / 2;
            int32_t ty = y + height - th - 16;

            memset(&cmd, 0, sizeof(cmd));
            cmd.type = VCMD_ROUNDED_RECT;
            cmd.bounds.x = tx;
            cmd.bounds.y = ty;
            cmd.bounds.w = tw;
            cmd.bounds.h = th;
            cmd.rounded_rect.color = bg_elevated;
            cmd.rounded_rect.radius = 18;
            push_cmd(out, cmd);

            memset(&cmd, 0, sizeof(cmd));
            cmd.type = VCMD_BORDER;
            cmd.bounds.x = tx;
            cmd.bounds.y = ty;
            cmd.bounds.w = tw;
            cmd.bounds.h = th;
            cmd.border.color = border_col;
            cmd.border.width = 1;
            push_cmd(out, cmd);

            if (w->toast.message) {
                int len = (int)strlen(w->toast.message);
                int mw = len * 8;
                memset(&cmd, 0, sizeof(cmd));
                cmd.type = VCMD_TEXT;
                cmd.bounds.x = tx + (tw - mw) / 2;
                cmd.bounds.y = ty + (th - 8) / 2;
                cmd.bounds.w = mw;
                cmd.bounds.h = 8;
                cmd.text.text = w->toast.message;
                cmd.text.color = fg_primary;
                cmd.text.font_size = 8.0f;
                push_cmd(out, cmd);
            }
        }
        break;
    }

    case UI_WIDGET_MENU: {
        memset(&cmd, 0, sizeof(cmd));
        cmd.type = VCMD_ROUNDED_RECT;
        cmd.bounds.x = x;
        cmd.bounds.y = y;
        cmd.bounds.w = width;
        cmd.bounds.h = height;
        cmd.rounded_rect.color = bg_elevated;
        cmd.rounded_rect.radius = 4;
        push_cmd(out, cmd);

        memset(&cmd, 0, sizeof(cmd));
        cmd.type = VCMD_BORDER;
        cmd.bounds.x = x;
        cmd.bounds.y = y;
        cmd.bounds.w = width;
        cmd.bounds.h = height;
        cmd.border.color = border_col;
        cmd.border.width = 1;
        push_cmd(out, cmd);

        int32_t row_h = 24;
        for (int i = 0; i < w->menu.count; i++) {
            int32_t ry = y + i * row_h;
            if (w->menu.items && w->menu.items[i]) {
                int len = (int)strlen(w->menu.items[i]);
                memset(&cmd, 0, sizeof(cmd));
                cmd.type = VCMD_TEXT;
                cmd.bounds.x = x + 8;
                cmd.bounds.y = ry + (row_h - 8) / 2;
                cmd.bounds.w = len * 8;
                cmd.bounds.h = 8;
                cmd.text.text = w->menu.items[i];
                cmd.text.color = fg_primary;
                cmd.text.font_size = 8.0f;
                push_cmd(out, cmd);
            }
        }
        break;
    }
    }
}
