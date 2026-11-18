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
#include <stdio.h>
#include <string.h>

static inline void push_cmd(vanilla_draw_cmd_array_t *out, vanilla_draw_cmd_t cmd)
{
    if (out && out->cmds && out->count < out->capacity)
        out->cmds[out->count++] = cmd;
}

static void push_fill_rect(vanilla_draw_cmd_array_t *out,
                           int32_t x, int32_t y, int32_t w, int32_t h, uint32_t color)
{
    if (w <= 0 || h <= 0)
        return;
    vanilla_draw_cmd_t cmd;
    memset(&cmd, 0, sizeof(cmd));
    cmd.type = VCMD_FILL_RECT;
    cmd.bounds.x = x;
    cmd.bounds.y = y;
    cmd.bounds.w = w;
    cmd.bounds.h = h;
    cmd.fill_rect.color = color;
    push_cmd(out, cmd);
}

static void push_rounded_rect(vanilla_draw_cmd_array_t *out,
                              int32_t x, int32_t y, int32_t w, int32_t h,
                              int32_t radius, uint32_t color)
{
    if (w <= 0 || h <= 0)
        return;
    vanilla_draw_cmd_t cmd;
    memset(&cmd, 0, sizeof(cmd));
    if (radius > 0) {
        cmd.type = VCMD_ROUNDED_RECT;
        cmd.rounded_rect.color = color;
        cmd.rounded_rect.radius = radius;
    } else {
        cmd.type = VCMD_FILL_RECT;
        cmd.fill_rect.color = color;
    }
    cmd.bounds.x = x;
    cmd.bounds.y = y;
    cmd.bounds.w = w;
    cmd.bounds.h = h;
    push_cmd(out, cmd);
}

static void push_border(vanilla_draw_cmd_array_t *out,
                        int32_t x, int32_t y, int32_t w, int32_t h,
                        int32_t bw, uint32_t color)
{
    if (w <= 0 || h <= 0 || bw <= 0)
        return;
    vanilla_draw_cmd_t cmd;
    memset(&cmd, 0, sizeof(cmd));
    cmd.type = VCMD_BORDER;
    cmd.bounds.x = x;
    cmd.bounds.y = y;
    cmd.bounds.w = w;
    cmd.bounds.h = h;
    cmd.border.color = color;
    cmd.border.width = bw;
    push_cmd(out, cmd);
}

static void push_text(vanilla_draw_cmd_array_t *out,
                      int32_t x, int32_t y, const char *text,
                      float fsize, app_font_weight_t weight, uint32_t color)
{
    if (!text || text[0] == '\0')
        return;
    float eff = (fsize > 0.0f) ? fsize : app_default_font_size();
    int32_t tw = 0;
    int32_t th = 0;
    app_measure_text_ex(text, eff, weight, &tw, &th);
    if (tw <= 0) tw = 1;
    if (th <= 0) th = (int32_t)eff;

    vanilla_draw_cmd_t cmd;
    memset(&cmd, 0, sizeof(cmd));
    cmd.type = VCMD_TEXT;
    cmd.bounds.x = x;
    cmd.bounds.y = y;
    cmd.bounds.w = tw;
    cmd.bounds.h = th;
    cmd.text.text = text;
    cmd.text.color = color;
    cmd.text.font_size = eff;
    cmd.text.font_id = 0;
    cmd.text.font_weight = (int32_t)weight;
    push_cmd(out, cmd);
}

void ui_scrollbar_metrics(int32_t x, int32_t y, int32_t w, int32_t h,
                          int32_t content_h, int32_t scroll_y,
                          int32_t *track_x, int32_t *track_y,
                          int32_t *track_w, int32_t *track_h,
                          int32_t *thumb_y, int32_t *thumb_h,
                          int32_t *max_scroll)
{
    int32_t tx = x + w - UI_SCROLLBAR_W;
    int32_t ty = y + 1;
    int32_t tw = UI_SCROLLBAR_W - 2;
    int32_t th = (h > 2) ? (h - 2) : 0;
    int32_t ms = (content_h > h) ? (content_h - h) : 0;
    int32_t sy = scroll_y;
    if (sy < 0) sy = 0;
    if (sy > ms) sy = ms;

    int32_t bh = (ms > 0 && th > 0 && content_h > 0) ? (th * h) / content_h : th;
    if (bh < 18) bh = 18;
    if (bh > th) bh = th;

    int32_t travel = th - bh;
    int32_t by = ty + ((ms > 0 && travel > 0) ? (sy * travel) / ms : 0);

    if (track_x) *track_x = tx;
    if (track_y) *track_y = ty;
    if (track_w) *track_w = tw;
    if (track_h) *track_h = th;
    if (thumb_y) *thumb_y = by;
    if (thumb_h) *thumb_h = bh;
    if (max_scroll) *max_scroll = ms;
}

int32_t ui_scroll_view_effective_content_h(const ui_widget_t *w)
{
    if (!w || w->type != UI_WIDGET_SCROLL_VIEW)
        return 0;

    int32_t child_h = 0;
    if (w->layout_elem) {
        int32_t count = 0;
        int32_t accum = 0;
        int32_t max_h = 0;
        for (const ui_widget_t *c = w->first_child; c; c = c->next_sibling) {
            if (c->hidden || !c->layout_elem)
                continue;
            int32_t ch = c->layout_elem->computed_h;
            if (ch <= 0 && c->layout_elem->h_mode == VSIZE_FIXED)
                ch = c->layout_elem->h_px;
            accum += ch;
            if (ch > max_h)
                max_h = ch;
            count++;
        }
        if (w->layout_elem->direction == VDIR_ROW)
            child_h = max_h;
        else
            child_h = accum + ((count > 1) ? (count - 1) * w->layout_elem->gap : 0);
        child_h += w->layout_elem->pad_top + w->layout_elem->pad_bottom;
    }

    return (child_h > w->scroll_view.content_h) ? child_h : w->scroll_view.content_h;
}

