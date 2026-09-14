/*
 * Project Tsukasa — include "../include/app_runtime.h"
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

#include <stddef.h>
#include <stdint.h>
#include "user/include/libui.h"
#include "user/include/stdio.h"
#include "user/include/string.h"
#include "user/include/unistd.h"
#include "user/include/app_runtime.h"

#define CALC_W 280
#define CALC_H 220

typedef struct calc_state {
    ui_window_t win;
    int running;
    int acc;
    int cur;
    char op;
    char display[64];
} calc_state_t;

static void calc_update(calc_state_t *st)
{
    snprintf(st->display, sizeof(st->display), "%d", st->cur);
}

static void calc_apply_pending(calc_state_t *st)
{
    if (st->op == '+')
        st->acc += st->cur;
    else if (st->op == '-')
        st->acc -= st->cur;
    else if (st->op == '*')
        st->acc *= st->cur;
    else if (st->op == '/' && st->cur != 0)
        st->acc /= st->cur;
    else if (!st->op)
        st->acc = st->cur;
    st->cur = st->acc;
    calc_update(st);
}

static void calc_input(calc_state_t *st, char c)
{
    if (c >= '0' && c <= '9') {
        st->cur = st->cur * 10 + (c - '0');
        calc_update(st);
        return;
    }
    if (c == 'c' || c == 'C') {
        st->acc = 0;
        st->cur = 0;
        st->op = 0;
        calc_update(st);
        return;
    }
    if (c == '+' || c == '-' || c == '*' || c == '/') {
        calc_apply_pending(st);
        st->op = c;
        st->cur = 0;
        return;
    }
    if (c == '=' || c == '\n' || c == '\r') {
        calc_apply_pending(st);
        st->op = 0;
    }
}

static void calc_draw(calc_state_t *st)
{
    ui_draw_rect(st->win, 0, 0, CALC_W, CALC_H, 0xFF1B2634u);
    ui_draw_rect(st->win, 12, 24, CALC_W - 24, 28, 0xFF111111u);
    ui_draw_string(st->win, 20, 34, st->display, 0xFF66FFAAu);
    ui_draw_string(st->win, 12, 70, "Use keyboard: 0-9 + - * / = C", 0xFFFFFFFFu);
    ui_draw_string(st->win, 12, 84, "This calculator runs in userspace.", 0xFFE0E0E0u);
    ui_mark_dirty(st->win, 0, 0, CALC_W, CALC_H);
}

static int calc_main(int argc, char **argv)
{
    calc_state_t st;
    ui_event_t ev;
    (void)argc;
    (void)argv;
    memset(&st, 0, sizeof(st));
    st.win = ui_window_create("Calculator", 170, 120, CALC_W, CALC_H);
    if ((int64_t)st.win <= 0)
        return 1;
    st.running = 1;
    calc_update(&st);
    calc_draw(&st);

    while (st.running) {
        if (!ui_get_event(st.win, &ev)) {
            yield();
            continue;
        }
        if (ev.type == UI_EVENT_CLOSE) {
            st.running = 0;
            break;
        }
        if (ev.type == UI_EVENT_PAINT || ev.type == UI_EVENT_RESIZE) {
            calc_draw(&st);
            continue;
        }
        if (ev.type == UI_EVENT_KEY) {
            calc_input(&st, (char)(ev.keycode & 0xFF));
            calc_draw(&st);
        }
    }

    ui_window_destroy(st.win);
    return 0;
}

void app_calc_gui_entry(void)
{
    _exit(app_run_main(calc_main));
}
