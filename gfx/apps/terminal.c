/*
 * Project Tsukasa — Minimal terminal/shell app
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

#include "apps.h"
#include "../wm.h"
#include "../font.h"
#include "../blit.h"
#include "../../input/event.h"
#include "../../mm/heap.h"
#include <stddef.h>
#include <stdint.h>

#define TERM_W         400
#define TERM_H         280
#define TERM_BUF_SIZE  2048
#define TERM_CMD_SIZE  80
#define TERM_LINE_H    10

struct term_data {
    char   output[TERM_BUF_SIZE];
    int    out_len;
    char   cmd[TERM_CMD_SIZE];
    int    cmd_len;
};

static int kstrcmp(const char *a, const char *b)
{
    while (*a && *b && *a == *b) { a++; b++; }
    return (unsigned char)*a - (unsigned char)*b;
}

static int kstrncmp(const char *a, const char *b, int n)
{
    for (int i = 0; i < n; i++) {
        if (a[i] != b[i]) return (unsigned char)a[i] - (unsigned char)b[i];
        if (a[i] == '\0') return 0;
    }
    return 0;
}

static void term_append(struct term_data *td, const char *str)
{
    while (str && *str && td->out_len < TERM_BUF_SIZE - 1) {
        td->output[td->out_len++] = *str++;
    }
    td->output[td->out_len] = '\0';
}

static void term_execute(struct term_data *td)
{
    char *cmd = td->cmd;
    while (*cmd == ' ') cmd++;

    if (cmd[0] == '\0') {
    } else if (kstrcmp(cmd, "help") == 0) {
        term_append(td, "Available commands:\n");
        term_append(td, "  help  - Show this help\n");
        term_append(td, "  ver   - Show version\n");
        term_append(td, "  cls   - Clear screen\n");
        term_append(td, "  echo  - Echo text\n");
    } else if (kstrcmp(cmd, "ver") == 0) {
        term_append(td, "Tsukasa Kernel v0.1\n");
    } else if (kstrcmp(cmd, "cls") == 0) {
        td->out_len = 0;
        td->output[0] = '\0';
    } else if (kstrncmp(cmd, "echo ", 5) == 0) {
        term_append(td, cmd + 5);
        term_append(td, "\n");
    } else {
        term_append(td, "Unknown command: ");
        term_append(td, cmd);
        term_append(td, "\n");
    }

    td->cmd_len = 0;
    td->cmd[0] = '\0';

    term_append(td, "C:\\> ");
}

static void term_draw(wm_window_t *win)
{
    struct term_data *td = (struct term_data *)win->app_data;
    if (!td) return;

    int cx, cy, cw, ch;
    wm_client_rect(win, &cx, &cy, &cw, &ch);

    color_t bg = rgb(0, 0, 0);
    color_t fg = rgb(0, 255, 0);

    fb_fill_rect(cx, cy, cw, ch, bg);

    int x = cx + 4;
    int y = cy + 4;
    int max_y = cy + ch - 4;

    int total_lines = 1;
    for (int i = 0; i < td->out_len; i++)
        if (td->output[i] == '\n') total_lines++;

    int visible_lines = (max_y - y) / TERM_LINE_H;
    int skip_lines = total_lines - visible_lines;
    if (skip_lines < 0) skip_lines = 0;

    int cur_line = 0;
    for (int i = 0; i < td->out_len; i++) {
        if (cur_line < skip_lines) {
            if (td->output[i] == '\n') cur_line++;
            continue;
        }

        char c = td->output[i];
        if (c == '\n') {
            y += TERM_LINE_H;
            x = cx + 4;
            cur_line++;
            if (y + 8 > max_y) break;
            continue;
        }

        if (x + 8 > cx + cw - 4) {
            y += TERM_LINE_H;
            x = cx + 4;
            if (y + 8 > max_y) break;
        }

        fb_draw_char(x, y, c, fg, bg);
        x += 8;
    }

    for (int i = 0; i < td->cmd_len; i++) {
        if (x + 8 > cx + cw - 4) {
            y += TERM_LINE_H;
            x = cx + 4;
            if (y + 8 > max_y) break;
        }
        fb_draw_char(x, y, td->cmd[i], fg, bg);
        x += 8;
    }

    if (y + 8 <= max_y)
        fb_draw_char(x, y, '_', fg, bg);
}

static void term_event(wm_window_t *win, const void *event)
{
    struct term_data *td = (struct term_data *)win->app_data;
    if (!td) return;

    const struct gui_event *ev = (const struct gui_event *)event;
    if (ev->type != EVENT_KEY || ev->subtype != KEY_PRESS)
        return;

    char key = (char)(ev->keycode & 0xFF);

    if (key == '\b') {
        if (td->cmd_len > 0)
            td->cmd[--td->cmd_len] = '\0';
        return;
    }

    if (key == '\n' || key == '\r') {
        term_append(td, td->cmd);
        term_append(td, "\n");
        term_execute(td);
        return;
    }

    if (key >= ' ' && key <= '~') {
        if (td->cmd_len < TERM_CMD_SIZE - 1) {
            td->cmd[td->cmd_len++] = key;
            td->cmd[td->cmd_len] = '\0';
        }
    }
}

void app_terminal_open(void)
{
    struct term_data *td = (struct term_data *)kmalloc(sizeof(struct term_data));
    if (!td) return;
    td->out_len = 0;
    td->output[0] = '\0';
    td->cmd_len = 0;
    td->cmd[0] = '\0';

    term_append(td, "Tsukasa Terminal - v0.1.3\n");
    term_append(td, "Type 'help' for commands.\n\n");
    term_append(td, "C:\\> ");

    wm_create_window(80, 60, TERM_W, TERM_H, "Terminal",
                     term_draw, term_event, td);
}
