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
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/input.h>

#define TF_LINE_NUM_POOL 256

typedef struct {
    int32_t x;
    int32_t y;
    int32_t width;
    int32_t height;
    int32_t pad_x;
    int32_t pad_y;
    int32_t gutter_x;
    int32_t gutter_w;
    int32_t text_x;
    int32_t text_y;
    int32_t text_w;
    int32_t text_h;
    int32_t glyph_h;
    int32_t line_h;
    int32_t line_count;
    int32_t content_h;
    int32_t max_scroll_y;
    int32_t has_sb;
    float   fsize;
} tf_geom_t;

static void push_cmd(vanilla_draw_cmd_array_t *out, vanilla_draw_cmd_t cmd)
{
    if (out && out->count < out->capacity)
        out->cmds[out->count++] = cmd;
}

static float tf_font_size(const ui_widget_t *w)
{
    if (w && w->text_field.font_size > 0.0f)
        return w->text_field.font_size;
    return app_default_font_size();
}

static const char *tf_buf_ro(const ui_widget_t *w, int32_t *out_len)
{
    if (!w || w->type != UI_WIDGET_TEXT_FIELD) {
        if (out_len)
            *out_len = 0;
        return "";
    }
    if (w->text_field.multiline) {
        if (!w->text_field.ml_buf) {
            if (out_len)
                *out_len = 0;
            return "";
        }
        int32_t len = w->text_field.ml_len;
        if (len < 0)
            len = (int32_t)strlen(w->text_field.ml_buf);
        if (out_len)
            *out_len = len;
        return w->text_field.ml_buf;
    }
    int32_t len = (int32_t)strlen(w->text_field.buf);
    if (out_len)
        *out_len = len;
    return w->text_field.buf;
}

static int tf_ensure_ml_cap(ui_widget_t *w, int32_t needed_bytes)
{
    if (!w || !w->text_field.multiline)
        return 0;
    if (needed_bytes + 1 <= w->text_field.ml_cap && w->text_field.ml_buf)
        return 1;

    int32_t new_cap = w->text_field.ml_cap > 0 ? w->text_field.ml_cap : 512;
    while (new_cap < needed_bytes + 1) {
        if (new_cap > 0x10000000)
            return 0;
        new_cap *= 2;
    }

    char *nb = (char *)realloc(w->text_field.ml_buf, (size_t)new_cap);
    if (!nb)
        return 0;
    if (!w->text_field.ml_buf)
        nb[0] = '\0';
    w->text_field.ml_buf = nb;
    w->text_field.ml_cap = new_cap;
    return 1;
}

static int32_t tf_clamp_pos(int32_t pos, int32_t len)
{
    if (pos < 0)
        return 0;
    if (pos > len)
        return len;
    return pos;
}

static int32_t tf_prev_char(const char *s, int32_t pos)
{
    if (pos <= 0)
        return 0;
    int32_t p = pos - 1;
    while (p > 0 && (((uint8_t)s[p] & 0xC0u) == 0x80u))
        p--;
    return p;
}

static int32_t tf_next_char(const char *s, int32_t len, int32_t pos)
{
    if (pos >= len)
        return len;
    int32_t p = pos + 1;
    while (p < len && (((uint8_t)s[p] & 0xC0u) == 0x80u))
        p++;
    return p;
}

static uint32_t tf_decode_utf8(const char *s, int32_t len, int32_t *pos)
{
    int32_t i = *pos;
    if (i >= len)
        return 0;

    uint8_t b0 = (uint8_t)s[i];
    if (b0 < 0x80u) {
        *pos = i + 1;
        return b0;
    }
    if ((b0 & 0xE0u) == 0xC0u && i + 1 < len && (((uint8_t)s[i + 1] & 0xC0u) == 0x80u)) {
        uint32_t cp = ((uint32_t)(b0 & 0x1Fu) << 6) | (uint32_t)((uint8_t)s[i + 1] & 0x3Fu);
        if (cp >= 0x80u) {
            *pos = i + 2;
            return cp;
        }
    } else if ((b0 & 0xF0u) == 0xE0u && i + 2 < len &&
               (((uint8_t)s[i + 1] & 0xC0u) == 0x80u) &&
               (((uint8_t)s[i + 2] & 0xC0u) == 0x80u)) {
        uint32_t cp = ((uint32_t)(b0 & 0x0Fu) << 12) |
                      ((uint32_t)((uint8_t)s[i + 1] & 0x3Fu) << 6) |
                      (uint32_t)((uint8_t)s[i + 2] & 0x3Fu);
        if (cp >= 0x800u) {
            *pos = i + 3;
            return cp;
        }
    } else if ((b0 & 0xF8u) == 0xF0u && i + 3 < len &&
               (((uint8_t)s[i + 1] & 0xC0u) == 0x80u) &&
               (((uint8_t)s[i + 2] & 0xC0u) == 0x80u) &&
               (((uint8_t)s[i + 3] & 0xC0u) == 0x80u)) {
        uint32_t cp = ((uint32_t)(b0 & 0x07u) << 18) |
                      ((uint32_t)((uint8_t)s[i + 1] & 0x3Fu) << 12) |
                      ((uint32_t)((uint8_t)s[i + 2] & 0x3Fu) << 6) |
                      (uint32_t)((uint8_t)s[i + 3] & 0x3Fu);
        if (cp >= 0x10000u && cp <= 0x10FFFFu) {
            *pos = i + 4;
            return cp;
        }
    }
    *pos = i + 1;
    return (uint32_t)b0;
}

static uint32_t tf_peek_utf8(const char *s, int32_t len, int32_t pos)
{
    if (pos >= len)
        return 0;
    int32_t tmp = pos;
    return tf_decode_utf8(s, len, &tmp);
}

static int32_t tf_prefix_px(const char *line, int32_t line_len, int32_t target_bytes, float fsize)
{
    if (!line || target_bytes <= 0 || line_len <= 0)
        return 0;
    if (target_bytes > line_len)
        target_bytes = line_len;

    vanilla_font_t *font = app_get_font();
    if (!font)
        return 0;

    int32_t x = 0;
    int32_t pos = 0;
    while (pos < target_bytes) {
        uint32_t cp = tf_decode_utf8(line, line_len, &pos);
        if (cp == '\r' || cp == '\n')
            continue;
        uint32_t next_cp = (pos < line_len) ? tf_peek_utf8(line, line_len, pos) : 0;
        if (next_cp == '\r' || next_cp == '\n')
            next_cp = 0;
        x += font_glyph_advance(font, cp, next_cp, fsize, FONT_WEIGHT_REGULAR);
    }
    return x;
}