int32_t ui_menu_is_separator(const ui_widget_t *w, int32_t idx)
{
    if (!w || w->type != UI_WIDGET_MENU || idx < 0 || idx >= w->menu.count)
        return 0;
    if (w->menu.rich_items)
        return (w->menu.rich_items[idx].flags & UI_MENU_ITEM_SEPARATOR) != 0;
    if (w->menu.items && w->menu.items[idx]) {
        const char *s = w->menu.items[idx];
        return (strcmp(s, "---") == 0 || strcmp(s, "-") == 0);
    }
    return 0;
}

int32_t ui_menu_is_disabled(const ui_widget_t *w, int32_t idx)
{
    if (!w || w->type != UI_WIDGET_MENU || idx < 0 || idx >= w->menu.count)
        return 1;
    if (ui_menu_is_separator(w, idx))
        return 1;
    if (w->menu.rich_items)
        return (w->menu.rich_items[idx].flags & UI_MENU_ITEM_DISABLED) != 0;
    return 0;
}

void ui_menu_row_rect(const ui_widget_t *w, int32_t idx, int32_t *out_y, int32_t *out_h)
{
    int32_t base_y = (w && w->layout_elem) ? w->layout_elem->computed_y : 0;
    int32_t cur_y = base_y;
    int32_t rh = UI_MENU_ROW_H;

    for (int32_t i = 0; i < idx && w && i < w->menu.count; i++) {
        cur_y += ui_menu_is_separator(w, i) ? UI_MENU_SEP_H : UI_MENU_ROW_H;
    }
    if (w && idx >= 0 && idx < w->menu.count)
        rh = ui_menu_is_separator(w, idx) ? UI_MENU_SEP_H : UI_MENU_ROW_H;

    if (out_y) *out_y = cur_y;
    if (out_h) *out_h = rh;
}

int32_t ui_menu_hit_item(const ui_widget_t *w, int32_t mx, int32_t my)
{
    if (!w || w->type != UI_WIDGET_MENU || !w->layout_elem)
        return -1;

    int32_t bx = w->layout_elem->computed_x;
    int32_t by = w->layout_elem->computed_y;
    int32_t bw = w->layout_elem->computed_w;
    int32_t bh = w->layout_elem->computed_h;
    if (mx < bx || mx >= bx + bw || my < by || my >= by + bh)
        return -1;

    if (w->menu.is_bar) {
        float fsize = app_default_font_size();
        int32_t cur_x = bx + 4;
        for (int32_t i = 0; i < w->menu.count; i++) {
            const char *lbl = w->menu.rich_items ? w->menu.rich_items[i].label
                                                 : (w->menu.items ? w->menu.items[i] : NULL);
            int32_t tw = 0;
            if (lbl)
                app_measure_text_ex(lbl, fsize, APP_FONT_MEDIUM, &tw, NULL);
            int32_t iw = tw + 20;
            if (mx >= cur_x && mx < cur_x + iw)
                return i;
            cur_x += iw;
        }
        return -1;
    }

    int32_t cur_y = by;
    for (int32_t i = 0; i < w->menu.count; i++) {
        int32_t rh = ui_menu_is_separator(w, i) ? UI_MENU_SEP_H : UI_MENU_ROW_H;
        if (my >= cur_y && my < cur_y + rh)
            return i;
        cur_y += rh;
    }
    return -1;
}

void ui_modal_compute_layout(const ui_widget_t *w, int32_t *cx, int32_t *cy,
                             int32_t *cw, int32_t *ch)
{
    int32_t bx = (w && w->layout_elem) ? w->layout_elem->computed_x : 0;
    int32_t by = (w && w->layout_elem) ? w->layout_elem->computed_y : 0;
    int32_t bw = (w && w->layout_elem) ? w->layout_elem->computed_w : 360;
    int32_t bh = (w && w->layout_elem) ? w->layout_elem->computed_h : 180;

    int32_t card_w = (bw >= 360 || bw <= 0) ? 360 : (bw > 220 ? bw - 24 : bw);
    int32_t card_h = (bh >= 180 || bh <= 0) ? 180 : (bh > 120 ? bh - 24 : bh);
    int32_t card_x = bx + (bw - card_w) / 2;
    int32_t card_y = by + (bh - card_h) / 2;

    if (cx) *cx = card_x;
    if (cy) *cy = card_y;
    if (cw) *cw = card_w;
    if (ch) *ch = card_h;
}

static void menu_recompute_bounds(ui_widget_t *w)
{
    if (!w || !w->layout_elem || w->menu.is_bar)
        return;

    float fsize = app_default_font_size();
    int32_t max_label_w = 0;
    int32_t max_sc_w = 0;
    int32_t has_check = 0;
    int32_t has_sub = 0;
    int32_t total_h = 0;

    for (int32_t i = 0; i < w->menu.count; i++) {
        if (ui_menu_is_separator(w, i)) {
            total_h += UI_MENU_SEP_H;
            continue;
        }
        total_h += UI_MENU_ROW_H;

        const char *lbl = NULL;
        const char *sc = NULL;
        if (w->menu.rich_items) {
            lbl = w->menu.rich_items[i].label;
            sc = w->menu.rich_items[i].shortcut;
            if (w->menu.rich_items[i].flags & UI_MENU_ITEM_CHECKED)
                has_check = 1;
            if (w->menu.rich_items[i].flags & UI_MENU_ITEM_SUBMENU)
                has_sub = 1;
        } else if (w->menu.items) {
            lbl = w->menu.items[i];
        }

        if (lbl && lbl[0] != '\0') {
            int32_t lw = 0;
            app_measure_text_ex(lbl, fsize, APP_FONT_REGULAR, &lw, NULL);
            if (lw > max_label_w)
                max_label_w = lw;
        }
        if (sc && sc[0] != '\0') {
            int32_t sw = 0;
            app_measure_text_ex(sc, fsize, APP_FONT_REGULAR, &sw, NULL);
            if (sw > max_sc_w)
                max_sc_w = sw;
        }
    }

    int32_t left_pad = has_check ? 24 : 12;
    int32_t right_pad = has_sub ? 24 : 14;
    int32_t sc_pad = (max_sc_w > 0) ? (20 + max_sc_w) : 0;
    int32_t menu_w = left_pad + max_label_w + sc_pad + right_pad;
    if (menu_w < 140)
        menu_w = 140;

    w->layout_elem->w_mode = VSIZE_FIXED;
    w->layout_elem->w_px = menu_w;
    w->layout_elem->h_mode = VSIZE_FIXED;
    w->layout_elem->h_px = total_h > 0 ? total_h : UI_MENU_ROW_H;
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
    w->button.hovered = 0;
    w->on_event = cb;
    w->userdata = ud;

    if (w->layout_elem && label) {
        int32_t tw = 0;
        int32_t th = 0;
        app_measure_text_ex(label, app_default_font_size(), APP_FONT_MEDIUM, &tw, &th);
        int32_t calc_w = tw + w->layout_elem->pad_left + w->layout_elem->pad_right;
        w->layout_elem->w_mode = VSIZE_FIXED;
        w->layout_elem->w_px = calc_w > 64 ? calc_w : 64;
        int32_t min_h = th + w->layout_elem->pad_top + w->layout_elem->pad_bottom;
        if (min_h > w->layout_elem->h_px)
            w->layout_elem->h_px = min_h;
    }

    return w;
}

