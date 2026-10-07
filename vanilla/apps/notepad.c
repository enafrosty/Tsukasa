/*
 * Project Tsukasa — Project Vanilla Text Editor
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

#include "app_common.h"
#include "../include/ui.h"
#include "../include/ui_widgets.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <time.h>
#include <sys/time.h>

#define NOTE_DEFAULT_W  560
#define NOTE_DEFAULT_H  380
#define TOOLBAR_HEIGHT  32
#define STATUS_HEIGHT   24
#define GUTTER_WIDTH    44
#define LINE_HEIGHT     16

#define MAX_LINES       512
#define MAX_LINE_LEN    128

typedef struct {
    char lines[MAX_LINES][MAX_LINE_LEN];
    int  num_lines;
    int  cursor_line;
    int  cursor_col;
    int  scroll_line;
    char filename[128];
    char status_str[128];

    /* Selection range: anchor to cursor */
    int  sel_anchor_line;
    int  sel_anchor_col;
    int  has_selection;

    int  shift_down;
    int  ctrl_down;
    int  mouse_down;
    int  dirty;

    ui_ctx_t    *ui_ctx;
    ui_widget_t *root;
    ui_widget_t *path_label;
    ui_widget_t *status_label;
    ui_widget_t *editor_box;
} notepad_state_t;

static int64_t note_now_ms(void)
{
    struct timeval tv;
    if (gettimeofday(&tv, NULL) == 0)
        return (int64_t)tv.tv_sec * 1000 + (int64_t)(tv.tv_usec / 1000);
    return 0;
}

static void note_update_status(notepad_state_t *st)
{
    int total_chars = 0;
    for (int i = 0; i < st->num_lines; i++)
        total_chars += (int)strlen(st->lines[i]);

    snprintf(st->status_str, sizeof(st->status_str),
             "Ln %d, Col %d  |  %d chars  |  UTF-8",
             st->cursor_line + 1, st->cursor_col + 1, total_chars);

    if (st->status_label) {
        st->status_label->label.text = st->status_str;
        ui_widget_invalidate(st->status_label);
    }
}

static void note_init(notepad_state_t *st, const char *path)
{
    st->num_lines = 1;
    st->lines[0][0] = '\0';
    st->cursor_line = 0;
    st->cursor_col = 0;
    st->scroll_line = 0;
    st->has_selection = 0;
    st->dirty = 1;

    if (path && path[0]) {
        strncpy(st->filename, path, sizeof(st->filename) - 1);
        st->filename[sizeof(st->filename) - 1] = '\0';
        int fd = open(path, O_RDONLY);
        if (fd >= 0) {
            char buf[1024];
            ssize_t n;
            int cur_l = 0;
            int cur_c = 0;
            int at_max = 0;
            while (!at_max && (n = read(fd, buf, sizeof(buf))) > 0) {
                for (ssize_t i = 0; i < n; i++) {
                    char c = buf[i];
                    if (c == '\r')
                        continue;
                    if (c == '\n') {
                        st->lines[cur_l][cur_c] = '\0';
                        if (cur_l >= MAX_LINES - 1) {
                            at_max = 1;
                            break;
                        }
                        cur_l++;
                        cur_c = 0;
                    } else if (cur_c < MAX_LINE_LEN - 1) {
                        st->lines[cur_l][cur_c++] = c;
                    }
                }
            }
            st->lines[cur_l][cur_c] = '\0';
            st->num_lines = cur_l + 1;
            close(fd);
        }
    } else {
        strcpy(st->filename, "/tmp/untitled.txt");
    }

    if (st->path_label) {
        st->path_label->label.text = st->filename;
        ui_widget_invalidate(st->path_label);
    }
    note_update_status(st);
}

static void note_save_file(notepad_state_t *st)
{
    int fd = open(st->filename, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd < 0)
        return;

    for (int i = 0; i < st->num_lines; i++) {
        size_t len = strlen(st->lines[i]);
        if (len > 0)
            write(fd, st->lines[i], len);
        write(fd, "\n", 1);
    }
    close(fd);
    st->dirty = 1;
}