static int32_t tf_hit_test_col(const char *line, int32_t line_len, int32_t rel_x, float fsize)
{
    if (!line || line_len <= 0 || rel_x <= 0)
        return 0;

    vanilla_font_t *font = app_get_font();
    if (!font)
        return 0;

    int32_t cur_x = 0;
    int32_t pos = 0;
    while (pos < line_len) {
        int32_t char_start = pos;
        uint32_t cp = tf_decode_utf8(line, line_len, &pos);
        if (cp == '\r' || cp == '\n')
            continue;
        uint32_t next_cp = (pos < line_len) ? tf_peek_utf8(line, line_len, pos) : 0;
        if (next_cp == '\r' || next_cp == '\n')
            next_cp = 0;
        int32_t adv = font_glyph_advance(font, cp, next_cp, fsize, FONT_WEIGHT_REGULAR);
        int32_t mid = cur_x + adv / 2;
        if (rel_x < mid)
            return char_start;
        cur_x += adv;
    }
    return line_len;
}

static int32_t tf_count_lines(const char *s, int32_t len)
{
    int32_t lines = 1;
    for (int32_t i = 0; i < len; i++) {
        if (s[i] == '\n')
            lines++;
    }
    return lines;
}

static void tf_line_bounds(const char *s, int32_t len, int32_t pos,
                           int32_t *out_ls, int32_t *out_le)
{
    pos = tf_clamp_pos(pos, len);
    int32_t ls = pos;
    while (ls > 0 && s[ls - 1] != '\n')
        ls--;

    int32_t le = pos;
    while (le < len && s[le] != '\n')
        le++;
    if (le > ls && s[le - 1] == '\r')
        le--;

    if (out_ls) *out_ls = ls;
    if (out_le) *out_le = le;
}

static void tf_pos_to_lc0(const char *s, int32_t len, int32_t pos,
                          int32_t *out_line, int32_t *out_ls, int32_t *out_le)
{
    pos = tf_clamp_pos(pos, len);
    int32_t line = 0;
    for (int32_t i = 0; i < pos; i++) {
        if (s[i] == '\n')
            line++;
    }
    if (out_line) *out_line = line;
    tf_line_bounds(s, len, pos, out_ls, out_le);
}

static int tf_line_at_index(const char *s, int32_t len, int32_t target_line,
                            int32_t *out_ls, int32_t *out_le)
{
    if (target_line < 0)
        return 0;

    int32_t line = 0;
    int32_t ls = 0;
    for (int32_t i = 0; i <= len; i++) {
        if (i == len || s[i] == '\n') {
            if (line == target_line) {
                int32_t le = i;
                if (le > ls && s[le - 1] == '\r')
                    le--;
                if (out_ls) *out_ls = ls;
                if (out_le) *out_le = le;
                return 1;
            }
            line++;
            ls = i + 1;
        }
    }
    return 0;
}

static void tf_get_geom(const ui_widget_t *w, tf_geom_t *g)
{
    memset(g, 0, sizeof(*g));
    if (!w || !w->layout_elem)
        return;

    int32_t len = 0;
    const char *buf = tf_buf_ro(w, &len);

    g->x = w->layout_elem->computed_x;
    g->y = w->layout_elem->computed_y;
    g->width = w->layout_elem->computed_w;
    g->height = w->layout_elem->computed_h;
    g->fsize = tf_font_size(w);

    int32_t gh = 0;
    app_measure_text_ex("Ag", g->fsize, APP_FONT_REGULAR, NULL, &gh);
    if (gh <= 0)
        gh = (int32_t)(g->fsize + 0.5f);
    if (gh <= 0)
        gh = 13;
    g->glyph_h = gh;

    if (!w->text_field.multiline) {
        g->line_h = gh;
        g->pad_x = 6;
        g->pad_y = 4;
        g->gutter_w = 0;
        g->line_count = 1;
        g->content_h = gh;
        g->has_sb = 0;
        g->text_x = g->x + g->pad_x;
        g->text_y = g->y + g->pad_y;
        g->text_w = g->width - g->pad_x * 2;
        g->text_h = g->height - g->pad_y * 2;
    } else {
        g->line_h = gh + 4;
        g->pad_x = 6;
        g->pad_y = 6;
        g->line_count = tf_count_lines(buf, len);

        if (w->text_field.show_line_numbers && g->width > 80) {
            char sample[16];
            int32_t digits_val = (g->line_count >= 1000) ? g->line_count : 999;
            snprintf(sample, sizeof(sample), "%d", digits_val);
            int32_t nw = 0;
            app_measure_text_ex(sample, g->fsize, APP_FONT_REGULAR, &nw, NULL);
            g->gutter_x = g->x + 1;
            g->gutter_w = nw + 14;
            if (g->gutter_w > g->width / 3)
                g->gutter_w = g->width / 3;
        }

        g->text_y = g->y + g->pad_y;
        g->text_h = g->height - g->pad_y * 2;
        g->content_h = g->line_count * g->line_h;
        g->has_sb = (g->content_h > g->text_h &&
                     g->height > 24 &&
                     g->width > g->gutter_w + UI_SCROLLBAR_W + 24) ? 1 : 0;
        g->text_x = g->x + g->gutter_w + g->pad_x;
        g->text_w = g->width - g->gutter_w - g->pad_x * 2 - (g->has_sb ? UI_SCROLLBAR_W : 0);
        g->max_scroll_y = (g->content_h > g->text_h && g->text_h > 0)
                          ? (g->content_h - g->text_h) : 0;
    }

    if (g->text_w < 0)
        g->text_w = 0;
    if (g->text_h < 0)
        g->text_h = 0;
}

static void tf_clear_selection(ui_widget_t *w)
{
    w->text_field.sel_start = -1;
    w->text_field.sel_end = -1;
    w->text_field.sel_anchor = -1;
}

