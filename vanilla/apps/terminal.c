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
#include "terminal_vt.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <time.h>
#include <sys/wait.h>

#define TERM_WIDTH   640
#define TERM_HEIGHT  400

#define COLOR_TERM_BG     (g_theme ? g_theme->bg_base : 0xFF2E3440u)
#define COLOR_TERM_FG     (g_theme ? g_theme->fg_primary : 0xFFD8DEE9u)
#define COLOR_TERM_CURSOR (g_theme ? g_theme->accent : 0xFF88C0D0u)

static void term_render(vanilla_surface_t *surf, terminal_state_t *st)
{
    int32_t surf_w = (int32_t)surf->width;
    int32_t surf_h = (int32_t)surf->height;

    app_fill_rect(surf, 0, 0, surf_w, surf_h, COLOR_TERM_BG);

    char empty_row[TERM_MAX_COLS];
    memset(empty_row, ' ', sizeof(empty_row));

    for (int r = 0; r < st->rows; r++) {
        int y = r * CELL_HEIGHT;
        if (y + CELL_HEIGHT > surf_h)
            break;

        const char *line_chars = NULL;
        const uint32_t *line_fg = NULL;
        const uint32_t *line_bg = NULL;

        if (r - st->scroll_offset >= 0) {
            int src_r = r - st->scroll_offset;
            if (src_r < st->rows) {
                line_chars = st->grid[src_r];
                line_fg = st->colors[src_r];
                line_bg = st->bg_colors[src_r];
            }
        } else {
            int d = st->scroll_offset - r;
            if (d <= st->scrollback_count) {
                int idx = (st->scrollback_head - d + TERM_SCROLLBACK * 2) % TERM_SCROLLBACK;
                line_chars = st->scrollback[idx];
                line_fg = st->scrollback_fg[idx];
                line_bg = st->scrollback_bg[idx];
            }
        }

        if (!line_chars)
            line_chars = empty_row;

        for (int c = 0; c < st->cols; c++) {
            int x = c * CELL_WIDTH;
            if (x + CELL_WIDTH > surf_w)
                break;

            uint32_t bg = line_bg ? line_bg[c] : 0;
            if (bg != 0 && bg != COLOR_TERM_BG)
                app_fill_rect(surf, x, y, CELL_WIDTH, CELL_HEIGHT, bg);

            char ch = line_chars[c];
            if (ch != ' ' && ch != '\0') {
                uint32_t fg = line_fg ? line_fg[c] : COLOR_TERM_FG;
                app_draw_char(surf, x, y + 4, ch, fg);
            }
        }
    }

    /* Draw cursor if live view and cursor visible */
    if (st->cursor_visible && st->scroll_offset == 0) {
        int cur_x = st->cursor_col * CELL_WIDTH;
        int cur_y = st->cursor_row * CELL_HEIGHT;
        if (cur_x + CELL_WIDTH <= surf_w && cur_y + CELL_HEIGHT <= surf_h)
            app_fill_rect(surf, cur_x, cur_y + 13, CELL_WIDTH, 2, COLOR_TERM_CURSOR);
    }

    /* Scrollback indicator bar */
    if (st->scroll_offset > 0 && st->scrollback_count > 0 && surf_h > 30) {
        int bar_h = (surf_h * st->rows) / (st->scrollback_count + st->rows);
        if (bar_h < 16) bar_h = 16;
        int bar_y = ((surf_h - bar_h) * (st->scrollback_count - st->scroll_offset)) / st->scrollback_count;
        app_fill_rect(surf, surf_w - 4, bar_y, 4, bar_h, COLOR_TERM_CURSOR);
    }
}