ui_widget_t *ui_label(ui_ctx_t *ctx, const char *text, uint32_t color)
{
    return ui_label_ex(ctx, text, color, 0.0f, APP_FONT_REGULAR);
}

ui_widget_t *ui_label_ex(ui_ctx_t *ctx, const char *text, uint32_t color,
                         float font_size, int32_t font_weight)
{
    ui_widget_t *w = ui_alloc_widget(ctx, UI_WIDGET_LABEL);
    if (!w)
        return NULL;

    w->label.text = text;
    w->label.color = color;

    if (w->layout_elem) {
        w->layout_elem->text = text;
        w->layout_elem->text_color = color;
        w->layout_elem->font_size = font_size;
        w->layout_elem->font_weight = font_weight;
    }

    return w;
}

void ui_label_set_text(ui_widget_t *w, const char *text)
{
    if (!w || w->type != UI_WIDGET_LABEL)
        return;

    w->label.text = text;
    if (w->layout_elem)
        w->layout_elem->text = text;
    ui_widget_invalidate(w);
}

void ui_widget_set_font(ui_widget_t *w, float font_size, int32_t font_weight)
{
    if (!w)
        return;

    if (w->layout_elem) {
        w->layout_elem->font_size = font_size;
        w->layout_elem->font_weight = font_weight;
    }
    if (w->type == UI_WIDGET_TEXT_FIELD)
        w->text_field.font_size = font_size;
    ui_widget_invalidate(w);
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
        int32_t tw = 0;
        int32_t th = 0;
        if (label)
            app_measure_text_ex(label, app_default_font_size(), APP_FONT_REGULAR, &tw, &th);
        w->layout_elem->w_mode = VSIZE_FIXED;
        w->layout_elem->w_px = 16 + 8 + tw + 8;
        if (th + 8 > w->layout_elem->h_px)
            w->layout_elem->h_px = th + 8;
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
        int32_t tw = 0;
        int32_t th = 0;
        if (label)
            app_measure_text_ex(label, app_default_font_size(), APP_FONT_REGULAR, &tw, &th);
        w->layout_elem->w_mode = VSIZE_FIXED;
        w->layout_elem->w_px = 16 + 8 + tw + 8;
        if (th + 8 > w->layout_elem->h_px)
            w->layout_elem->h_px = th + 8;
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
    w->list.hovered = -1;
    w->list.sb_dragging = 0;
    w->on_event = cb;
    w->userdata = ud;

    return w;
}

void ui_list_set_items(ui_widget_t *w, const char **items, int32_t count)
{
    if (!w || w->type != UI_WIDGET_LIST)
        return;

    w->list.items = items;
    w->list.count = count > 0 ? count : 0;
    if (w->list.selected >= w->list.count)
        w->list.selected = w->list.count > 0 ? w->list.count - 1 : 0;
    if (w->list.selected < 0)
        w->list.selected = 0;
    ui_list_scroll_to_selected(w);
    ui_widget_invalidate(w);
}

void ui_list_scroll_to_selected(ui_widget_t *w)
{
    if (!w || w->type != UI_WIDGET_LIST)
        return;

    int32_t view_h = (w->layout_elem && w->layout_elem->computed_h > 2)
                     ? (w->layout_elem->computed_h - 2)
                     : 200;
    int32_t total_h = w->list.count * UI_LIST_ROW_H;
    int32_t max_scroll = (total_h > view_h) ? (total_h - view_h) : 0;

    if (w->list.count > 0 && w->list.selected >= 0 && w->list.selected < w->list.count) {
        int32_t row_top = w->list.selected * UI_LIST_ROW_H;
        int32_t row_bot = row_top + UI_LIST_ROW_H;
        if (row_top < w->list.scroll_top)
            w->list.scroll_top = row_top;
        if (row_bot > w->list.scroll_top + view_h)
            w->list.scroll_top = row_bot - view_h;
    }

    if (w->list.scroll_top < 0)
        w->list.scroll_top = 0;
    if (w->list.scroll_top > max_scroll)
        w->list.scroll_top = max_scroll;
}

ui_widget_t *ui_scroll_view(ui_ctx_t *ctx)
{
    ui_widget_t *w = ui_alloc_widget(ctx, UI_WIDGET_SCROLL_VIEW);
    if (!w)
        return NULL;

    w->scroll_view.scroll_y = 0;
    w->scroll_view.content_h = 0;
    w->scroll_view.sb_dragging = 0;

    return w;
}

void ui_scroll_view_scroll_to(ui_widget_t *w, int32_t scroll_y)
{
    if (!w || w->type != UI_WIDGET_SCROLL_VIEW)
        return;

    int32_t eff_ch = ui_scroll_view_effective_content_h(w);
    int32_t vh = w->layout_elem ? w->layout_elem->computed_h : 0;
    int32_t max_scroll = (eff_ch > vh) ? (eff_ch - vh) : 0;
    if (scroll_y < 0)
        scroll_y = 0;
    if (scroll_y > max_scroll)
        scroll_y = max_scroll;
    w->scroll_view.scroll_y = scroll_y;
    ui_widget_invalidate(w);
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
    w->tab_bar.hovered_tab = -1;
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
    w->modal.cancel_label = NULL;
    w->modal.kind = UI_DIALOG_INFO;
    w->modal.result = UI_DIALOG_RES_NONE;
    w->modal.input_field = NULL;
    w->modal.hovered_btn = 0;
    w->modal.pressed_btn = 0;
    w->on_event = cb;
    w->userdata = ud;

    return w;
}