static int tf_replace_range(ui_widget_t *w, int32_t s0, int32_t s1,
                            const char *ins, int32_t ins_len)
{
    if (!w || w->type != UI_WIDGET_TEXT_FIELD)
        return 0;

    int32_t len = 0;
    tf_buf_ro(w, &len);
    s0 = tf_clamp_pos(s0, len);
    s1 = tf_clamp_pos(s1, len);
    if (s1 < s0) {
        int32_t tmp = s0;
        s0 = s1;
        s1 = tmp;
    }
    if (!ins || ins_len < 0)
        ins_len = 0;

    if (!w->text_field.multiline) {
        int32_t max_ins = (int32_t)sizeof(w->text_field.buf) - 1 - (len - (s1 - s0));
        if (max_ins < 0)
            max_ins = 0;
        if (ins_len > max_ins)
            ins_len = max_ins;
        if (ins_len == 0 && s0 == s1)
            return 0;

        int32_t tail = len - s1;
        memmove(&w->text_field.buf[s0 + ins_len], &w->text_field.buf[s1], (size_t)tail + 1);
        for (int32_t i = 0; i < ins_len; i++) {
            char c = ins[i];
            w->text_field.buf[s0 + i] = (c == '\n' || c == '\r' || c == '\t') ? ' ' : c;
        }
        w->text_field.cursor_pos = s0 + ins_len;
        tf_clear_selection(w);
        w->text_field.ensure_cursor_visible = 1;
        return 1;
    }

    int32_t new_len = len - (s1 - s0) + ins_len;
    if (!tf_ensure_ml_cap(w, new_len))
        return 0;
    if (ins_len == 0 && s0 == s1)
        return 0;

    int32_t tail = len - s1;
    memmove(&w->text_field.ml_buf[s0 + ins_len], &w->text_field.ml_buf[s1], (size_t)tail + 1);
    if (ins_len > 0)
        memcpy(&w->text_field.ml_buf[s0], ins, (size_t)ins_len);
    w->text_field.ml_len = new_len;
    w->text_field.ml_buf[new_len] = '\0';
    w->text_field.cursor_pos = s0 + ins_len;
    tf_clear_selection(w);
    w->text_field.ensure_cursor_visible = 1;
    return 1;
}

static void tf_delete_selection(ui_widget_t *w)
{
    if (w->text_field.sel_start >= 0 && w->text_field.sel_end > w->text_field.sel_start)
        tf_replace_range(w, w->text_field.sel_start, w->text_field.sel_end, "", 0);
}

static void tf_emit_changed(ui_widget_t *w)
{
    if (w->on_event) {
        ui_event_t ev;
        memset(&ev, 0, sizeof(ev));
        ev.type = UI_EVENT_TEXT_CHANGED;
        ev.source = w;
        w->on_event(w, &ev, w->userdata);
    }
}

static void tf_apply_cursor_move(ui_widget_t *w, int32_t old_pos, int32_t new_pos, int shift)
{
    if (shift) {
        if (w->text_field.sel_anchor < 0)
            w->text_field.sel_anchor = old_pos;
        w->text_field.cursor_pos = new_pos;
        int32_t a = w->text_field.sel_anchor;
        if (new_pos == a) {
            w->text_field.sel_start = -1;
            w->text_field.sel_end = -1;
        } else if (new_pos < a) {
            w->text_field.sel_start = new_pos;
            w->text_field.sel_end = a;
        } else {
            w->text_field.sel_start = a;
            w->text_field.sel_end = new_pos;
        }
    } else {
        w->text_field.cursor_pos = new_pos;
        tf_clear_selection(w);
    }
    w->text_field.ensure_cursor_visible = 1;
    w->text_field.caret_ms = ui_current_time_ms();
    ui_widget_invalidate(w);
}

static void tf_move_vertical(ui_widget_t *w, int32_t line_delta, int shift)
{
    int32_t len = 0;
    const char *buf = tf_buf_ro(w, &len);
    int32_t cur = tf_clamp_pos(w->text_field.cursor_pos, len);

    int32_t cur_line = 0, ls = 0, le = 0;
    tf_pos_to_lc0(buf, len, cur, &cur_line, &ls, &le);

    int32_t target_line = cur_line + line_delta;
    int32_t max_line = tf_count_lines(buf, len) - 1;
    if (target_line < 0)
        target_line = 0;
    if (target_line > max_line)
        target_line = max_line;

    int32_t nls = 0, nle = 0;
    if (!tf_line_at_index(buf, len, target_line, &nls, &nle))
        return;

    float fsize = tf_font_size(w);
    int32_t target_px = tf_prefix_px(buf + ls, le - ls, cur - ls, fsize);
    int32_t col = tf_hit_test_col(buf + nls, nle - nls, target_px, fsize);
    tf_apply_cursor_move(w, cur, nls + col, shift);
}

ui_widget_t *ui_text_field(ui_ctx_t *ctx, const char *initial, ui_event_cb_t cb, void *ud)
{
    ui_widget_t *w = ui_alloc_widget(ctx, UI_WIDGET_TEXT_FIELD);
    if (!w)
        return NULL;

    w->on_event = cb;
    w->userdata = ud;
    w->text_field.multiline = 0;
    w->text_field.show_line_numbers = 0;
    w->text_field.read_only = 0;
    w->text_field.font_size = 0.0f;
    w->text_field.sel_start = -1;
    w->text_field.sel_end = -1;
    w->text_field.sel_anchor = -1;
    w->text_field.scroll_x = 0;
    w->text_field.scroll_y = 0;
    w->text_field.ensure_cursor_visible = 1;
    w->text_field.caret_ms = ui_current_time_ms();

    if (initial) {
        strncpy(w->text_field.buf, initial, sizeof(w->text_field.buf) - 1);
        w->text_field.buf[sizeof(w->text_field.buf) - 1] = '\0';
        w->text_field.cursor_pos = (int32_t)strlen(w->text_field.buf);
    } else {
        w->text_field.buf[0] = '\0';
        w->text_field.cursor_pos = 0;
    }

    return w;
}

ui_widget_t *ui_text_area(ui_ctx_t *ctx, const char *initial, int32_t show_line_numbers,
                          ui_event_cb_t cb, void *ud)
{
    ui_widget_t *w = ui_alloc_widget(ctx, UI_WIDGET_TEXT_FIELD);
    if (!w)
        return NULL;

    w->on_event = cb;
    w->userdata = ud;
    w->text_field.multiline = 1;
    w->text_field.show_line_numbers = show_line_numbers ? 1 : 0;
    w->text_field.read_only = 0;
    w->text_field.font_size = 0.0f;
    w->text_field.sel_start = -1;
    w->text_field.sel_end = -1;
    w->text_field.sel_anchor = -1;
    w->text_field.scroll_x = 0;
    w->text_field.scroll_y = 0;
    w->text_field.ensure_cursor_visible = 0;
    w->text_field.caret_ms = ui_current_time_ms();

    if (w->layout_elem) {
        w->layout_elem->w_mode = VSIZE_GROW;
        w->layout_elem->h_mode = VSIZE_GROW;
    }

    int32_t init_len = initial ? (int32_t)strlen(initial) : 0;
    if (tf_ensure_ml_cap(w, init_len)) {
        if (init_len > 0)
            memcpy(w->text_field.ml_buf, initial, (size_t)init_len);
        w->text_field.ml_buf[init_len] = '\0';
        w->text_field.ml_len = init_len;
    }
    w->text_field.cursor_pos = 0;
    return w;
}