int main(int argc, char **argv)
{
    (void)argc;
    (void)argv;

    terminal_state_t state;
    vt_init(&state, TERM_DEFAULT_ROWS, TERM_DEFAULT_COLS);

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

    vanilla_set_size_hints(client, win->window_id, 160, 120, 0, 0, 0, 0);
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
    char read_buf[512];
    int running = 1;

    while (running) {
        vanilla_event_t ev;
        while (vanilla_poll_event(client, &ev) > 0) {
            if (ev.type == VANILLA_EVENT_CLOSE_REQ) {
                running = 0;
                break;
            } else if (ev.type == VANILLA_EVENT_CONFIGURE) {
                int new_cols = (int)ev.configure.width / CELL_WIDTH;
                int new_rows = (int)ev.configure.height / CELL_HEIGHT;
                vt_resize(&state, new_rows, new_cols);
                vanilla_ack_configure(client, win->window_id, ev.configure.serial);
            } else if (ev.type == VANILLA_EVENT_INPUT) {
                struct input_event *iev = &ev.input;
                if (iev->type == EV_KEY) {
                    if (iev->code == KEY_LEFTSHIFT || iev->code == KEY_RIGHTSHIFT) {
                        state.shift_down = (iev->value != 0);
                    } else if (iev->code == KEY_LEFTCTRL || iev->code == KEY_RIGHTCTRL) {
                        state.ctrl_down = (iev->value != 0);
                    } else if (iev->value == 1) {
                        int is_ctrl = state.ctrl_down || ((ev.mod_state & MOD_CTRL) != 0);
                        int is_shift = state.shift_down || ((ev.mod_state & MOD_SHIFT) != 0);

                        if (is_shift && iev->code == KEY_PAGEUP) {
                            vt_scroll_history(&state, state.rows / 2);
                            continue;
                        } else if (is_shift && iev->code == KEY_PAGEDOWN) {
                            vt_scroll_history(&state, -(state.rows / 2));
                            continue;
                        }

                        /* Any typing snaps back to live prompt */
                        if (state.scroll_offset > 0 && !is_ctrl)
                            state.scroll_offset = 0;

                        if (is_ctrl) {
                            if (iev->code == KEY_C) {
                                vt_copy_last_output(&state);
                                if (!is_shift) {
                                    char c = 3;
                                    write(state.in_pipe[1], &c, 1);
                                }
                            } else if (iev->code == KEY_V) {
                                char *paste_buf = (char *)malloc(VCLIP_TEXT_MAX + 1);
                                if (paste_buf) {
                                    int n = clipboard_get(paste_buf, VCLIP_TEXT_MAX + 1);
                                    if (n > 0) {
                                        size_t off = 0;
                                        while (off < (size_t)n) {
                                            size_t chunk = (size_t)n - off;
                                            if (chunk > 512)
                                                chunk = 512;
                                            write(state.in_pipe[1], paste_buf + off, chunk);
                                            off += chunk;
                                        }
                                    }
                                    free(paste_buf);
                                }
                            } else if (iev->code == KEY_D) {
                                char c = 4;
                                write(state.in_pipe[1], &c, 1);
                            } else if (iev->code == KEY_L) {
                                char c = 12;
                                write(state.in_pipe[1], &c, 1);
                            }
                        } else {
                            char ascii = vanilla_evdev_to_ascii(iev->code, is_shift);
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
                            } else if (iev->code == KEY_PAGEUP) {
                                vt_scroll_history(&state, state.rows / 2);
                            } else if (iev->code == KEY_PAGEDOWN) {
                                vt_scroll_history(&state, -(state.rows / 2));
                            }
                        }
                    }
                }
            }
        }

        /* Check if shell subprocess exited */
        if (state.shell_pid > 0) {
            int status = 0;
            pid_t wp = waitpid(state.shell_pid, &status, WNOHANG);
            if (wp > 0) {
                state.shell_pid = -1;
            }
        }

        /* Read output from shell and interpret via VT state machine */
        ssize_t n = read(state.out_pipe[0], read_buf, sizeof(read_buf));
        if (n > 0) {
            vt_process_bytes(&state, read_buf, (size_t)n);
        } else if (n == 0 && state.shell_pid > 0) {
            int status = 0;
            waitpid(state.shell_pid, &status, WNOHANG);
            state.shell_pid = -1;
        }

        if (state.dirty) {
            term_render(&win->surface, &state);
            vanilla_present(win, NULL);
            state.dirty = 0;
        }

        usleep(10000); /* 10ms pacing */
    }

    if (state.shell_pid > 0)
        kill(state.shell_pid, 9);

    close(state.in_pipe[1]);
    close(state.out_pipe[0]);
    vanilla_destroy_window(win);
    vanilla_disconnect(client);

    return 0;
}