static void get_selection_bounds(notepad_state_t *st,
                                 int *s_line, int *s_col,
                                 int *e_line, int *e_col)
{
    if (st->sel_anchor_line < st->cursor_line ||
        (st->sel_anchor_line == st->cursor_line && st->sel_anchor_col <= st->cursor_col)) {
        *s_line = st->sel_anchor_line;
        *s_col  = st->sel_anchor_col;
        *e_line = st->cursor_line;
        *e_col  = st->cursor_col;
    } else {
        *s_line = st->cursor_line;
        *s_col  = st->cursor_col;
        *e_line = st->sel_anchor_line;
        *e_col  = st->sel_anchor_col;
    }
}

static void note_delete_selection(notepad_state_t *st)
{
    if (!st->has_selection)
        return;

    int s_line, s_col, e_line, e_col;
    get_selection_bounds(st, &s_line, &s_col, &e_line, &e_col);

    if (s_line == e_line) {
        char *l = st->lines[s_line];
        int len = (int)strlen(l);
        if (e_col > len) e_col = len;
        if (s_col < 0) s_col = 0;
        if (s_col < e_col)
            memmove(l + s_col, l + e_col, len - e_col + 1);
    } else {
        char *sl = st->lines[s_line];
        char *el = st->lines[e_line];
        int el_len = (int)strlen(el);
        if (e_col > el_len) e_col = el_len;

        /* Combine end remainder into start line */
        int sl_len = s_col;
        if (sl_len + (el_len - e_col) < MAX_LINE_LEN) {
            memcpy(sl + sl_len, el + e_col, el_len - e_col + 1);
        } else {
            sl[sl_len] = '\0';
        }

        /* Shift remaining lines up */
        int removed = e_line - s_line;
        for (int i = s_line + 1; i + removed < st->num_lines; i++)
            strcpy(st->lines[i], st->lines[i + removed]);

        st->num_lines -= removed;
        if (st->num_lines < 1)
            st->num_lines = 1;
    }

    st->cursor_line = s_line;
    st->cursor_col = s_col;
    st->has_selection = 0;
    st->dirty = 1;
    note_update_status(st);
}

static void note_copy_selection(notepad_state_t *st)
{
    if (!st->has_selection) {
        /* Default: copy current line */
        const char *cur_line = st->lines[st->cursor_line];
        clipboard_set(cur_line, strlen(cur_line));
        return;
    }

    int s_line, s_col, e_line, e_col;
    get_selection_bounds(st, &s_line, &s_col, &e_line, &e_col);

    char *copy_buf = (char *)malloc(VCLIP_TEXT_MAX + 1);
    if (!copy_buf)
        return;

    size_t out_len = 0;
    for (int l = s_line; l <= e_line && out_len < VCLIP_TEXT_MAX; l++) {
        char *line = st->lines[l];
        int len = (int)strlen(line);
        int c_start = (l == s_line) ? s_col : 0;
        int c_end = (l == e_line) ? e_col : len;
        if (c_start < 0) c_start = 0;
        if (c_end > len) c_end = len;

        if (c_end > c_start) {
            size_t seg = (size_t)(c_end - c_start);
            if (out_len + seg > VCLIP_TEXT_MAX)
                seg = VCLIP_TEXT_MAX - out_len;
            memcpy(copy_buf + out_len, line + c_start, seg);
            out_len += seg;
        }

        if (l < e_line && out_len < VCLIP_TEXT_MAX)
            copy_buf[out_len++] = '\n';
    }

    copy_buf[out_len] = '\0';
    clipboard_set(copy_buf, out_len);
    free(copy_buf);
}