const char *ui_text_field_get_text(const ui_widget_t *w)
{
    return tf_buf_ro(w, NULL);
}

void ui_text_field_set_text(ui_widget_t *w, const char *text)
{
    if (!w || w->type != UI_WIDGET_TEXT_FIELD)
        return;

    const char *src = text ? text : "";
    int32_t slen = (int32_t)strlen(src);
    if (!w->text_field.multiline) {
        if (slen >= (int32_t)sizeof(w->text_field.buf))
            slen = (int32_t)sizeof(w->text_field.buf) - 1;
        memcpy(w->text_field.buf, src, (size_t)slen);
        w->text_field.buf[slen] = '\0';
        w->text_field.cursor_pos = slen;
    } else {
        if (tf_ensure_ml_cap(w, slen)) {
            if (slen > 0)
                memcpy(w->text_field.ml_buf, src, (size_t)slen);
            w->text_field.ml_buf[slen] = '\0';
            w->text_field.ml_len = slen;
        }
        w->text_field.cursor_pos = 0;
        w->text_field.scroll_y = 0;
    }
    w->text_field.scroll_x = 0;
    tf_clear_selection(w);
    w->text_field.ensure_cursor_visible = 1;
    ui_widget_invalidate(w);
}

void ui_text_field_get_cursor_lc(const ui_widget_t *w, int32_t *out_line, int32_t *out_col)
{
    if (!w || w->type != UI_WIDGET_TEXT_FIELD) {
        if (out_line) *out_line = 1;
        if (out_col) *out_col = 1;
        return;
    }

    int32_t len = 0;
    const char *buf = tf_buf_ro(w, &len);
    int32_t pos = tf_clamp_pos(w->text_field.cursor_pos, len);
    int32_t line0 = 0, ls = 0, le = 0;
    tf_pos_to_lc0(buf, len, pos, &line0, &ls, &le);

    if (out_line)
        *out_line = line0 + 1;
    if (out_col)
        *out_col = (pos - ls) + 1;
}

int32_t ui_text_field_get_line_count(const ui_widget_t *w)
{
    int32_t len = 0;
    const char *buf = tf_buf_ro(w, &len);
    return tf_count_lines(buf, len);
}

void ui_text_field_select_all(ui_widget_t *w)
{
    if (!w || w->type != UI_WIDGET_TEXT_FIELD)
        return;

    int32_t len = 0;
    tf_buf_ro(w, &len);
    if (len > 0) {
        w->text_field.sel_anchor = 0;
        w->text_field.sel_start = 0;
        w->text_field.sel_end = len;
        w->text_field.cursor_pos = len;
    } else {
        tf_clear_selection(w);
        w->text_field.cursor_pos = 0;
    }
    w->text_field.ensure_cursor_visible = 1;
    ui_widget_invalidate(w);
}

void ui_text_field_free(ui_widget_t *w)
{
    if (!w || w->type != UI_WIDGET_TEXT_FIELD)
        return;
    if (w->text_field.ml_buf) {
        free(w->text_field.ml_buf);
        w->text_field.ml_buf = NULL;
        w->text_field.ml_len = 0;
        w->text_field.ml_cap = 0;
    }
}

