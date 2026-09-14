/*
 * Project Tsukasa — Project Vanilla Terminal Emulator
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
#include <errno.h>

#define TERM_WIDTH   640
#define TERM_HEIGHT  400
#define TERM_ROWS    25
#define TERM_COLS    80
#define CELL_WIDTH   8
#define CELL_HEIGHT  16

#define COLOR_TERM_BG     0xFF1A1C23u
#define COLOR_TERM_FG     0xFFD8DEE9u
#define COLOR_TERM_CURSOR 0xFF88C0D0u

typedef struct {
    char     grid[TERM_ROWS][TERM_COLS];
    uint32_t colors[TERM_ROWS][TERM_COLS];
    int      cursor_row;
    int      cursor_col;
    int      in_pipe[2];
    int      out_pipe[2];
    int      shell_pid;
    int      shift_down;
    int      dirty;
} terminal_state_t;

static void term_scroll_up(terminal_state_t *st)
{
    for (int r = 0; r < TERM_ROWS - 1; r++) {
        memcpy(st->grid[r], st->grid[r + 1], TERM_COLS);
        memcpy(st->colors[r], st->colors[r + 1], TERM_COLS * sizeof(uint32_t));
    }
    memset(st->grid[TERM_ROWS - 1], ' ', TERM_COLS);
    for (int c = 0; c < TERM_COLS; c++)
        st->colors[TERM_ROWS - 1][c] = COLOR_TERM_FG;
}

static void term_put_char(terminal_state_t *st, char ch)
{
    if (ch == '\r') {
        st->cursor_col = 0;
        st->dirty = 1;
        return;
    }
    if (ch == '\n') {
        st->cursor_col = 0;
        st->cursor_row++;
        if (st->cursor_row >= TERM_ROWS) {
            term_scroll_up(st);
            st->cursor_row = TERM_ROWS - 1;
        }
        st->dirty = 1;
        return;
    }
    if (ch == '\b') {
        if (st->cursor_col > 0) {
            st->cursor_col--;
            st->grid[st->cursor_row][st->cursor_col] = ' ';
            st->dirty = 1;
        }
        return;
    }
    if (ch == '\t') {
        int next_tab = (st->cursor_col + 4) & ~3;
        while (st->cursor_col < next_tab && st->cursor_col < TERM_COLS) {
            st->grid[st->cursor_row][st->cursor_col] = ' ';
            st->cursor_col++;
        }
        st->dirty = 1;
        return;
    }

    if ((unsigned char)ch < 32)
        return;

    if (st->cursor_col >= TERM_COLS) {
        st->cursor_col = 0;
        st->cursor_row++;
        if (st->cursor_row >= TERM_ROWS) {
            term_scroll_up(st);
            st->cursor_row = TERM_ROWS - 1;
        }
    }

    st->grid[st->cursor_row][st->cursor_col] = ch;
    st->colors[st->cursor_row][st->cursor_col] = COLOR_TERM_FG;
    st->cursor_col++;
    st->dirty = 1;
}

static void term_render(vanilla_surface_t *surf, terminal_state_t *st)
{
    app_fill_rect(surf, 0, 0, TERM_WIDTH, TERM_HEIGHT, COLOR_TERM_BG);

    for (int r = 0; r < TERM_ROWS; r++) {
        int y = r * CELL_HEIGHT;
        for (int c = 0; c < TERM_COLS; c++) {
            char ch = st->grid[r][c];
            if (ch != ' ' && ch != '\0') {
                app_draw_char(surf, c * CELL_WIDTH, y + 4, ch, st->colors[r][c]);
            }
        }
    }

    /* Draw cursor */
    int cur_x = st->cursor_col * CELL_WIDTH;
    int cur_y = st->cursor_row * CELL_HEIGHT;
    app_fill_rect(surf, cur_x, cur_y + 13, CELL_WIDTH, 2, COLOR_TERM_CURSOR);
}