static void note_insert_char(notepad_state_t *st, char c)
{
    if (st->has_selection)
        note_delete_selection(st);

    char *l = st->lines[st->cursor_line];
    int len = (int)strlen(l);

    if (c == '\n') {
        if (st->num_lines >= MAX_LINES)
            return;

        for (int i = st->num_lines; i > st->cursor_line + 1; i--)
            strcpy(st->lines[i], st->lines[i - 1]);

        strcpy(st->lines[st->cursor_line + 1], l + st->cursor_col);
        l[st->cursor_col] = '\0';

        st->num_lines++;
        st->cursor_line++;
        st->cursor_col = 0;
        st->dirty = 1;
        note_update_status(st);
        return;
    }

    if (c == '\b') {
        if (st->cursor_col > 0) {
            memmove(l + st->cursor_col - 1, l + st->cursor_col, len - st->cursor_col + 1);
            st->cursor_col--;
            st->dirty = 1;
        } else if (st->cursor_line > 0) {
            char *prev = st->lines[st->cursor_line - 1];
            int prev_len = (int)strlen(prev);
            if (prev_len + len < MAX_LINE_LEN) {
                strcat(prev, l);
                for (int i = st->cursor_line; i < st->num_lines - 1; i++)
                    strcpy(st->lines[i], st->lines[i + 1]);
                st->num_lines--;
                st->cursor_line--;
                st->cursor_col = prev_len;
                st->dirty = 1;
            }
        }
        note_update_status(st);
        return;
    }

    if (c == '\t') {
        for (int s = 0; s < 4; s++) {
            if (len < MAX_LINE_LEN - 1) {
                memmove(l + st->cursor_col + 1, l + st->cursor_col, len - st->cursor_col + 1);
                l[st->cursor_col] = ' ';
                st->cursor_col++;
                len++;
            }
        }
        st->dirty = 1;
        note_update_status(st);
        return;
    }

    if (len < MAX_LINE_LEN - 1 && (unsigned char)c >= 32) {
        memmove(l + st->cursor_col + 1, l + st->cursor_col, len - st->cursor_col + 1);
        l[st->cursor_col] = c;
        st->cursor_col++;
        st->dirty = 1;
        note_update_status(st);
    }
}

static void on_btn_new(ui_widget_t *w, const ui_event_t *ev, void *ud)
{
    (void)w;
    if (ev->type != UI_EVENT_CLICK) return;
    notepad_state_t *st = (notepad_state_t *)ud;
    note_init(st, "/tmp/untitled.txt");
    st->dirty = 1;
}

static void on_btn_save(ui_widget_t *w, const ui_event_t *ev, void *ud)
{
    (void)w;
    if (ev->type != UI_EVENT_CLICK) return;
    notepad_state_t *st = (notepad_state_t *)ud;
    note_save_file(st);
}