void ui_text_field_handle_key_ctx(ui_ctx_t *ctx, ui_widget_t *w,
                                  const struct input_event *ev, int shift, int ctrl)
{
    if (!w || w->type != UI_WIDGET_TEXT_FIELD || !ev || ev->value == 0)
        return;

    int32_t len = 0;
    const char *buf = tf_buf_ro(w, &len);
    int32_t cur = tf_clamp_pos(w->text_field.cursor_pos, len);
    w->text_field.cursor_pos = cur;

    int text_changed = 0;

    if (ctrl) {
        if (ev->code == KEY_A) {
            ui_text_field_select_all(w);
            return;
        }
        if (ev->code == KEY_C || ev->code == KEY_X) {
            if (w->text_field.sel_start >= 0 && w->text_field.sel_end > w->text_field.sel_start) {
                int32_t s0 = tf_clamp_pos(w->text_field.sel_start, len);
                int32_t s1 = tf_clamp_pos(w->text_field.sel_end, len);
                if (s1 > s0 && ctx && ctx->clipboard_set_fn)
                    ctx->clipboard_set_fn(buf + s0, (size_t)(s1 - s0));
                if (ev->code == KEY_X && !w->text_field.read_only && s1 > s0) {
                    if (tf_replace_range(w, s0, s1, "", 0))
                        text_changed = 1;
                }
            }
            w->text_field.caret_ms = ui_current_time_ms();
            ui_widget_invalidate(w);
            if (text_changed)
                tf_emit_changed(w);
            return;
        }
        if (ev->code == KEY_V) {
            if (!w->text_field.read_only && ctx && ctx->clipboard_get_fn) {
                size_t cap = w->text_field.multiline ? 16384 : 256;
                char *tmp = (char *)malloc(cap);
                if (tmp) {
                    int n = ctx->clipboard_get_fn(tmp, cap);
                    if (n > 0) {
                        if ((size_t)n >= cap)
                            n = (int)(cap - 1);
                        tmp[n] = '\0';
                        int32_t ins_s0 = cur;
                        int32_t ins_s1 = cur;
                        if (w->text_field.sel_start >= 0 &&
                            w->text_field.sel_end > w->text_field.sel_start) {
                            ins_s0 = w->text_field.sel_start;
                            ins_s1 = w->text_field.sel_end;
                        }
                        if (tf_replace_range(w, ins_s0, ins_s1, tmp, n))
                            text_changed = 1;
                    }
                    free(tmp);
                }
            }
            w->text_field.caret_ms = ui_current_time_ms();
            ui_widget_invalidate(w);
            if (text_changed)
                tf_emit_changed(w);
            return;
        }
        if (ev->code == KEY_HOME) {
            tf_apply_cursor_move(w, cur, 0, shift);
            return;
        }
        if (ev->code == KEY_END) {
            tf_apply_cursor_move(w, cur, len, shift);
            return;
        }
        return;
    }

    if (ev->code == KEY_ENTER || ev->code == KEY_KPENTER) {
        if (w->text_field.multiline && !w->text_field.read_only) {
            int32_t s0 = cur, s1 = cur;
            if (w->text_field.sel_start >= 0 && w->text_field.sel_end > w->text_field.sel_start) {
                s0 = w->text_field.sel_start;
                s1 = w->text_field.sel_end;
            }
            if (tf_replace_range(w, s0, s1, "\n", 1))
                text_changed = 1;
        }
        w->text_field.caret_ms = ui_current_time_ms();
        ui_widget_invalidate(w);
        if (text_changed)
            tf_emit_changed(w);
        return;
    }

    if (ev->code == KEY_TAB) {
        if (w->text_field.multiline && !w->text_field.read_only) {
            int32_t s0 = cur, s1 = cur;
            if (w->text_field.sel_start >= 0 && w->text_field.sel_end > w->text_field.sel_start) {
                s0 = w->text_field.sel_start;
                s1 = w->text_field.sel_end;
            }
            if (tf_replace_range(w, s0, s1, "    ", 4))
                text_changed = 1;
        }
        w->text_field.caret_ms = ui_current_time_ms();
        ui_widget_invalidate(w);
        if (text_changed)
            tf_emit_changed(w);
        return;
    }

    char ch = vanilla_evdev_to_ascii(ev->code, shift);
    if (ch >= 0x20 && ch <= 0x7E) {
        if (!w->text_field.read_only) {
            int32_t s0 = cur, s1 = cur;
            if (w->text_field.sel_start >= 0 && w->text_field.sel_end > w->text_field.sel_start) {
                s0 = w->text_field.sel_start;
                s1 = w->text_field.sel_end;
            }
            if (tf_replace_range(w, s0, s1, &ch, 1))
                text_changed = 1;
        }
    } else if (ev->code == KEY_BACKSPACE) {
        if (!w->text_field.read_only) {
            if (w->text_field.sel_start >= 0 && w->text_field.sel_end > w->text_field.sel_start) {
                tf_delete_selection(w);
                text_changed = 1;
            } else if (cur > 0) {
                int32_t prev = tf_prev_char(buf, cur);
                if (tf_replace_range(w, prev, cur, "", 0))
                    text_changed = 1;
            }
        }
    } else if (ev->code == KEY_DELETE) {
        if (!w->text_field.read_only) {
            if (w->text_field.sel_start >= 0 && w->text_field.sel_end > w->text_field.sel_start) {
                tf_delete_selection(w);
                text_changed = 1;
            } else if (cur < len) {
                int32_t next = tf_next_char(buf, len, cur);
                if (tf_replace_range(w, cur, next, "", 0))
                    text_changed = 1;
            }
        }
    } else if (ev->code == KEY_LEFT) {
        if (!shift && w->text_field.sel_start >= 0 && w->text_field.sel_end > w->text_field.sel_start) {
            tf_apply_cursor_move(w, cur, w->text_field.sel_start, 0);
        } else {
            int32_t next = tf_prev_char(buf, cur);
            tf_apply_cursor_move(w, cur, next, shift);
        }
        return;
    } else if (ev->code == KEY_RIGHT) {
        if (!shift && w->text_field.sel_start >= 0 && w->text_field.sel_end > w->text_field.sel_start) {
            tf_apply_cursor_move(w, cur, w->text_field.sel_end, 0);
        } else {
            int32_t next = tf_next_char(buf, len, cur);
            tf_apply_cursor_move(w, cur, next, shift);
        }
        return;
    } else if (ev->code == KEY_UP) {
        if (w->text_field.multiline) {
            tf_move_vertical(w, -1, shift);
            return;
        }
    } else if (ev->code == KEY_DOWN) {
        if (w->text_field.multiline) {
            tf_move_vertical(w, 1, shift);
            return;
        }
    } else if (ev->code == KEY_PAGEUP || ev->code == KEY_PAGEDOWN) {
        if (w->text_field.multiline) {
            tf_geom_t g;
            tf_get_geom(w, &g);
            int32_t step = (g.line_h > 0 && g.text_h > g.line_h) ? (g.text_h / g.line_h) : 10;
            if (step < 1)
                step = 1;
            tf_move_vertical(w, (ev->code == KEY_PAGEUP) ? -step : step, shift);
            return;
        }
    } else if (ev->code == KEY_HOME) {
        if (w->text_field.multiline) {
            int32_t ls = 0;
            tf_line_bounds(buf, len, cur, &ls, NULL);
            tf_apply_cursor_move(w, cur, ls, shift);
        } else {
            tf_apply_cursor_move(w, cur, 0, shift);
        }
        return;
    } else if (ev->code == KEY_END) {
        if (w->text_field.multiline) {
            int32_t le = 0;
            tf_line_bounds(buf, len, cur, NULL, &le);
            tf_apply_cursor_move(w, cur, le, shift);
        } else {
            tf_apply_cursor_move(w, cur, len, shift);
        }
        return;
    }

    w->text_field.caret_ms = ui_current_time_ms();
    w->text_field.ensure_cursor_visible = 1;
    ui_widget_invalidate(w);

    if (text_changed)
        tf_emit_changed(w);
}

void ui_text_field_handle_key(ui_widget_t *w, const struct input_event *ev, int shift, int ctrl)
{
    ui_text_field_handle_key_ctx(NULL, w, ev, shift, ctrl);
}

static int32_t tf_point_to_offset(const ui_widget_t *w, const tf_geom_t *g,
                                  int32_t mx, int32_t my)
{
    int32_t len = 0;
    const char *buf = tf_buf_ro(w, &len);

    if (!w->text_field.multiline) {
        int32_t rel_x = mx - g->text_x + w->text_field.scroll_x;
        return tf_hit_test_col(buf, len, rel_x, g->fsize);
    }

    int32_t line_idx = 0;
    if (g->line_h > 0)
        line_idx = (my - g->text_y + w->text_field.scroll_y) / g->line_h;
    if (line_idx < 0)
        line_idx = 0;
    if (line_idx >= g->line_count)
        line_idx = g->line_count - 1;

    int32_t ls = 0, le = 0;
    if (!tf_line_at_index(buf, len, line_idx, &ls, &le))
        return len;

    int32_t rel_x = mx - g->text_x + w->text_field.scroll_x;
    int32_t col = tf_hit_test_col(buf + ls, le - ls, rel_x, g->fsize);
    return ls + col;
}

