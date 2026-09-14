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
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>

#define NOTE_WIDTH      560
#define NOTE_HEIGHT     380
#define TOOLBAR_HEIGHT  28
#define STATUS_HEIGHT   22
#define GUTTER_WIDTH    40
#define LINE_HEIGHT     14

#define MAX_LINES       512
#define MAX_LINE_LEN    128

typedef struct {
    char lines[MAX_LINES][MAX_LINE_LEN];
    int  num_lines;
    int  cursor_line;
    int  cursor_col;
    int  scroll_line;
    char filename[128];
    int  shift_down;
    int  dirty;
} notepad_state_t;

static void note_init(notepad_state_t *st, const char *path)
{
    memset(st, 0, sizeof(*st));
    st->num_lines = 1;
    st->lines[0][0] = '\0';
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

static void note_insert_char(notepad_state_t *st, char c)
{
    char *l = st->lines[st->cursor_line];
    int len = (int)strlen(l);

    if (c == '\n') {
        if (st->num_lines >= MAX_LINES)
            return;

        /* Move subsequent lines down */
        for (int i = st->num_lines; i > st->cursor_line + 1; i--)
            strcpy(st->lines[i], st->lines[i - 1]);

        /* Copy remainder of current line to new line */
        strcpy(st->lines[st->cursor_line + 1], l + st->cursor_col);
        l[st->cursor_col] = '\0';

        st->num_lines++;
        st->cursor_line++;
        st->cursor_col = 0;
        st->dirty = 1;
        return;
    }

    if (c == '\b') {
        if (st->cursor_col > 0) {
            memmove(l + st->cursor_col - 1, l + st->cursor_col, len - st->cursor_col + 1);
            st->cursor_col--;
            st->dirty = 1;
        } else if (st->cursor_line > 0) {
            /* Merge with previous line */
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
        return;
    }

    if (len < MAX_LINE_LEN - 1 && (unsigned char)c >= 32) {
        memmove(l + st->cursor_col + 1, l + st->cursor_col, len - st->cursor_col + 1);
        l[st->cursor_col] = c;
        st->cursor_col++;
        st->dirty = 1;
    }
}

static void note_render(vanilla_surface_t *surf, notepad_state_t *st)
{
    /* Background */
    app_fill_rect(surf, 0, 0, NOTE_WIDTH, NOTE_HEIGHT, APP_COLOR_BG);

    /* Toolbar header */
    app_fill_rect(surf, 0, 0, NOTE_WIDTH, TOOLBAR_HEIGHT, APP_COLOR_SURFACE);
    app_fill_rect(surf, 0, TOOLBAR_HEIGHT - 1, NOTE_WIDTH, 1, APP_COLOR_BORDER);
    app_draw_button(surf, 8, 4, 52, 20, "New", 0);
    app_draw_button(surf, 66, 4, 52, 20, "Save", 0);
    app_draw_text(surf, 130, 10, st->filename, APP_COLOR_MUTED);

    /* Gutter */
    int content_y = TOOLBAR_HEIGHT;
    int content_h = NOTE_HEIGHT - TOOLBAR_HEIGHT - STATUS_HEIGHT;
    app_fill_rect(surf, 0, content_y, GUTTER_WIDTH, content_h, 0xFF242933u);
    app_fill_rect(surf, GUTTER_WIDTH - 1, content_y, 1, content_h, APP_COLOR_BORDER);

    int visible_lines = content_h / LINE_HEIGHT;
    if (st->cursor_line < st->scroll_line)
        st->scroll_line = st->cursor_line;
    if (st->cursor_line >= st->scroll_line + visible_lines)
        st->scroll_line = st->cursor_line - visible_lines + 1;

    for (int i = 0; i < visible_lines; i++) {
        int l_idx = st->scroll_line + i;
        if (l_idx >= st->num_lines)
            break;

        int line_y = content_y + i * LINE_HEIGHT + 3;

        /* Line number */
        char lno[8];
        snprintf(lno, sizeof(lno), "%3d", l_idx + 1);
        app_draw_text(surf, 6, line_y, lno, APP_COLOR_DIM);

        /* Text */
        app_draw_text(surf, GUTTER_WIDTH + 8, line_y, st->lines[l_idx], APP_COLOR_TEXT);

        /* Cursor */
        if (l_idx == st->cursor_line) {
            int cx = GUTTER_WIDTH + 8 + st->cursor_col * 8;
            app_fill_rect(surf, cx, line_y - 1, 2, 10, APP_COLOR_PRIMARY);
        }
    }

    /* Status bar */
    int status_y = NOTE_HEIGHT - STATUS_HEIGHT;
    app_fill_rect(surf, 0, status_y, NOTE_WIDTH, STATUS_HEIGHT, APP_COLOR_SURFACE);
    app_fill_rect(surf, 0, status_y, NOTE_WIDTH, 1, APP_COLOR_BORDER);

    int total_chars = 0;
    for (int i = 0; i < st->num_lines; i++)
        total_chars += (int)strlen(st->lines[i]);

    char status[64];
    snprintf(status, sizeof(status), "Ln %d, Col %d  |  %d chars  |  UTF-8",
             st->cursor_line + 1, st->cursor_col + 1, total_chars);
    app_draw_text(surf, 12, status_y + 7, status, APP_COLOR_MUTED);
}

int main(int argc, char **argv)
{
    notepad_state_t *state = (notepad_state_t *)malloc(sizeof(notepad_state_t));
    if (!state) {
        fprintf(stderr, "notepad: memory allocation failed\n");
        return 1;
    }
    note_init(state, argc >= 2 ? argv[1] : NULL);

    vanilla_client_t *client = vanilla_connect(NULL);
    if (!client) {
        fprintf(stderr, "notepad: failed to connect to display server\n");
        free(state);
        return 1;
    }

    char title[128];
    snprintf(title, sizeof(title), "Notepad - %s", state->filename);

    vanilla_window_t *win = vanilla_create_window(client, title, 120, 100,
                                                  NOTE_WIDTH, NOTE_HEIGHT,
                                                  WINDOW_FLAG_RESIZABLE);
    if (!win) {
        vanilla_disconnect(client);
        free(state);
        return 1;
    }

    vanilla_map_window(win);

    int running = 1;
    while (running) {
        vanilla_event_t ev;
        while (vanilla_poll_event(client, &ev) > 0) {
            if (ev.type == VANILLA_EVENT_CLOSE_REQ) {
                running = 0;
                break;
            } else if (ev.type == VANILLA_EVENT_INPUT) {
                struct input_event *iev = &ev.input;
                if (iev->type == EV_KEY) {
                    if (iev->code == BTN_LEFT && iev->value == 1) {
                        int cx = (int)iev->pad1;
                        int cy = (int)iev->pad2;
                        if (cy >= 4 && cy < 24) {
                            if (cx >= 8 && cx < 60) {
                                note_init(state, "/tmp/untitled.txt");
                                state->dirty = 1;
                            } else if (cx >= 66 && cx < 118) {
                                note_save_file(state);
                            }
                        } else if (cy >= TOOLBAR_HEIGHT && cy < NOTE_HEIGHT - STATUS_HEIGHT) {
                            int clicked_line = state->scroll_line + (cy - TOOLBAR_HEIGHT) / LINE_HEIGHT;
                            if (clicked_line < 0)
                                clicked_line = 0;
                            if (clicked_line >= state->num_lines)
                                clicked_line = state->num_lines - 1;
                            state->cursor_line = clicked_line;

                            int col_px = cx - (GUTTER_WIDTH + 8);
                            int clicked_col = (col_px + 4) / 8;
                            if (clicked_col < 0)
                                clicked_col = 0;
                            int line_len = (int)strlen(state->lines[state->cursor_line]);
                            if (clicked_col > line_len)
                                clicked_col = line_len;
                            state->cursor_col = clicked_col;
                            state->dirty = 1;
                        }
                    } else if (iev->code == KEY_LEFTSHIFT || iev->code == KEY_RIGHTSHIFT) {
                        state->shift_down = (iev->value != 0);
                    } else if (iev->code == KEY_LEFTCTRL) {
                        /* Track Ctrl key */
                    } else if (iev->value == 1) {
                        if (iev->code == KEY_F2) {
                            note_save_file(state);
                        } else if (iev->code == KEY_UP) {
                            if (state->cursor_line > 0) {
                                state->cursor_line--;
                                int len = (int)strlen(state->lines[state->cursor_line]);
                                if (state->cursor_col > len) state->cursor_col = len;
                                state->dirty = 1;
                            }
                        } else if (iev->code == KEY_DOWN) {
                            if (state->cursor_line < state->num_lines - 1) {
                                state->cursor_line++;
                                int len = (int)strlen(state->lines[state->cursor_line]);
                                if (state->cursor_col > len) state->cursor_col = len;
                                state->dirty = 1;
                            }
                        } else if (iev->code == KEY_LEFT) {
                            if (state->cursor_col > 0) {
                                state->cursor_col--;
                                state->dirty = 1;
                            }
                        } else if (iev->code == KEY_RIGHT) {
                            int len = (int)strlen(state->lines[state->cursor_line]);
                            if (state->cursor_col < len) {
                                state->cursor_col++;
                                state->dirty = 1;
                            }
                        } else if (iev->code == KEY_HOME) {
                            state->cursor_col = 0;
                            state->dirty = 1;
                        } else if (iev->code == KEY_END) {
                            state->cursor_col = (int)strlen(state->lines[state->cursor_line]);
                            state->dirty = 1;
                        } else {
                            char c = app_evdev_to_ascii(iev->code, state->shift_down);
                            if (c != 0)
                                note_insert_char(state, c);
                        }
                    }
                }
            }
        }

        if (state->dirty) {
            note_render(&win->surface, state);
            vanilla_present(win, NULL);
            state->dirty = 0;
        }

        usleep(16000);
    }

    vanilla_destroy_window(win);
    vanilla_disconnect(client);
    free(state);
    return 0;
}