static void note_render_editor(vanilla_surface_t *surf, notepad_state_t *st)
{
    if (!st->editor_box || !st->editor_box->layout_elem)
        return;

    int ed_x = st->editor_box->layout_elem->computed_x;
    int ed_y = st->editor_box->layout_elem->computed_y;
    int ed_w = st->editor_box->layout_elem->computed_w;
    int ed_h = st->editor_box->layout_elem->computed_h;

    if (ed_w <= 0 || ed_h <= 0)
        return;

    uint32_t bg_base = g_theme ? g_theme->bg_base : 0xFF2E3440u;
    uint32_t gutter_bg = g_theme ? g_theme->taskbar_bg : 0xFF3B4252u;
    uint32_t border_col = g_theme ? g_theme->border : 0xFF4C566Au;
    uint32_t fg_primary = g_theme ? g_theme->fg_primary : 0xFFECEFF4u;
    uint32_t fg_dim = g_theme ? g_theme->fg_dim : 0xFF4C566Au;
    uint32_t sel_col = g_theme ? g_theme->selection : 0xFF434C5Eu;
    uint32_t accent_col = g_theme ? g_theme->accent : 0xFF88C0D0u;

    /* Fill editor background */
    app_fill_rect(surf, ed_x, ed_y, ed_w, ed_h, bg_base);

    /* Gutter */
    app_fill_rect(surf, ed_x, ed_y, GUTTER_WIDTH, ed_h, gutter_bg);
    app_fill_rect(surf, ed_x + GUTTER_WIDTH - 1, ed_y, 1, ed_h, border_col);

    int visible_lines = ed_h / LINE_HEIGHT;
    if (st->cursor_line < st->scroll_line)
        st->scroll_line = st->cursor_line;
    if (st->cursor_line >= st->scroll_line + visible_lines)
        st->scroll_line = st->cursor_line - visible_lines + 1;

    int s_line = 0, s_col = 0, e_line = 0, e_col = 0;
    if (st->has_selection)
        get_selection_bounds(st, &s_line, &s_col, &e_line, &e_col);

    for (int i = 0; i < visible_lines; i++) {
        int l_idx = st->scroll_line + i;
        if (l_idx >= st->num_lines)
            break;

        int line_y = ed_y + i * LINE_HEIGHT + 3;

        /* Line number */
        char lno[12];
        snprintf(lno, sizeof(lno), "%3d", l_idx + 1);
        app_draw_text(surf, ed_x + 6, line_y, lno, fg_dim);

        /* Highlight selection on this line */
        int text_x = ed_x + GUTTER_WIDTH + 8;
        char *line_txt = st->lines[l_idx];
        int line_len = (int)strlen(line_txt);

        if (st->has_selection && l_idx >= s_line && l_idx <= e_line) {
            int c0 = (l_idx == s_line) ? s_col : 0;
            int c1 = (l_idx == e_line) ? e_col : line_len;
            if (c0 < 0) c0 = 0;
            if (c1 > line_len) c1 = line_len;
            if (c1 > c0) {
                int sx = text_x + c0 * 8;
                int sw = (c1 - c0) * 8;
                app_fill_rect(surf, sx, line_y - 2, sw, LINE_HEIGHT, sel_col);
            }
        }

        /* Line text */
        app_draw_text(surf, text_x, line_y, line_txt, fg_primary);

        /* Cursor with 500ms blinking */
        if (l_idx == st->cursor_line) {
            int blink_on = ((note_now_ms() / 500) % 2 == 0);
            if (blink_on) {
                int cx = text_x + st->cursor_col * 8;
                app_fill_rect(surf, cx, line_y - 2, 2, LINE_HEIGHT - 2, accent_col);
            }
        }
    }

    /* Scrollbar if content exceeds viewport */
    if (st->num_lines > visible_lines && ed_h > 30) {
        int sb_w = 4;
        int sb_x = ed_x + ed_w - sb_w - 2;
        int thumb_h = (ed_h * visible_lines) / st->num_lines;
        if (thumb_h < 14) thumb_h = 14;
        int max_scroll = st->num_lines - visible_lines;
        int thumb_y = ed_y + (st->scroll_line * (ed_h - thumb_h)) / (max_scroll > 0 ? max_scroll : 1);
        app_fill_rect(surf, sb_x, thumb_y, sb_w, thumb_h, accent_col);
    }
}