void ui_text_field_handle_click(ui_widget_t *w, int32_t click_x, int32_t click_y, int shift)
{
    if (!w || w->type != UI_WIDGET_TEXT_FIELD || !w->layout_elem)
        return;

    tf_geom_t g;
    tf_get_geom(w, &g);

    if (g.has_sb && !shift) {
        int32_t tx = 0, ty = 0, tw = 0, th = 0, by = 0, bh = 0, ms = 0;
        ui_scrollbar_metrics(g.x, g.y, g.width, g.height, g.content_h,
                             w->text_field.scroll_y, &tx, &ty, &tw, &th, &by, &bh, &ms);
        if (click_x >= g.x + g.width - UI_SCROLLBAR_W - 2) {
            if (click_y >= by && click_y < by + bh) {
                w->text_field.sb_dragging = 1;
                w->text_field.sb_drag_off = click_y - by;
            } else {
                int32_t step = (g.text_h > g.line_h) ? (g.text_h - g.line_h) : g.line_h * 4;
                if (click_y < by)
                    w->text_field.scroll_y -= step;
                else
                    w->text_field.scroll_y += step;
                if (w->text_field.scroll_y < 0)
                    w->text_field.scroll_y = 0;
                if (w->text_field.scroll_y > ms)
                    w->text_field.scroll_y = ms;
                w->text_field.ensure_cursor_visible = 0;
            }
            ui_widget_invalidate(w);
            return;
        }
    }

    int32_t old_pos = w->text_field.cursor_pos;
    int32_t new_pos = tf_point_to_offset(w, &g, click_x, click_y);
    tf_apply_cursor_move(w, old_pos, new_pos, shift);
    if (!shift)
        w->text_field.sel_anchor = new_pos;
}

void ui_text_field_handle_drag(ui_widget_t *w, int32_t mx, int32_t my)
{
    if (!w || w->type != UI_WIDGET_TEXT_FIELD || !w->layout_elem)
        return;

    tf_geom_t g;
    tf_get_geom(w, &g);

    if (w->text_field.sb_dragging && g.has_sb) {
        int32_t tx = 0, ty = 0, tw = 0, th = 0, by = 0, bh = 0, ms = 0;
        ui_scrollbar_metrics(g.x, g.y, g.width, g.height, g.content_h,
                             w->text_field.scroll_y, &tx, &ty, &tw, &th, &by, &bh, &ms);
        int32_t travel = th - bh;
        if (travel > 0 && ms > 0) {
            int32_t new_by = my - w->text_field.sb_drag_off;
            if (new_by < ty)
                new_by = ty;
            if (new_by > ty + travel)
                new_by = ty + travel;
            w->text_field.scroll_y = ((new_by - ty) * ms) / travel;
            if (w->text_field.scroll_y < 0)
                w->text_field.scroll_y = 0;
            if (w->text_field.scroll_y > ms)
                w->text_field.scroll_y = ms;
        }
        w->text_field.ensure_cursor_visible = 0;
        ui_widget_invalidate(w);
        return;
    }

    int32_t anchor = w->text_field.sel_anchor;
    if (anchor < 0)
        anchor = w->text_field.cursor_pos;
    int32_t pos = tf_point_to_offset(w, &g, mx, my);
    w->text_field.sel_anchor = anchor;
    w->text_field.cursor_pos = pos;
    if (pos == anchor) {
        w->text_field.sel_start = -1;
        w->text_field.sel_end = -1;
    } else if (pos < anchor) {
        w->text_field.sel_start = pos;
        w->text_field.sel_end = anchor;
    } else {
        w->text_field.sel_start = anchor;
        w->text_field.sel_end = pos;
    }
    w->text_field.ensure_cursor_visible = 1;
    ui_widget_invalidate(w);
}

void ui_text_field_handle_release(ui_widget_t *w)
{
    if (!w || w->type != UI_WIDGET_TEXT_FIELD)
        return;
    if (w->text_field.sb_dragging) {
        w->text_field.sb_dragging = 0;
        ui_widget_invalidate(w);
    }
}

void ui_text_field_handle_wheel(ui_widget_t *w, int32_t delta)
{
    if (!w || w->type != UI_WIDGET_TEXT_FIELD || !w->text_field.multiline || delta == 0)
        return;

    tf_geom_t g;
    tf_get_geom(w, &g);
    int32_t step = g.line_h * 3;
    if (step < 16)
        step = 16;

    w->text_field.scroll_y -= delta * step;
    if (w->text_field.scroll_y < 0)
        w->text_field.scroll_y = 0;
    if (w->text_field.scroll_y > g.max_scroll_y)
        w->text_field.scroll_y = g.max_scroll_y;
    w->text_field.ensure_cursor_visible = 0;
    ui_widget_invalidate(w);
}

void ui_text_field_update_blink(ui_widget_t *w)
{
    if (!w || w->type != UI_WIDGET_TEXT_FIELD || !w->focused)
        return;

    int64_t now = ui_current_time_ms();
    int prev_phase = (int)((w->text_field.caret_ms / 530) % 2);
    int curr_phase = (int)((now / 530) % 2);
    if (curr_phase != prev_phase) {
        w->text_field.caret_ms = now;
        ui_widget_invalidate(w);
    }
}

static void tf_emit_line_number(vanilla_draw_cmd_array_t *out, const tf_geom_t *g,
                                int32_t line_num_1, int32_t ly, uint32_t col)
{
    static char lnum_pool[TF_LINE_NUM_POOL][12];
    static int lnum_idx = 0;

    char *slot = lnum_pool[lnum_idx];
    lnum_idx = (lnum_idx + 1) % TF_LINE_NUM_POOL;
    snprintf(slot, 12, "%d", line_num_1);

    int32_t nw = 0;
    app_measure_text_ex(slot, g->fsize, APP_FONT_REGULAR, &nw, NULL);
    int32_t nx = g->gutter_x + g->gutter_w - nw - 6;
    if (nx < g->gutter_x + 2)
        nx = g->gutter_x + 2;

    vanilla_draw_cmd_t cmd;
    memset(&cmd, 0, sizeof(cmd));
    cmd.type = VCMD_TEXT;
    cmd.bounds.x = nx;
    cmd.bounds.y = ly + (g->line_h - g->glyph_h) / 2;
    cmd.bounds.w = nw > 0 ? nw : 1;
    cmd.bounds.h = g->glyph_h;
    cmd.text.text = slot;
    cmd.text.color = col;
    cmd.text.font_size = g->fsize;
    cmd.text.font_weight = APP_FONT_REGULAR;
    push_cmd(out, cmd);
}