ui_widget_t *ui_dialog_confirm(ui_ctx_t *ctx, const char *title, const char *body,
                               const char *ok_label, const char *cancel_label,
                               ui_event_cb_t cb, void *ud)
{
    ui_widget_t *w = ui_modal_dialog(ctx, title, body, ok_label ? ok_label : "OK", cb, ud);
    if (!w)
        return NULL;

    w->modal.kind = UI_DIALOG_CONFIRM;
    w->modal.cancel_label = cancel_label ? cancel_label : "Cancel";
    return w;
}

ui_widget_t *ui_dialog_prompt(ui_ctx_t *ctx, const char *title, const char *body,
                              const char *initial_input, const char *ok_label,
                              const char *cancel_label, ui_event_cb_t cb, void *ud)
{
    ui_widget_t *w = ui_modal_dialog(ctx, title, body, ok_label ? ok_label : "OK", cb, ud);
    if (!w)
        return NULL;

    w->modal.kind = UI_DIALOG_INPUT;
    w->modal.cancel_label = cancel_label ? cancel_label : "Cancel";
    w->modal.input_field = ui_text_field(ctx, initial_input, NULL, NULL);
    if (w->modal.input_field)
        ui_widget_add_child(w, w->modal.input_field);
    return w;
}

const char *ui_dialog_get_input(const ui_widget_t *dlg)
{
    if (!dlg || dlg->type != UI_WIDGET_MODAL_DIALOG || !dlg->modal.input_field)
        return "";
    return ui_text_field_get_text(dlg->modal.input_field);
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
    w->menu.rich_items = NULL;
    w->menu.count = count;
    w->menu.selected = -1;
    w->menu.hovered = -1;
    w->menu.is_bar = 0;
    w->on_event = cb;
    w->userdata = ud;

    menu_recompute_bounds(w);
    return w;
}

ui_widget_t *ui_menu_ex(ui_ctx_t *ctx, const ui_menu_item_t *items, int32_t count,
                        ui_event_cb_t cb, void *ud)
{
    ui_widget_t *w = ui_alloc_widget(ctx, UI_WIDGET_MENU);
    if (!w)
        return NULL;

    w->menu.items = NULL;
    w->menu.rich_items = items;
    w->menu.count = count;
    w->menu.selected = -1;
    w->menu.hovered = -1;
    w->menu.is_bar = 0;
    w->on_event = cb;
    w->userdata = ud;

    menu_recompute_bounds(w);
    return w;
}

ui_widget_t *ui_menubar(ui_ctx_t *ctx, const char **labels, int32_t count,
                        ui_event_cb_t cb, void *ud)
{
    ui_widget_t *w = ui_alloc_widget(ctx, UI_WIDGET_MENU);
    if (!w)
        return NULL;

    w->menu.items = labels;
    w->menu.rich_items = NULL;
    w->menu.count = count;
    w->menu.selected = -1;
    w->menu.hovered = -1;
    w->menu.is_bar = 1;
    w->on_event = cb;
    w->userdata = ud;

    if (w->layout_elem) {
        w->layout_elem->w_mode = VSIZE_GROW;
        w->layout_elem->h_mode = VSIZE_FIXED;
        w->layout_elem->h_px = 28;
        w->layout_elem->corner_radius = 0;
    }
    return w;
}

void ui_menu_set_rich_items(ui_widget_t *w, const ui_menu_item_t *items, int32_t count)
{
    if (!w || w->type != UI_WIDGET_MENU)
        return;

    w->menu.items = NULL;
    w->menu.rich_items = items;
    w->menu.count = count;
    w->menu.selected = -1;
    w->menu.hovered = -1;
    menu_recompute_bounds(w);
    ui_widget_invalidate(w);
}

int32_t ui_menubar_item_x(const ui_widget_t *bar, int32_t idx)
{
    if (!bar || bar->type != UI_WIDGET_MENU || !bar->menu.is_bar || !bar->layout_elem)
        return 0;

    float fsize = app_default_font_size();
    int32_t cur_x = bar->layout_elem->computed_x + 4;
    for (int32_t i = 0; i < idx && i < bar->menu.count; i++) {
        const char *lbl = bar->menu.rich_items ? bar->menu.rich_items[i].label
                                               : (bar->menu.items ? bar->menu.items[i] : NULL);
        int32_t tw = 0;
        if (lbl)
            app_measure_text_ex(lbl, fsize, APP_FONT_MEDIUM, &tw, NULL);
        cur_x += tw + 20;
    }
    return cur_x;
}

static void progress_bar_format_label(ui_widget_t *w)
{
    if (!w || w->type != UI_WIDGET_PROGRESS_BAR)
        return;

    float max_v = (w->progress_bar.max_val > 0.0f) ? w->progress_bar.max_val : 100.0f;
    float frac = w->progress_bar.value / max_v;
    if (frac < 0.0f) frac = 0.0f;
    if (frac > 1.0f) frac = 1.0f;
    int pct = (int)(frac * 100.0f + 0.5f);
    snprintf(w->progress_bar.label_buf, sizeof(w->progress_bar.label_buf), "%d%%", pct);
}

ui_widget_t *ui_progress_bar(ui_ctx_t *ctx, float value, float max_val,
                             uint32_t bar_color, int32_t show_label)
{
    ui_widget_t *w = ui_alloc_widget(ctx, UI_WIDGET_PROGRESS_BAR);
    if (!w)
        return NULL;

    w->progress_bar.max_val = (max_val > 0.0f) ? max_val : 100.0f;
    w->progress_bar.value = value;
    if (w->progress_bar.value < 0.0f) w->progress_bar.value = 0.0f;
    if (w->progress_bar.value > w->progress_bar.max_val)
        w->progress_bar.value = w->progress_bar.max_val;
    w->progress_bar.bar_color = bar_color;
    w->progress_bar.show_label = show_label ? 1 : 0;
    progress_bar_format_label(w);
    return w;
}