int main(int argc, char **argv)
{
    (void)argc;
    (void)argv;

    terminal_state_t state;
    memset(&state, 0, sizeof(state));

    for (int r = 0; r < TERM_ROWS; r++) {
        memset(state.grid[r], ' ', TERM_COLS);
        for (int c = 0; c < TERM_COLS; c++)
            state.colors[r][c] = COLOR_TERM_FG;
    }

    vanilla_client_t *client = vanilla_connect(NULL);
    if (!client) {
        fprintf(stderr, "terminal: failed to connect to display server\n");
        return 1;
    }

    vanilla_window_t *win = vanilla_create_window(client, "Terminal", 80, 80,
                                                  TERM_WIDTH, TERM_HEIGHT,
                                                  WINDOW_FLAG_RESIZABLE);
    if (!win) {
        fprintf(stderr, "terminal: failed to create window\n");
        vanilla_disconnect(client);
        return 1;
    }

    vanilla_map_window(win);

    /* Create stdin and stdout pipes for shell subprocess */
    if (pipe(state.in_pipe) < 0 || pipe(state.out_pipe) < 0) {
        fprintf(stderr, "terminal: pipe allocation failed\n");
        vanilla_destroy_window(win);
        vanilla_disconnect(client);
        return 1;
    }

    /* Make shell output non-blocking */
    int flags = fcntl(state.out_pipe[0], F_GETFL);
    fcntl(state.out_pipe[0], F_SETFL, flags | O_NONBLOCK);

    /* Spawn interactive shell using fallback candidate paths */
    static const char *shell_candidates[] = {
        "/bin/tsh.elf",
        "/bin/tsh",
        "tsh.elf",
        "tsh",
        "/bin/TSH.ELF",
        "/fat12/tsh.elf",
        "/fat12/TSH.ELF",
        NULL
    };

    struct tsukasa_spawn_request req;
    memset(&req, 0, sizeof(req));
    req.stdin_fd = state.in_pipe[0];
    req.stdout_fd = state.out_pipe[1];
    req.stderr_fd = state.out_pipe[1];
    req.tty_id = -1;

    state.shell_pid = -1;
    for (int i = 0; shell_candidates[i] != NULL; i++) {
        req.path = shell_candidates[i];
        req.args = shell_candidates[i];
        state.shell_pid = spawn_ex(&req);
        if (state.shell_pid >= 0)
            break;
    }

    /* Close unused ends in parent */
    close(state.in_pipe[0]);
    close(state.out_pipe[1]);

    state.dirty = 1;
    char read_buf[256];
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
                    if (iev->code == KEY_LEFTSHIFT || iev->code == KEY_RIGHTSHIFT) {
                        state.shift_down = (iev->value != 0);
                    } else if (iev->value == 1) {
                        char ascii = app_evdev_to_ascii(iev->code, state.shift_down);
                        if (ascii != 0) {
                            write(state.in_pipe[1], &ascii, 1);
                        } else if (iev->code == KEY_UP) {
                            write(state.in_pipe[1], "\033[A", 3);
                        } else if (iev->code == KEY_DOWN) {
                            write(state.in_pipe[1], "\033[B", 3);
                        } else if (iev->code == KEY_RIGHT) {
                            write(state.in_pipe[1], "\033[C", 3);
                        } else if (iev->code == KEY_LEFT) {
                            write(state.in_pipe[1], "\033[D", 3);
                        }
                    }
                }
            }
        }

        /* Read output from shell */
        ssize_t n = read(state.out_pipe[0], read_buf, sizeof(read_buf));
        if (n > 0) {
            for (ssize_t i = 0; i < n; i++) {
                char c = read_buf[i];
                if (c == 0x1B) {
                    /* Consume simple ANSI CSI sequence */
                    if (i + 1 < n && read_buf[i + 1] == '[') {
                        i += 2;
                        while (i < n && read_buf[i] >= 0x20 && read_buf[i] <= 0x3F)
                            i++;
                        continue;
                    }
                }
                term_put_char(&state, c);
            }
        }

        if (state.dirty) {
            term_render(&win->surface, &state);
            vanilla_present(win, NULL);
            state.dirty = 0;
        }

        usleep(10000); /* 10ms frame pacing */
    }

    if (state.shell_pid > 0)
        kill(state.shell_pid, 9);

    close(state.in_pipe[1]);
    close(state.out_pipe[0]);
    vanilla_destroy_window(win);
    vanilla_disconnect(client);

    return 0;
}
