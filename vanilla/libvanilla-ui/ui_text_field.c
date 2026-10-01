/*
 * Project Tsukasa — Display Server Widget Toolkit Text Field
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
#include "../input/keymap.h"
#include <string.h>
#include <sys/input.h>

ui_widget_t *ui_text_field(ui_ctx_t *ctx, const char *initial, ui_event_cb_t cb, void *ud)
{
    ui_widget_t *w = ui_alloc_widget(ctx, UI_WIDGET_TEXT_FIELD);
    if (!w)
        return NULL;

    w->on_event = cb;
    w->userdata = ud;

    if (initial) {
        strncpy(w->text_field.buf, initial, sizeof(w->text_field.buf) - 1);
        w->text_field.buf[sizeof(w->text_field.buf) - 1] = '\0';
        w->text_field.cursor_pos = (int32_t)strlen(w->text_field.buf);
    } else {
        w->text_field.buf[0] = '\0';
        w->text_field.cursor_pos = 0;
    }

    w->text_field.sel_start = -1;
    w->text_field.sel_end = -1;
    w->text_field.scroll_x = 0;
    w->text_field.caret_ms = ui_current_time_ms();

    return w;
}

static void delete_selection(ui_widget_t *w)
{
    if (w->text_field.sel_start < 0 || w->text_field.sel_end <= w->text_field.sel_start)
        return;

    int len = (int)strlen(w->text_field.buf);
    int s0 = w->text_field.sel_start;
    int s1 = w->text_field.sel_end;
    if (s0 < 0) s0 = 0;
    if (s1 > len) s1 = len;
    if (s0 >= s1) {
        w->text_field.sel_start = -1;
        w->text_field.sel_end = -1;
        return;
    }

    memmove(&w->text_field.buf[s0], &w->text_field.buf[s1], len - s1 + 1);
    w->text_field.cursor_pos = s0;
    w->text_field.sel_start = -1;
    w->text_field.sel_end = -1;
}

void ui_text_field_handle_key(ui_widget_t *w, const struct input_event *ev, int shift, int ctrl)
{
    (void)ctrl;
    if (!w || w->type != UI_WIDGET_TEXT_FIELD || ev->value == 0)
        return;

    int text_changed = 0;
    int len = (int)strlen(w->text_field.buf);
    if (w->text_field.cursor_pos < 0)
        w->text_field.cursor_pos = 0;
    if (w->text_field.cursor_pos > len)
        w->text_field.cursor_pos = len;

    char ch = vanilla_evdev_to_ascii(ev->code, shift);

    if (ch >= 0x20 && ch <= 0x7E) {
        if (w->text_field.sel_start >= 0 && w->text_field.sel_end > w->text_field.sel_start)
            delete_selection(w);

        len = (int)strlen(w->text_field.buf);
        if (len < (int)sizeof(w->text_field.buf) - 1) {
            if (w->text_field.cursor_pos < 0)
                w->text_field.cursor_pos = 0;
            if (w->text_field.cursor_pos > len)
                w->text_field.cursor_pos = len;

            memmove(&w->text_field.buf[w->text_field.cursor_pos + 1],
                    &w->text_field.buf[w->text_field.cursor_pos],
                    len - w->text_field.cursor_pos + 1);
            w->text_field.buf[w->text_field.cursor_pos] = ch;
            w->text_field.cursor_pos++;
            text_changed = 1;
        }
    } else {
        switch (ev->code) {
        case KEY_BACKSPACE:
            if (w->text_field.sel_start >= 0 && w->text_field.sel_end > w->text_field.sel_start) {
                delete_selection(w);
                text_changed = 1;
            } else if (w->text_field.cursor_pos > 0) {
                memmove(&w->text_field.buf[w->text_field.cursor_pos - 1],
                        &w->text_field.buf[w->text_field.cursor_pos],
                        len - w->text_field.cursor_pos + 1);
                w->text_field.cursor_pos--;
                text_changed = 1;
            }
            break;

        case KEY_DELETE:
            if (w->text_field.sel_start >= 0 && w->text_field.sel_end > w->text_field.sel_start) {
                delete_selection(w);
                text_changed = 1;
            } else if (w->text_field.cursor_pos < len) {
                memmove(&w->text_field.buf[w->text_field.cursor_pos],
                        &w->text_field.buf[w->text_field.cursor_pos + 1],
                        len - w->text_field.cursor_pos);
                text_changed = 1;
            }
            break;

        case KEY_LEFT: {
            int anchor = (w->text_field.sel_start >= 0) ?
                         (w->text_field.cursor_pos == w->text_field.sel_end ? w->text_field.sel_start : w->text_field.sel_end) :
                         w->text_field.cursor_pos;
            if (w->text_field.cursor_pos > 0)
                w->text_field.cursor_pos--;
            if (shift) {
                int a = anchor, b = w->text_field.cursor_pos;
                w->text_field.sel_start = a < b ? a : b;
                w->text_field.sel_end = a > b ? a : b;
                if (w->text_field.sel_start == w->text_field.sel_end) {
                    w->text_field.sel_start = -1;
                    w->text_field.sel_end = -1;
                }
            } else {
                w->text_field.sel_start = -1;
                w->text_field.sel_end = -1;
            }
            break;
        }

        case KEY_RIGHT: {
            int anchor = (w->text_field.sel_start >= 0) ?
                         (w->text_field.cursor_pos == w->text_field.sel_end ? w->text_field.sel_start : w->text_field.sel_end) :
                         w->text_field.cursor_pos;
            if (w->text_field.cursor_pos < len)
                w->text_field.cursor_pos++;
            if (shift) {
                int a = anchor, b = w->text_field.cursor_pos;
                w->text_field.sel_start = a < b ? a : b;
                w->text_field.sel_end = a > b ? a : b;
                if (w->text_field.sel_start == w->text_field.sel_end) {
                    w->text_field.sel_start = -1;
                    w->text_field.sel_end = -1;
                }
            } else {
                w->text_field.sel_start = -1;
                w->text_field.sel_end = -1;
            }
            break;
        }

        case KEY_HOME: {
            int anchor = (w->text_field.sel_start >= 0) ?
                         (w->text_field.cursor_pos == w->text_field.sel_end ? w->text_field.sel_start : w->text_field.sel_end) :
                         w->text_field.cursor_pos;
            w->text_field.cursor_pos = 0;
            if (shift) {
                int a = anchor, b = 0;
                w->text_field.sel_start = a < b ? a : b;
                w->text_field.sel_end = a > b ? a : b;
                if (w->text_field.sel_start == w->text_field.sel_end) {
                    w->text_field.sel_start = -1;
                    w->text_field.sel_end = -1;
                }
            } else {
                w->text_field.sel_start = -1;
                w->text_field.sel_end = -1;
            }
            break;
        }

        case KEY_END: {
            int anchor = (w->text_field.sel_start >= 0) ?
                         (w->text_field.cursor_pos == w->text_field.sel_end ? w->text_field.sel_start : w->text_field.sel_end) :
                         w->text_field.cursor_pos;
            w->text_field.cursor_pos = len;
            if (shift) {
                int a = anchor, b = len;
                w->text_field.sel_start = a < b ? a : b;
                w->text_field.sel_end = a > b ? a : b;
                if (w->text_field.sel_start == w->text_field.sel_end) {
                    w->text_field.sel_start = -1;
                    w->text_field.sel_end = -1;
                }
            } else {
                w->text_field.sel_start = -1;
                w->text_field.sel_end = -1;
            }
            break;
        }

        default:
            break;
        }
    }

    w->text_field.caret_ms = ui_current_time_ms();
    ui_widget_invalidate(w);

    if (text_changed) {
        ui_event_t out_ev;
        out_ev.type = UI_EVENT_TEXT_CHANGED;
        out_ev.source = w;
        if (w->on_event)
            w->on_event(w, &out_ev, w->userdata);
    }
}

void ui_text_field_handle_click(ui_widget_t *w, int32_t click_x, int32_t click_y, int shift)
{
    (void)click_y;
    if (!w || w->type != UI_WIDGET_TEXT_FIELD)
        return;

    int32_t bx = w->layout_elem ? w->layout_elem->computed_x : 0;
    int32_t inner_x = bx + 6;
    int32_t rel_x = click_x - inner_x + w->text_field.scroll_x;
    int char_w = 8;
    int char_idx = (rel_x + char_w / 2) / char_w;
    int len = (int)strlen(w->text_field.buf);

    if (char_idx < 0) char_idx = 0;
    if (char_idx > len) char_idx = len;

    if (shift) {
        int anchor = (w->text_field.sel_start >= 0) ?
                     (w->text_field.cursor_pos == w->text_field.sel_end ? w->text_field.sel_start : w->text_field.sel_end) :
                     w->text_field.cursor_pos;
        w->text_field.cursor_pos = char_idx;
        w->text_field.sel_start = anchor < char_idx ? anchor : char_idx;
        w->text_field.sel_end = anchor > char_idx ? anchor : char_idx;
        if (w->text_field.sel_start == w->text_field.sel_end) {
            w->text_field.sel_start = -1;
            w->text_field.sel_end = -1;
        }
    } else {
        w->text_field.cursor_pos = char_idx;
        w->text_field.sel_start = -1;
        w->text_field.sel_end = -1;
    }

    w->text_field.caret_ms = ui_current_time_ms();
    ui_widget_invalidate(w);
}

void ui_text_field_update_blink(ui_widget_t *w)
{
    if (!w || w->type != UI_WIDGET_TEXT_FIELD || !w->focused)
        return;

    int64_t now = ui_current_time_ms();
    int cur_state = (int)((now / 530) % 2);
    int last_state = (int)((w->text_field.caret_ms / 530) % 2);

    if (cur_state != last_state) {
        w->text_field.caret_ms = now;
        ui_widget_invalidate(w);
    }
}

static inline void push_cmd(vanilla_draw_cmd_array_t *out, vanilla_draw_cmd_t cmd)
{
    if (out && out->cmds && out->count < out->capacity)
        out->cmds[out->count++] = cmd;
}

void ui_text_field_emit_draw(ui_widget_t *w, vanilla_draw_cmd_array_t *out)
{
    if (!w || !w->layout_elem || !out)
        return;

    int32_t x = w->layout_elem->computed_x;
    int32_t y = w->layout_elem->computed_y;
    int32_t width = w->layout_elem->computed_w;
    int32_t height = w->layout_elem->computed_h;

    uint32_t bg_col = g_theme ? g_theme->bg_elevated : 0xFF3B4252u;
    uint32_t border_col = w->focused ?
                          (g_theme ? g_theme->border_focus : 0xFF88C0D0u) :
                          (g_theme ? g_theme->border : 0xFF4C566Au);
    uint32_t text_col = g_theme ? g_theme->fg_primary : 0xFFECEFF4u;
    uint32_t sel_col = g_theme ? g_theme->selection : 0x4488C0D0u;
    uint32_t accent_col = g_theme ? g_theme->accent : 0xFF88C0D0u;

    vanilla_draw_cmd_t cmd;
    memset(&cmd, 0, sizeof(cmd));
    cmd.type = VCMD_ROUNDED_RECT;
    cmd.bounds.x = x;
    cmd.bounds.y = y;
    cmd.bounds.w = width;
    cmd.bounds.h = height;
    cmd.rounded_rect.color = bg_col;
    cmd.rounded_rect.radius = 4;
    push_cmd(out, cmd);

    memset(&cmd, 0, sizeof(cmd));
    cmd.type = VCMD_BORDER;
    cmd.bounds.x = x;
    cmd.bounds.y = y;
    cmd.bounds.w = width;
    cmd.bounds.h = height;
    cmd.border.color = border_col;
    cmd.border.width = w->focused ? 2 : 1;
    push_cmd(out, cmd);

    int32_t pad_x = 6;
    int32_t pad_y = 4;
    int32_t inner_x = x + pad_x;
    int32_t inner_y = y + pad_y;
    int32_t inner_w = width - pad_x * 2;
    int32_t inner_h = height - pad_y * 2;
    if (inner_w < 0) inner_w = 0;
    if (inner_h < 0) inner_h = 0;

    memset(&cmd, 0, sizeof(cmd));
    cmd.type = VCMD_PUSH_CLIP;
    cmd.bounds.x = inner_x;
    cmd.bounds.y = inner_y;
    cmd.bounds.w = inner_w;
    cmd.bounds.h = inner_h;
    push_cmd(out, cmd);

    int char_w = 8;
    int char_h = 8;
    int len = (int)strlen(w->text_field.buf);

    int32_t cursor_px = w->text_field.cursor_pos * char_w;
    if (cursor_px - w->text_field.scroll_x > inner_w - char_w) {
        if (inner_w > char_w + 16)
            w->text_field.scroll_x = cursor_px - inner_w + char_w + 16;
        else
            w->text_field.scroll_x = cursor_px;
    }
    if (cursor_px - w->text_field.scroll_x < 0)
        w->text_field.scroll_x = cursor_px;
    if (w->text_field.scroll_x < 0)
        w->text_field.scroll_x = 0;

    if (w->text_field.sel_start >= 0 && w->text_field.sel_end > w->text_field.sel_start) {
        int s0 = w->text_field.sel_start < len ? w->text_field.sel_start : len;
        int s1 = w->text_field.sel_end < len ? w->text_field.sel_end : len;
        int32_t sx = inner_x + s0 * char_w - w->text_field.scroll_x;
        int32_t sw = (s1 - s0) * char_w;
        memset(&cmd, 0, sizeof(cmd));
        cmd.type = VCMD_FILL_RECT;
        cmd.bounds.x = sx;
        cmd.bounds.y = inner_y;
        cmd.bounds.w = sw;
        cmd.bounds.h = inner_h;
        cmd.fill_rect.color = sel_col;
        push_cmd(out, cmd);
    }

    if (len > 0) {
        memset(&cmd, 0, sizeof(cmd));
        cmd.type = VCMD_TEXT;
        cmd.bounds.x = inner_x - w->text_field.scroll_x;
        cmd.bounds.y = inner_y + (inner_h - char_h) / 2;
        cmd.bounds.w = len * char_w;
        cmd.bounds.h = char_h;
        cmd.text.text = w->text_field.buf;
        cmd.text.color = text_col;
        cmd.text.font_size = 8.0f;
        push_cmd(out, cmd);
    }

    if (w->focused) {
        int64_t now = ui_current_time_ms();
        if ((now / 530) % 2 == 0) {
            int32_t cx = inner_x + w->text_field.cursor_pos * char_w - w->text_field.scroll_x;
            int32_t cy = inner_y + (inner_h - char_h) / 2;
            memset(&cmd, 0, sizeof(cmd));
            cmd.type = VCMD_FILL_RECT;
            cmd.bounds.x = cx;
            cmd.bounds.y = cy;
            cmd.bounds.w = 2;
            cmd.bounds.h = char_h;
            cmd.fill_rect.color = accent_col;
            push_cmd(out, cmd);
        }
    }

    memset(&cmd, 0, sizeof(cmd));
    cmd.type = VCMD_POP_CLIP;
    push_cmd(out, cmd);
}