int main(int argc, char **argv)
{
    notepad_state_t *state = (notepad_state_t *)malloc(sizeof(notepad_state_t));
    if (!state) {
        fprintf(stderr, "notepad: memory allocation failed\n");
        return 1;
    }
    memset(state, 0, sizeof(*state));

    const char *sock_path = VANILLA_SOCKET_PATH;
    vanilla_client_t *client = NULL;
    for (int retry = 0; retry < 50; retry++) {
        client = vanilla_connect(sock_path);
        if (client)
            break;
        struct timespec ts = { 0, 10000000 }; /* 10ms */
        nanosleep(&ts, NULL);
    }
    if (!client) {
        fprintf(stderr, "notepad: failed to connect to display server\n");
        free(state);
        return 1;
    }

    vanilla_window_t *win = vanilla_create_window(client, "Notepad", 120, 100,
                                                  NOTE_DEFAULT_W, NOTE_DEFAULT_H,
                                                  WINDOW_FLAG_RESIZABLE);
    if (!win) {
        vanilla_disconnect(client);
        free(state);
        return 1;
    }

    vanilla_set_size_hints(client, win->window_id, 160, 120, 0, 0, 0, 0);
    vanilla_map_window(win);

    static uint8_t ui_arena[128 * 1024];
    state->ui_ctx = ui_ctx_init(ui_arena, sizeof(ui_arena), NULL);
    if (!state->ui_ctx) {
        vanilla_destroy_window(win);
        vanilla_disconnect(client);
        free(state);
        return 1;
    }

    uint32_t bg = g_theme ? g_theme->bg_base : 0xFF2E3440u;
    uint32_t elev = g_theme ? g_theme->bg_elevated : 0xFF3B4252u;
    uint32_t bdr = g_theme ? g_theme->border : 0xFF4C566Au;
    uint32_t fg_muted = g_theme ? g_theme->fg_muted : 0xFFD8DEE9u;

    state->root = ui_box(state->ui_ctx, VDIR_COLUMN);
    state->root->layout_elem->w_mode = VSIZE_GROW;
    state->root->layout_elem->h_mode = VSIZE_GROW;
    state->root->layout_elem->bg_color = bg;

    /* Toolbar */
    ui_widget_t *toolbar = ui_box(state->ui_ctx, VDIR_ROW);
    toolbar->layout_elem->w_mode = VSIZE_GROW;
    toolbar->layout_elem->h_mode = VSIZE_FIXED;
    toolbar->layout_elem->h_px = TOOLBAR_HEIGHT;
    toolbar->layout_elem->gap = 8;
    toolbar->layout_elem->pad_left = toolbar->layout_elem->pad_right = 8;
    toolbar->layout_elem->align_items = VALIGN_CENTER;
    toolbar->layout_elem->bg_color = elev;
    toolbar->layout_elem->border_color = bdr;
    toolbar->layout_elem->border_width = 1;
    ui_widget_add_child(state->root, toolbar);

    ui_widget_t *btn_new = ui_button(state->ui_ctx, "New", on_btn_new, state);
    ui_widget_add_child(toolbar, btn_new);

    ui_widget_t *btn_save = ui_button(state->ui_ctx, "Save", on_btn_save, state);
    ui_widget_add_child(toolbar, btn_save);

    state->path_label = ui_label(state->ui_ctx, "/tmp/untitled.txt", fg_muted);
    ui_widget_add_child(toolbar, state->path_label);

    /* Editor Box */
    state->editor_box = ui_box(state->ui_ctx, VDIR_ROW);
    state->editor_box->layout_elem->w_mode = VSIZE_GROW;
    state->editor_box->layout_elem->h_mode = VSIZE_GROW;
    state->editor_box->layout_elem->clip_children = 1;
    ui_widget_add_child(state->root, state->editor_box);

    /* Status Bar */
    ui_widget_t *statusbar = ui_box(state->ui_ctx, VDIR_ROW);
    statusbar->layout_elem->w_mode = VSIZE_GROW;
    statusbar->layout_elem->h_mode = VSIZE_FIXED;
    statusbar->layout_elem->h_px = STATUS_HEIGHT;
    statusbar->layout_elem->pad_left = 12;
    statusbar->layout_elem->align_items = VALIGN_CENTER;
    statusbar->layout_elem->bg_color = elev;
    statusbar->layout_elem->border_color = bdr;
    statusbar->layout_elem->border_width = 1;
    ui_widget_add_child(state->root, statusbar);

    state->status_label = ui_label(state->ui_ctx, "Ln 1, Col 1  |  0 chars  |  UTF-8", fg_muted);
    ui_widget_add_child(statusbar, state->status_label);

    note_init(state, argc >= 2 ? argv[1] : NULL);

    int running = 1;
    while (running) {
        vanilla_event_t ev;
        while (vanilla_poll_event(client, &ev) > 0) {
            if (ev.type == VANILLA_EVENT_CLOSE_REQ) {
                running = 0;
                break;
            } else if (ev.type == VANILLA_EVENT_CONFIGURE) {
                vanilla_ack_configure(client, win->window_id, ev.configure.serial);
                ui_widget_invalidate(state->root);
                state->dirty = 1;
            } else if (ev.type == VANILLA_EVENT_DND_ENTER) {
                if (strcmp(ev.dnd_enter.mime, "text/uri-list") == 0)
                    dnd_set_accept(win, 1);
                else
                    dnd_set_accept(win, 0);
            } else if (ev.type == VANILLA_EVENT_DND_DROP) {
                if (strcmp(ev.dnd_drop.mime, "text/uri-list") == 0) {
                    const char *path = ev.dnd_drop.data;
                    if (strncmp(path, "file://", 7) == 0)
                        path += 7;
                    note_init(state, path);
                    state->dirty = 1;
                    dnd_set_accept(win, 1);
                }
            } else if (ev.type == VANILLA_EVENT_INPUT) {
                struct input_event *iev = &ev.input;
                ui_handle_event(state->ui_ctx, state->root, iev);

                if (iev->type == EV_KEY) {
                    if (iev->code == BTN_LEFT) {
                        if (iev->value == 1) {
                            int cx = (int)iev->pad1;
                            int cy = (int)iev->pad2;
                            state->mouse_down = 1;
                            int ed_y = state->editor_box->layout_elem->computed_y;
                            int ed_h = state->editor_box->layout_elem->computed_h;
                            if (cy >= ed_y && cy < ed_y + ed_h) {
                                int clicked_line = state->scroll_line + (cy - ed_y) / LINE_HEIGHT;
                                if (clicked_line < 0) clicked_line = 0;
                                if (clicked_line >= state->num_lines) clicked_line = state->num_lines - 1;
                                int col_px = cx - (GUTTER_WIDTH + 8);
                                int clicked_col = (col_px + 4) / 8;
                                if (clicked_col < 0) clicked_col = 0;
                                int line_len = (int)strlen(state->lines[clicked_line]);
                                if (clicked_col > line_len) clicked_col = line_len;

                                if (state->shift_down) {
                                    if (!state->has_selection) {
                                        state->sel_anchor_line = state->cursor_line;
                                        state->sel_anchor_col = state->cursor_col;
                                        state->has_selection = 1;
                                    }
                                } else {
                                    state->sel_anchor_line = clicked_line;
                                    state->sel_anchor_col = clicked_col;
                                    state->has_selection = 0;
                                }
                                state->cursor_line = clicked_line;
                                state->cursor_col = clicked_col;
                                state->dirty = 1;
                                note_update_status(state);
                            }
                        } else {
                            state->mouse_down = 0;
                        }
                    } else if (iev->code == KEY_LEFTSHIFT || iev->code == KEY_RIGHTSHIFT) {
                        state->shift_down = (iev->value != 0);
                    } else if (iev->code == KEY_LEFTCTRL || iev->code == KEY_RIGHTCTRL) {
                        state->ctrl_down = (iev->value != 0);
                    } else if (iev->value == 1) {
                        int is_ctrl = state->ctrl_down || ((ev.mod_state & MOD_CTRL) != 0);
                        int is_shift = state->shift_down || ((ev.mod_state & MOD_SHIFT) != 0);

                        if (is_shift && !state->has_selection &&
                            (iev->code == KEY_LEFT || iev->code == KEY_RIGHT ||
                             iev->code == KEY_UP || iev->code == KEY_DOWN ||
                             iev->code == KEY_HOME || iev->code == KEY_END)) {
                            state->sel_anchor_line = state->cursor_line;
                            state->sel_anchor_col = state->cursor_col;
                            state->has_selection = 1;
                        } else if (!is_shift && !is_ctrl && state->has_selection &&
                                   (iev->code == KEY_LEFT || iev->code == KEY_RIGHT ||
                                    iev->code == KEY_UP || iev->code == KEY_DOWN)) {
                            state->has_selection = 0;
                        }

                        if (is_ctrl) {
                            if (iev->code == KEY_C) {
                                note_copy_selection(state);
                            } else if (iev->code == KEY_V) {
                                char *paste_buf = (char *)malloc(VCLIP_TEXT_MAX + 1);
                                if (paste_buf) {
                                    int n = clipboard_get(paste_buf, VCLIP_TEXT_MAX + 1);
                                    if (n > 0) {
                                        for (int i = 0; i < n; i++) {
                                            char ch = paste_buf[i];
                                            if (ch == '\r') {
                                                if (i + 1 < n && paste_buf[i + 1] == '\n')
                                                    continue;
                                                ch = '\n';
                                            }
                                            note_insert_char(state, ch);
                                        }
                                        state->dirty = 1;
                                    }
                                    free(paste_buf);
                                }
                            } else if (iev->code == KEY_S) {
                                note_save_file(state);
                            } else if (iev->code == KEY_A) {
                                state->sel_anchor_line = 0;
                                state->sel_anchor_col = 0;
                                state->cursor_line = state->num_lines - 1;
                                state->cursor_col = (int)strlen(state->lines[state->cursor_line]);
                                state->has_selection = 1;
                                state->dirty = 1;
                            }
                        } else if (iev->code == KEY_F2) {
                            note_save_file(state);
                        } else if (iev->code == KEY_UP) {
                            if (state->cursor_line > 0) {
                                state->cursor_line--;
                                int len = (int)strlen(state->lines[state->cursor_line]);
                                if (state->cursor_col > len) state->cursor_col = len;
                                state->dirty = 1;
                                note_update_status(state);
                            }
                        } else if (iev->code == KEY_DOWN) {
                            if (state->cursor_line < state->num_lines - 1) {
                                state->cursor_line++;
                                int len = (int)strlen(state->lines[state->cursor_line]);
                                if (state->cursor_col > len) state->cursor_col = len;
                                state->dirty = 1;
                                note_update_status(state);
                            }
                        } else if (iev->code == KEY_LEFT) {
                            if (state->cursor_col > 0) {
                                state->cursor_col--;
                                state->dirty = 1;
                                note_update_status(state);
                            }
                        } else if (iev->code == KEY_RIGHT) {
                            int len = (int)strlen(state->lines[state->cursor_line]);
                            if (state->cursor_col < len) {
                                state->cursor_col++;
                                state->dirty = 1;
                                note_update_status(state);
                            }
                        } else if (iev->code == KEY_HOME) {
                            state->cursor_col = 0;
                            state->dirty = 1;
                            note_update_status(state);
                        } else if (iev->code == KEY_END) {
                            state->cursor_col = (int)strlen(state->lines[state->cursor_line]);
                            state->dirty = 1;
                            note_update_status(state);
                        } else if (iev->code == KEY_BACKSPACE) {
                            note_insert_char(state, '\b');
                        } else if (iev->code == KEY_DELETE) {
                            if (state->has_selection) {
                                note_delete_selection(state);
                            } else {
                                char *l = state->lines[state->cursor_line];
                                int len = (int)strlen(l);
                                if (state->cursor_col < len) {
                                    memmove(l + state->cursor_col, l + state->cursor_col + 1, len - state->cursor_col);
                                    state->dirty = 1;
                                    note_update_status(state);
                                }
                            }
                        } else {
                            char c = vanilla_evdev_to_ascii(iev->code, state->shift_down);
                            if (c != 0)
                                note_insert_char(state, c);
                        }
                    }
                }
            }
        }

        ui_render(state->ui_ctx, state->root, &win->surface, NULL);
        note_render_editor(&win->surface, state);
        vanilla_present(win, NULL);

        usleep(16000);
    }

    vanilla_destroy_window(win);
    vanilla_disconnect(client);
    free(state);
    return 0;
}