ui_widget_t *ui_progress_bar_create(ui_ctx_t *ctx, float value)
{
    return ui_progress_bar(ctx, value, 100.0f, 0, 1);
}

void ui_progress_bar_set(ui_widget_t *w, float value)
{
    if (!w || w->type != UI_WIDGET_PROGRESS_BAR)
        return;

    float max_v = (w->progress_bar.max_val > 0.0f) ? w->progress_bar.max_val : 100.0f;
    if (value < 0.0f) value = 0.0f;
    if (value > max_v) value = max_v;
    w->progress_bar.value = value;
    progress_bar_format_label(w);
    ui_widget_invalidate(w);
}

void ui_progress_bar_set_color(ui_widget_t *w, uint32_t bar_color)
{
    if (!w || w->type != UI_WIDGET_PROGRESS_BAR)
        return;
    w->progress_bar.bar_color = bar_color;
    ui_widget_invalidate(w);
}

static void emit_scrollbar(vanilla_draw_cmd_array_t *out,
                           int32_t x, int32_t y, int32_t w, int32_t h,
                           int32_t content_h, int32_t scroll_y, int32_t dragging,
                           uint32_t track_col, uint32_t thumb_col, uint32_t active_col)
{
    if (content_h <= h || h <= 16 || w <= UI_SCROLLBAR_W + 4)
        return;

    int32_t tx = 0, ty = 0, tw = 0, th = 0, by = 0, bh = 0;
    ui_scrollbar_metrics(x, y, w, h, content_h, scroll_y,
                         &tx, &ty, &tw, &th, &by, &bh, NULL);

    push_fill_rect(out, tx, ty, tw, th, track_col);
    push_rounded_rect(out, tx + 1, by, tw > 2 ? tw - 2 : tw, bh, 3,
                      dragging ? active_col : thumb_col);
}

void ui_widget_emit_scroll_overlay(ui_widget_t *w, vanilla_draw_cmd_array_t *out)
{
    if (!w || !w->layout_elem || !out || w->hidden || w->type != UI_WIDGET_SCROLL_VIEW)
        return;

    int32_t x = w->layout_elem->computed_x;
    int32_t y = w->layout_elem->computed_y;
    int32_t width = w->layout_elem->computed_w;
    int32_t height = w->layout_elem->computed_h;
    int32_t eff_ch = ui_scroll_view_effective_content_h(w);

    uint32_t bg_base = g_theme ? g_theme->bg_base : 0xFF2E3440u;
    uint32_t fg_dim = g_theme ? g_theme->fg_dim : 0xFF98A2B3u;
    uint32_t accent = g_theme ? g_theme->accent : 0xFF88C0D0u;

    emit_scrollbar(out, x, y, width, height, eff_ch, w->scroll_view.scroll_y,
                   w->scroll_view.sb_dragging, bg_base, fg_dim, accent);
}