void ui_text_field_emit_draw(ui_widget_t *w, vanilla_draw_cmd_array_t *out)
{
    if (!w || !w->layout_elem || !out || w->hidden)
        return;

    tf_geom_t g;
    tf_get_geom(w, &g);
    if (g.width <= 0 || g.height <= 0)
        return;

    uint32_t bg_base = g_theme ? g_theme->bg_base : 0xFF2E3440u;
    uint32_t bg_col = g_theme ? g_theme->bg_elevated : 0xFF3B4252u;
    uint32_t base_border = g_theme ? g_theme->border : 0xFF4C566Au;
    uint32_t border_col = w->focused ?
                          (g_theme ? g_theme->border_focus : 0xFF88C0D0u) :
                          base_border;
    uint32_t text_col = g_theme ? g_theme->fg_primary : 0xFFECEFF4u;
    uint32_t fg_dim = g_theme ? g_theme->fg_dim : 0xFF98A2B3u;
    uint32_t sel_col = g_theme ? g_theme->selection : 0x4488C0D0u;
    uint32_t accent_col = g_theme ? g_theme->accent : 0xFF88C0D0u;

    vanilla_draw_cmd_t cmd;
    memset(&cmd, 0, sizeof(cmd));
    cmd.type = VCMD_ROUNDED_RECT;
    cmd.bounds.x = g.x;
    cmd.bounds.y = g.y;
    cmd.bounds.w = g.width;
    cmd.bounds.h = g.height;
    cmd.rounded_rect.color = bg_col;
    cmd.rounded_rect.radius = w->text_field.multiline ? 2 : 4;
    push_cmd(out, cmd);

    if (g.gutter_w > 0) {
        memset(&cmd, 0, sizeof(cmd));
        cmd.type = VCMD_FILL_RECT;
        cmd.bounds.x = g.gutter_x;
        cmd.bounds.y = g.y + 1;
        cmd.bounds.w = g.gutter_w - 1;
        cmd.bounds.h = g.height > 2 ? g.height - 2 : g.height;
        cmd.fill_rect.color = bg_base;
        push_cmd(out, cmd);

        memset(&cmd, 0, sizeof(cmd));
        cmd.type = VCMD_FILL_RECT;
        cmd.bounds.x = g.gutter_x + g.gutter_w - 1;
        cmd.bounds.y = g.y + 1;
        cmd.bounds.w = 1;
        cmd.bounds.h = g.height > 2 ? g.height - 2 : g.height;
        cmd.fill_rect.color = base_border;
        push_cmd(out, cmd);
    }

    memset(&cmd, 0, sizeof(cmd));
    cmd.type = VCMD_BORDER;
    cmd.bounds.x = g.x;
    cmd.bounds.y = g.y;
    cmd.bounds.w = g.width;
    cmd.bounds.h = g.height;
    cmd.border.color = border_col;
    cmd.border.width = (w->focused && !w->text_field.multiline) ? 2 : 1;
    push_cmd(out, cmd);

    int32_t len = 0;
    const char *buf = tf_buf_ro(w, &len);
    int32_t cur = tf_clamp_pos(w->text_field.cursor_pos, len);

    int32_t cur_line = 0, cls = 0, cle = 0;
    tf_pos_to_lc0(buf, len, cur, &cur_line, &cls, &cle);
    int32_t cursor_px = tf_prefix_px(buf + cls, cle - cls, cur - cls, g.fsize);

    if (w->text_field.ensure_cursor_visible) {
        if (cursor_px - w->text_field.scroll_x > g.text_w - 8) {
            if (g.text_w > 24)
                w->text_field.scroll_x = cursor_px - g.text_w + 20;
            else
                w->text_field.scroll_x = cursor_px;
        }
        if (cursor_px - w->text_field.scroll_x < 0)
            w->text_field.scroll_x = (cursor_px > 16) ? (cursor_px - 16) : 0;
        if (w->text_field.scroll_x < 0)
            w->text_field.scroll_x = 0;

        if (w->text_field.multiline && g.text_h > 0) {
            int32_t cur_top = cur_line * g.line_h;
            int32_t cur_bot = cur_top + g.line_h;
            if (cur_top < w->text_field.scroll_y)
                w->text_field.scroll_y = cur_top;
            if (cur_bot > w->text_field.scroll_y + g.text_h)
                w->text_field.scroll_y = cur_bot - g.text_h;
        }
        w->text_field.ensure_cursor_visible = 0;
    }

    if (w->text_field.multiline) {
        if (w->text_field.scroll_y < 0)
            w->text_field.scroll_y = 0;
        if (w->text_field.scroll_y > g.max_scroll_y)
            w->text_field.scroll_y = g.max_scroll_y;
    }

    if (g.gutter_w > 0 && g.text_h > 0) {
        memset(&cmd, 0, sizeof(cmd));
        cmd.type = VCMD_PUSH_CLIP;
        cmd.bounds.x = g.gutter_x;
        cmd.bounds.y = g.text_y;
        cmd.bounds.w = g.gutter_w - 2 > 0 ? g.gutter_w - 2 : 0;
        cmd.bounds.h = g.text_h;
        push_cmd(out, cmd);

        int32_t first_vis = (g.line_h > 0) ? (w->text_field.scroll_y / g.line_h) : 0;
        if (first_vis < 0)
            first_vis = 0;
        for (int32_t i = first_vis; i < g.line_count; i++) {
            int32_t ly = g.text_y + i * g.line_h - w->text_field.scroll_y;
            if (ly >= g.text_y + g.text_h)
                break;
            tf_emit_line_number(out, &g, i + 1, ly,
                                (i == cur_line && w->focused) ? text_col : fg_dim);
        }

        memset(&cmd, 0, sizeof(cmd));
        cmd.type = VCMD_POP_CLIP;
        push_cmd(out, cmd);
    }

    memset(&cmd, 0, sizeof(cmd));
    cmd.type = VCMD_PUSH_CLIP;
    cmd.bounds.x = g.text_x;
    cmd.bounds.y = g.text_y;
    cmd.bounds.w = g.text_w;
    cmd.bounds.h = g.text_h;
    push_cmd(out, cmd);

    int has_sel = (w->text_field.sel_start >= 0 && w->text_field.sel_end > w->text_field.sel_start);
    int32_t s0 = has_sel ? tf_clamp_pos(w->text_field.sel_start, len) : -1;
    int32_t s1 = has_sel ? tf_clamp_pos(w->text_field.sel_end, len) : -1;

    if (!w->text_field.multiline) {
        if (has_sel && s1 > s0) {
            int32_t x0 = tf_prefix_px(buf, len, s0, g.fsize);
            int32_t x1 = tf_prefix_px(buf, len, s1, g.fsize);
            memset(&cmd, 0, sizeof(cmd));
            cmd.type = VCMD_FILL_RECT;
            cmd.bounds.x = g.text_x + x0 - w->text_field.scroll_x;
            cmd.bounds.y = g.text_y;
            cmd.bounds.w = x1 - x0;
            cmd.bounds.h = g.text_h;
            cmd.fill_rect.color = sel_col;
            push_cmd(out, cmd);
        }

        if (len > 0) {
            int32_t full_w = tf_prefix_px(buf, len, len, g.fsize);
            memset(&cmd, 0, sizeof(cmd));
            cmd.type = VCMD_TEXT;
            cmd.bounds.x = g.text_x - w->text_field.scroll_x;
            cmd.bounds.y = g.text_y + (g.text_h - g.glyph_h) / 2;
            cmd.bounds.w = full_w > 0 ? full_w : 1;
            cmd.bounds.h = g.glyph_h;
            cmd.text.text = buf;
            cmd.text.color = text_col;
            cmd.text.font_size = g.fsize;
            cmd.text.font_weight = APP_FONT_REGULAR;
            cmd.text.max_bytes = 0;
            push_cmd(out, cmd);
        }

        if (w->focused && !w->text_field.read_only) {
            int64_t now = ui_current_time_ms();
            if ((now / 530) % 2 == 0) {
                memset(&cmd, 0, sizeof(cmd));
                cmd.type = VCMD_FILL_RECT;
                cmd.bounds.x = g.text_x + cursor_px - w->text_field.scroll_x;
                cmd.bounds.y = g.text_y + (g.text_h - g.glyph_h) / 2;
                cmd.bounds.w = 2;
                cmd.bounds.h = g.glyph_h;
                cmd.fill_rect.color = accent_col;
                push_cmd(out, cmd);
            }
        }
    } else {
        int32_t line_idx = 0;
        int32_t ls = 0;
        for (int32_t i = 0; i <= len; i++) {
            if (i < len && buf[i] != '\n')
                continue;

            int32_t le = i;
            if (le > ls && buf[le - 1] == '\r')
                le--;

            int32_t ly = g.text_y + line_idx * g.line_h - w->text_field.scroll_y;
            if (ly >= g.text_y + g.text_h)
                break;

            if (ly + g.line_h > g.text_y) {
                int32_t line_len = le - ls;

                if (has_sel && s0 <= i && s1 >= ls) {
                    int32_t a = (s0 > ls) ? (s0 - ls) : 0;
                    int32_t b = (s1 < le) ? (s1 - ls) : line_len;
                    int32_t sx = tf_prefix_px(buf + ls, line_len, a, g.fsize);
                    int32_t ex = tf_prefix_px(buf + ls, line_len, b, g.fsize);
                    if (s1 > i)
                        ex += 6;
                    if (ex > sx) {
                        memset(&cmd, 0, sizeof(cmd));
                        cmd.type = VCMD_FILL_RECT;
                        cmd.bounds.x = g.text_x + sx - w->text_field.scroll_x;
                        cmd.bounds.y = ly;
                        cmd.bounds.w = ex - sx;
                        cmd.bounds.h = g.line_h;
                        cmd.fill_rect.color = sel_col;
                        push_cmd(out, cmd);
                    }
                }

                if (line_len > 0) {
                    int32_t lw = tf_prefix_px(buf + ls, line_len, line_len, g.fsize);
                    memset(&cmd, 0, sizeof(cmd));
                    cmd.type = VCMD_TEXT;
                    cmd.bounds.x = g.text_x - w->text_field.scroll_x;
                    cmd.bounds.y = ly + (g.line_h - g.glyph_h) / 2;
                    cmd.bounds.w = lw > 0 ? lw : 1;
                    cmd.bounds.h = g.glyph_h;
                    cmd.text.text = buf + ls;
                    cmd.text.color = text_col;
                    cmd.text.font_size = g.fsize;
                    cmd.text.font_weight = APP_FONT_REGULAR;
                    cmd.text.max_bytes = (uint32_t)line_len;
                    push_cmd(out, cmd);
                }
            }

            line_idx++;
            ls = i + 1;
        }

        if (w->focused && !w->text_field.read_only) {
            int64_t now = ui_current_time_ms();
            if ((now / 530) % 2 == 0) {
                int32_t cy = g.text_y + cur_line * g.line_h - w->text_field.scroll_y;
                if (cy + g.line_h > g.text_y && cy < g.text_y + g.text_h) {
                    memset(&cmd, 0, sizeof(cmd));
                    cmd.type = VCMD_FILL_RECT;
                    cmd.bounds.x = g.text_x + cursor_px - w->text_field.scroll_x;
                    cmd.bounds.y = cy + 1;
                    cmd.bounds.w = 2;
                    cmd.bounds.h = g.line_h > 2 ? g.line_h - 2 : g.line_h;
                    cmd.fill_rect.color = accent_col;
                    push_cmd(out, cmd);
                }
            }
        }
    }

    memset(&cmd, 0, sizeof(cmd));
    cmd.type = VCMD_POP_CLIP;
    push_cmd(out, cmd);

    if (g.has_sb) {
        int32_t tx = 0, ty = 0, tw = 0, th = 0, by = 0, bh = 0;
        ui_scrollbar_metrics(g.x, g.y, g.width, g.height, g.content_h,
                             w->text_field.scroll_y, &tx, &ty, &tw, &th, &by, &bh, NULL);
        memset(&cmd, 0, sizeof(cmd));
        cmd.type = VCMD_FILL_RECT;
        cmd.bounds.x = tx;
        cmd.bounds.y = ty;
        cmd.bounds.w = tw;
        cmd.bounds.h = th;
        cmd.fill_rect.color = bg_base;
        push_cmd(out, cmd);

        memset(&cmd, 0, sizeof(cmd));
        cmd.type = VCMD_ROUNDED_RECT;
        cmd.bounds.x = tx + 1;
        cmd.bounds.y = by;
        cmd.bounds.w = tw > 2 ? tw - 2 : tw;
        cmd.bounds.h = bh;
        cmd.rounded_rect.color = w->text_field.sb_dragging ? accent_col : fg_dim;
        cmd.rounded_rect.radius = 3;
        push_cmd(out, cmd);
    }
}