void ui_widget_emit_draw(ui_widget_t *w, vanilla_draw_cmd_array_t *out)
{
    if (!w || !w->layout_elem || !out || w->hidden)
        return;

    int32_t x = w->layout_elem->computed_x;
    int32_t y = w->layout_elem->computed_y;
    int32_t width = w->layout_elem->computed_w;
    int32_t height = w->layout_elem->computed_h;

    if (width <= 0 || height <= 0)
        return;

    uint32_t bg_base = g_theme ? g_theme->bg_base : 0xFF2E3440u;
    uint32_t bg_elevated = g_theme ? g_theme->bg_elevated : 0xFF3B4252u;
    uint32_t bg_overlay = g_theme ? g_theme->bg_overlay : 0xCC1C2030u;
    uint32_t fg_primary = g_theme ? g_theme->fg_primary : 0xFFECEFF4u;
    uint32_t fg_muted = g_theme ? g_theme->fg_muted : 0xFFD8DEE9u;
    uint32_t fg_dim = g_theme ? g_theme->fg_dim : 0xFF98A2B3u;
    uint32_t border_col = g_theme ? g_theme->border : 0xFF4C566Au;
    uint32_t border_focus = g_theme ? g_theme->border_focus : 0xFF88C0D0u;
    uint32_t accent = g_theme ? g_theme->accent : 0xFF88C0D0u;
    uint32_t accent_hover = g_theme ? g_theme->accent_hover : 0xFF81A1C1u;
    uint32_t accent_pressed = g_theme ? g_theme->accent_pressed : 0xFF5E81ACu;
    uint32_t selection = g_theme ? g_theme->selection : 0x4488C0D0u;
    float fsize_default = app_default_font_size();

    switch (w->type) {
    case UI_WIDGET_BOX:
        if (w->layout_elem->bg_color != 0) {
            push_rounded_rect(out, x, y, width, height,
                              w->layout_elem->corner_radius, w->layout_elem->bg_color);
        }
        if (w->layout_elem->border_width > 0 && w->layout_elem->border_color != 0) {
            push_border(out, x, y, width, height,
                        w->layout_elem->border_width, w->layout_elem->border_color);
        }
        break;

    case UI_WIDGET_BUTTON: {
        uint32_t bg = w->button.pressed ? accent_pressed
                                        : (w->button.hovered ? accent_hover : bg_elevated);
        uint32_t bcol = w->focused ? border_focus : (w->button.pressed ? accent : border_col);
        uint32_t tcol = (w->button.pressed || w->button.hovered) ? bg_base : fg_primary;

        push_rounded_rect(out, x, y, width, height, 4, bg);
        push_border(out, x, y, width, height, w->focused ? 2 : 1, bcol);

        if (w->button.label && w->button.label[0] != '\0') {
            int32_t tw = 0, th = 0;
            app_measure_text_ex(w->button.label, fsize_default, APP_FONT_MEDIUM, &tw, &th);
            push_text(out, x + (width - tw) / 2, y + (height - th) / 2,
                      w->button.label, fsize_default, APP_FONT_MEDIUM, tcol);
        }
        break;
    }

    case UI_WIDGET_LABEL:
        if (w->label.text && w->label.text[0] != '\0') {
            float fsize = (w->layout_elem->font_size > 0.0f)
                          ? w->layout_elem->font_size : fsize_default;
            app_font_weight_t weight = (app_font_weight_t)w->layout_elem->font_weight;
            int32_t tw = 0, th = 0;
            app_measure_text_ex(w->label.text, fsize, weight, &tw, &th);
            int32_t ty = y + (height > th ? (height - th) / 2 : 0);
            push_text(out, x, ty, w->label.text, fsize, weight,
                      w->label.color ? w->label.color : fg_primary);
        }
        break;

    case UI_WIDGET_TEXT_FIELD:
        ui_text_field_emit_draw(w, out);
        break;

    case UI_WIDGET_CHECKBOX: {
        int32_t bs = 16;
        int32_t by = y + (height - bs) / 2;
        push_rounded_rect(out, x, by, bs, bs, 3, bg_elevated);
        push_border(out, x, by, bs, bs, 1, w->focused ? border_focus : border_col);

        if (w->checkbox.checked)
            push_fill_rect(out, x + 3, by + 3, bs - 6, bs - 6, accent);

        if (w->checkbox.label && w->checkbox.label[0] != '\0') {
            int32_t th = 0;
            app_measure_text_ex(w->checkbox.label, fsize_default, APP_FONT_REGULAR, NULL, &th);
            push_text(out, x + bs + 8, y + (height - th) / 2,
                      w->checkbox.label, fsize_default, APP_FONT_REGULAR, fg_primary);
        }
        break;
    }

    case UI_WIDGET_RADIO: {
        int32_t bs = 16;
        int32_t by = y + (height - bs) / 2;
        push_rounded_rect(out, x, by, bs, bs, 8, bg_elevated);
        push_border(out, x, by, bs, bs, 1, w->focused ? border_focus : border_col);

        if (w->radio.checked)
            push_rounded_rect(out, x + 4, by + 4, 8, 8, 4, accent);

        if (w->radio.label && w->radio.label[0] != '\0') {
            int32_t th = 0;
            app_measure_text_ex(w->radio.label, fsize_default, APP_FONT_REGULAR, NULL, &th);
            push_text(out, x + bs + 8, y + (height - th) / 2,
                      w->radio.label, fsize_default, APP_FONT_REGULAR, fg_primary);
        }
        break;
    }

    case UI_WIDGET_SLIDER: {
        int32_t th = 4;
        int32_t ty = y + (height - th) / 2;
        push_rounded_rect(out, x, ty, width, th, 2, bg_overlay);

        float range = (w->slider.max_val > w->slider.min_val)
                      ? (w->slider.max_val - w->slider.min_val) : 1.0f;
        float frac = (w->slider.value - w->slider.min_val) / range;
        if (frac < 0.0f) frac = 0.0f;
        if (frac > 1.0f) frac = 1.0f;

        int32_t thumb_x = x + (int32_t)(frac * (float)(width - 14));
        push_rounded_rect(out, x, ty, (thumb_x - x) + 7, th, 2, accent);

        int32_t thumb_y = y + (height - 14) / 2;
        push_rounded_rect(out, thumb_x, thumb_y, 14, 14, 7, fg_primary);
        push_border(out, thumb_x, thumb_y, 14, 14, 1, w->focused ? border_focus : accent);
        break;
    }

    case UI_WIDGET_LIST: {
        push_rounded_rect(out, x, y, width, height, 4, bg_elevated);
        push_border(out, x, y, width, height, 1, w->focused ? border_focus : border_col);

        int32_t inner_h = height - 2;
        int32_t total_h = w->list.count * UI_LIST_ROW_H;
        int has_sb = (total_h > inner_h && inner_h > 20 && width > UI_SCROLLBAR_W + 8);
        int32_t content_w = has_sb ? (width - 2 - UI_SCROLLBAR_W) : (width - 2);
        if (content_w < 0) content_w = 0;

        int32_t max_scroll = (total_h > inner_h) ? (total_h - inner_h) : 0;
        if (w->list.scroll_top < 0) w->list.scroll_top = 0;
        if (w->list.scroll_top > max_scroll) w->list.scroll_top = max_scroll;

        vanilla_draw_cmd_t cmd;
        memset(&cmd, 0, sizeof(cmd));
        cmd.type = VCMD_PUSH_CLIP;
        cmd.bounds.x = x + 1;
        cmd.bounds.y = y + 1;
        cmd.bounds.w = content_w;
        cmd.bounds.h = inner_h;
        push_cmd(out, cmd);

        int32_t row_h = UI_LIST_ROW_H;
        int32_t line_h = 0;
        app_measure_text_ex("Ag", fsize_default, APP_FONT_REGULAR, NULL, &line_h);
        if (line_h <= 0) line_h = (int32_t)fsize_default;

        for (int32_t i = 0; i < w->list.count; i++) {
            int32_t ry = y + 1 + i * row_h - w->list.scroll_top;
            if (ry + row_h < y || ry > y + height)
                continue;

            if (i == w->list.selected) {
                push_fill_rect(out, x + 2, ry + 1,
                               content_w > 2 ? content_w - 2 : content_w,
                               row_h - 2, selection);
            } else if (i == w->list.hovered) {
                push_fill_rect(out, x + 2, ry + 1,
                               content_w > 2 ? content_w - 2 : content_w,
                               row_h - 2, bg_base);
            }

            if (w->list.items && w->list.items[i]) {
                push_text(out, x + 8, ry + (row_h - line_h) / 2,
                          w->list.items[i], fsize_default, APP_FONT_REGULAR, fg_primary);
            }
        }

        memset(&cmd, 0, sizeof(cmd));
        cmd.type = VCMD_POP_CLIP;
        push_cmd(out, cmd);

        if (has_sb) {
            emit_scrollbar(out, x + 1, y + 1, width - 2, inner_h,
                           total_h, w->list.scroll_top, w->list.sb_dragging,
                           bg_base, fg_dim, accent);
        }
        break;
    }

    case UI_WIDGET_SCROLL_VIEW:
        if (w->layout_elem->bg_color != 0)
            push_fill_rect(out, x, y, width, height, w->layout_elem->bg_color);
        if (w->layout_elem->border_width > 0 && w->layout_elem->border_color != 0)
            push_border(out, x, y, width, height,
                        w->layout_elem->border_width, w->layout_elem->border_color);
        break;

    case UI_WIDGET_TAB_BAR: {
        push_fill_rect(out, x, y, width, height, bg_elevated);
        push_fill_rect(out, x, y + height - 1, width, 1, border_col);

        int32_t count = w->tab_bar.tab_count > 0 ? w->tab_bar.tab_count : 1;
        int32_t tab_w = width / count;

        for (int32_t i = 0; i < w->tab_bar.tab_count; i++) {
            int32_t tx = x + i * tab_w;
            if (i == w->tab_bar.active_tab) {
                push_fill_rect(out, tx + 4, y + height - 3, tab_w - 8, 3, accent);
            } else if (i == w->tab_bar.hovered_tab) {
                push_fill_rect(out, tx + 2, y + 2, tab_w - 4, height - 5, selection);
            }

            if (w->tab_bar.labels && w->tab_bar.labels[i]) {
                app_font_weight_t weight = (i == w->tab_bar.active_tab)
                                           ? APP_FONT_MEDIUM : APP_FONT_REGULAR;
                int32_t tw = 0, th = 0;
                app_measure_text_ex(w->tab_bar.labels[i], fsize_default, weight, &tw, &th);
                push_text(out, tx + (tab_w - tw) / 2, y + (height - th) / 2,
                          w->tab_bar.labels[i], fsize_default, weight,
                          (i == w->tab_bar.active_tab) ? accent : fg_muted);
            }
        }
        break;
    }

    case UI_WIDGET_MODAL_DIALOG: {
        push_fill_rect(out, x, y, width, height, bg_overlay);

        int32_t cx = 0, cy = 0, cw = 0, ch = 0;
        ui_modal_compute_layout(w, &cx, &cy, &cw, &ch);

        int32_t card_rad = g_theme ? g_theme->radius_md : 8;
        push_rounded_rect(out, cx, cy, cw, ch, card_rad, bg_elevated);
        push_border(out, cx, cy, cw, ch, 1, border_col);

        float title_sz = fsize_default + 2.0f;
        if (w->modal.title) {
            push_text(out, cx + 16, cy + 14, w->modal.title,
                      title_sz, APP_FONT_BOLD, fg_primary);
        }

        if (w->modal.body) {
            vanilla_draw_cmd_t clip_cmd;
            memset(&clip_cmd, 0, sizeof(clip_cmd));
            clip_cmd.type = VCMD_PUSH_CLIP;
            clip_cmd.bounds.x = cx + 16;
            clip_cmd.bounds.y = cy + 40;
            clip_cmd.bounds.w = cw > 32 ? cw - 32 : 0;
            clip_cmd.bounds.h = (w->modal.kind == UI_DIALOG_INPUT) ? 44 : 88;
            push_cmd(out, clip_cmd);

            push_text(out, cx + 16, cy + 42, w->modal.body,
                      fsize_default, APP_FONT_REGULAR, fg_muted);

            memset(&clip_cmd, 0, sizeof(clip_cmd));
            clip_cmd.type = VCMD_POP_CLIP;
            push_cmd(out, clip_cmd);
        }

        if (w->modal.kind == UI_DIALOG_CONFIRM || w->modal.kind == UI_DIALOG_INPUT) {
            const char *cancel_txt = w->modal.cancel_label ? w->modal.cancel_label : "Cancel";
            int32_t cbtn_x = cx + cw - 186;
            int32_t cbtn_y = cy + ch - 40;
            uint32_t cbg = (w->modal.pressed_btn == 2) ? bg_overlay
                         : ((w->modal.hovered_btn == 2) ? selection : bg_base);
            push_rounded_rect(out, cbtn_x, cbtn_y, 80, 28, 4, cbg);
            push_border(out, cbtn_x, cbtn_y, 80, 28, 1, border_col);

            int32_t tw = 0, th = 0;
            app_measure_text_ex(cancel_txt, fsize_default, APP_FONT_MEDIUM, &tw, &th);
            push_text(out, cbtn_x + (80 - tw) / 2, cbtn_y + (28 - th) / 2,
                      cancel_txt, fsize_default, APP_FONT_MEDIUM, fg_primary);
        }

        const char *btn_txt = w->modal.ok_label ? w->modal.ok_label : "OK";
        int32_t btn_x = cx + cw - 96;
        int32_t btn_y = cy + ch - 40;
        uint32_t obg = (w->modal.pressed_btn == 1) ? accent_pressed
                     : ((w->modal.hovered_btn == 1) ? accent_hover : accent);

        push_rounded_rect(out, btn_x, btn_y, 80, 28, 4, obg);

        int32_t tw = 0, th = 0;
        app_measure_text_ex(btn_txt, fsize_default, APP_FONT_MEDIUM, &tw, &th);
        push_text(out, btn_x + (80 - tw) / 2, btn_y + (28 - th) / 2,
                  btn_txt, fsize_default, APP_FONT_MEDIUM, bg_base);
        break;
    }

    case UI_WIDGET_TOAST: {
        int64_t now = ui_current_time_ms();
        if (now < w->toast.shown_ms + w->toast.timeout_ms) {
            int32_t mw = 0, mh = 0;
            if (w->toast.message)
                app_measure_text_ex(w->toast.message, fsize_default, APP_FONT_REGULAR, &mw, &mh);
            int32_t tw = (mw + 36 > 280) ? (mw + 36) : 280;
            if (tw > width - 16 && width > 64)
                tw = width - 16;
            int32_t th = 36;
            int32_t tx = x + (width - tw) / 2;
            int32_t ty = y + height - th - 16;

            push_rounded_rect(out, tx, ty, tw, th, 18, bg_elevated);
            push_border(out, tx, ty, tw, th, 1, border_col);

            if (w->toast.message) {
                push_text(out, tx + (tw - mw) / 2, ty + (th - mh) / 2,
                          w->toast.message, fsize_default, APP_FONT_REGULAR, fg_primary);
            }
        }
        break;
    }

    case UI_WIDGET_MENU: {
        /* Menus always render with sharp 0px corners per design system */
        push_fill_rect(out, x, y, width, height, bg_elevated);

        if (w->menu.is_bar) {
            push_fill_rect(out, x, y + height - 1, width, 1, border_col);
            int32_t cur_x = x + 4;
            int32_t lh = 0;
            app_measure_text_ex("Ag", fsize_default, APP_FONT_MEDIUM, NULL, &lh);
            if (lh <= 0) lh = (int32_t)fsize_default;

            for (int32_t i = 0; i < w->menu.count; i++) {
                const char *lbl = w->menu.rich_items ? w->menu.rich_items[i].label
                                                     : (w->menu.items ? w->menu.items[i] : NULL);
                if (!lbl)
                    continue;
                int32_t tw = 0;
                app_measure_text_ex(lbl, fsize_default, APP_FONT_MEDIUM, &tw, NULL);
                int32_t iw = tw + 20;
                if (cur_x + iw > x + width)
                    break;

                if (i == w->menu.selected || i == w->menu.hovered) {
                    push_fill_rect(out, cur_x, y + 2, iw, height - 4, selection);
                    if (i == w->menu.selected)
                        push_fill_rect(out, cur_x, y + height - 2, iw, 2, accent);
                }
                push_text(out, cur_x + 10, y + (height - lh) / 2,
                          lbl, fsize_default, APP_FONT_MEDIUM, fg_primary);
                cur_x += iw;
            }
            break;
        }

        push_border(out, x, y, width, height, 1, w->focused ? border_focus : border_col);

        int32_t has_check = 0;
        if (w->menu.rich_items) {
            for (int32_t i = 0; i < w->menu.count; i++) {
                if (w->menu.rich_items[i].flags & UI_MENU_ITEM_CHECKED) {
                    has_check = 1;
                    break;
                }
            }
        }
        int32_t text_x = x + (has_check ? 24 : 12);
        int32_t lh = 0;
        app_measure_text_ex("Ag", fsize_default, APP_FONT_REGULAR, NULL, &lh);
        if (lh <= 0) lh = (int32_t)fsize_default;

        int32_t ry = y;
        for (int32_t i = 0; i < w->menu.count; i++) {
            int32_t is_sep = ui_menu_is_separator(w, i);
            int32_t rh = is_sep ? UI_MENU_SEP_H : UI_MENU_ROW_H;
            if (ry + rh > y + height)
                break;

            if (is_sep) {
                push_fill_rect(out, x + 8, ry + rh / 2,
                               width > 16 ? width - 16 : width, 1, border_col);
                ry += rh;
                continue;
            }

            int32_t is_dis = ui_menu_is_disabled(w, i);
            if (!is_dis && (i == w->menu.selected || i == w->menu.hovered)) {
                push_fill_rect(out, x + 2, ry + 1,
                               width > 4 ? width - 4 : width, rh - 2, selection);
            }

            const char *lbl = NULL;
            const char *sc = NULL;
            uint32_t flags = 0;
            if (w->menu.rich_items) {
                lbl = w->menu.rich_items[i].label;
                sc = w->menu.rich_items[i].shortcut;
                flags = w->menu.rich_items[i].flags;
            } else if (w->menu.items) {
                lbl = w->menu.items[i];
            }

            uint32_t col = is_dis ? fg_dim : fg_primary;
            int32_t ty = ry + (rh - lh) / 2;

            if (flags & UI_MENU_ITEM_CHECKED)
                push_text(out, x + 8, ty, "*", fsize_default, APP_FONT_BOLD, accent);

            if (lbl && lbl[0] != '\0')
                push_text(out, text_x, ty, lbl, fsize_default, APP_FONT_REGULAR, col);

            if (sc && sc[0] != '\0') {
                int32_t sw = 0;
                app_measure_text_ex(sc, fsize_default, APP_FONT_REGULAR, &sw, NULL);
                int32_t right_off = (flags & UI_MENU_ITEM_SUBMENU) ? 24 : 12;
                push_text(out, x + width - sw - right_off, ty,
                          sc, fsize_default, APP_FONT_REGULAR, fg_muted);
            }

            if (flags & UI_MENU_ITEM_SUBMENU) {
                push_text(out, x + width - 16, ty, ">",
                          fsize_default, APP_FONT_MEDIUM, col);
            }

            ry += rh;
        }
        break;
    }

    case UI_WIDGET_PROGRESS_BAR: {
        int32_t rad = w->layout_elem->corner_radius;
        push_rounded_rect(out, x, y, width, height, rad, bg_base);
        push_border(out, x, y, width, height, 1, border_col);

        float max_v = (w->progress_bar.max_val > 0.0f) ? w->progress_bar.max_val : 100.0f;
        float frac = w->progress_bar.value / max_v;
        if (frac < 0.0f) frac = 0.0f;
        if (frac > 1.0f) frac = 1.0f;

        int32_t inner_w = width > 2 ? (width - 2) : 0;
        int32_t inner_h = height > 2 ? (height - 2) : 0;
        int32_t fill_w = (int32_t)((float)inner_w * frac + 0.5f);
        uint32_t fill_col = w->progress_bar.bar_color ? w->progress_bar.bar_color : accent;

        if (fill_w > 0 && inner_h > 0) {
            int32_t fill_rad = (rad > 1 && fill_w >= rad * 2) ? (rad - 1) : 0;
            push_rounded_rect(out, x + 1, y + 1, fill_w, inner_h, fill_rad, fill_col);
        }

        if (w->progress_bar.show_label && height >= 14 && w->progress_bar.label_buf[0] != '\0') {
            float lbl_sz = (fsize_default >= 12.0f) ? (fsize_default - 2.0f) : fsize_default;
            if (lbl_sz < 10.0f) lbl_sz = 10.0f;
            int32_t tw = 0, th = 0;
            app_measure_text_ex(w->progress_bar.label_buf, lbl_sz, APP_FONT_MEDIUM, &tw, &th);
            uint32_t lcol = (fill_w > width / 2) ? bg_base : fg_primary;
            push_text(out, x + (width - tw) / 2, y + (height - th) / 2,
                      w->progress_bar.label_buf, lbl_sz, APP_FONT_MEDIUM, lcol);
        }
        break;
    }
    }
}
