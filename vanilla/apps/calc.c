/*
 * Project Tsukasa — Project Vanilla Calculator
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

#define CALC_WIDTH   280
#define CALC_HEIGHT  360

typedef struct {
    char        display[32];
    double      accum, current;
    char        op;
    int         has_decimal, reset_on_next;
    ui_widget_t *label;
} calc_state_t;

static void calc_execute_op(calc_state_t *st)
{
    if (st->op == '+') st->accum += st->current;
    else if (st->op == '-') st->accum -= st->current;
    else if (st->op == '*') st->accum *= st->current;
    else if (st->op == '/') {
        if (st->current != 0.0) st->accum /= st->current;
        else { strcpy(st->display, "Error"); st->reset_on_next = 1; return; }
    } else st->accum = st->current;

    long ip = (long)st->accum;
    if (st->accum == (double)ip) snprintf(st->display, sizeof(st->display), "%ld", ip);
    else snprintf(st->display, sizeof(st->display), "%.4g", st->accum);
    st->current = st->accum;
    st->reset_on_next = 1;
}

static void calc_handle_input(calc_state_t *st, const char *tok)
{
    if (!strcmp(tok, "C")) {
        st->accum = st->current = st->op = st->has_decimal = st->reset_on_next = 0;
        strcpy(st->display, "0");
    } else if (!strcmp(tok, "+/-")) {
        st->current = -st->current;
        long ip = (long)st->current;
        if (st->current == (double)ip) snprintf(st->display, sizeof(st->display), "%ld", ip);
        else snprintf(st->display, sizeof(st->display), "%.4g", st->current);
    } else if (!strcmp(tok, "%")) {
        st->current /= 100.0;
        snprintf(st->display, sizeof(st->display), "%.4g", st->current);
    } else if (strchr("+-*/", tok[0])) {
        if (st->op && !st->reset_on_next) calc_execute_op(st);
        else st->accum = st->current;
        st->op = tok[0];
        st->reset_on_next = 1;
        st->has_decimal = 0;
    } else if (!strcmp(tok, "=")) {
        if (st->op) { calc_execute_op(st); st->op = 0; }
    } else if (!strcmp(tok, ".")) {
        if (st->reset_on_next) { strcpy(st->display, "0."); st->current = 0.0; st->has_decimal = 1; st->reset_on_next = 0; }
        else if (!st->has_decimal) { strcat(st->display, "."); st->has_decimal = 1; }
    } else if (tok[0] >= '0' && tok[0] <= '9') {
        if (st->reset_on_next || !strcmp(st->display, "0")) {
            strncpy(st->display, tok, sizeof(st->display) - 1);
            st->reset_on_next = st->has_decimal = 0;
        } else if (strlen(st->display) < 13) {
            strcat(st->display, tok);
        }
        st->current = strtod(st->display, NULL);
    }
}

static void on_calc_button(ui_widget_t *w, const ui_event_t *ev, void *ud)
{
    (void)w;
    if (ev->type != UI_EVENT_CLICK) return;
    calc_state_t *st = (calc_state_t *)ud;
    calc_handle_input(st, w->button.label);
    st->label->label.text = st->display;
    ui_widget_invalidate(st->label);
}

int main(int argc, char **argv)
{
    (void)argc; (void)argv;
    calc_state_t state;
    memset(&state, 0, sizeof(state));
    strcpy(state.display, "0");

    vanilla_client_t *client = vanilla_connect(NULL);
    if (!client) return 1;

    vanilla_window_t *win = vanilla_create_window(client, "Calculator", 120, 80,
                                                  CALC_WIDTH, CALC_HEIGHT, WINDOW_FLAG_NONE);
    if (!win) { vanilla_disconnect(client); return 1; }
    vanilla_map_window(win);

    static uint8_t ui_arena[128 * 1024];
    ui_ctx_t *ctx = ui_ctx_init(ui_arena, sizeof(ui_arena), NULL);
    if (!ctx) { vanilla_destroy_window(win); vanilla_disconnect(client); return 1; }

    ui_widget_t *root = ui_box(ctx, VDIR_COLUMN);
    root->layout_elem->pad_left = root->layout_elem->pad_right = root->layout_elem->pad_top = root->layout_elem->pad_bottom = 16;
    root->layout_elem->gap = 8;
    root->layout_elem->bg_color = g_theme ? g_theme->bg_base : 0xFF2E3440u;

    ui_widget_t *disp_box = ui_box(ctx, VDIR_ROW);
    disp_box->layout_elem->w_mode = VSIZE_GROW;
    disp_box->layout_elem->h_mode = VSIZE_FIXED;
    disp_box->layout_elem->h_px = 56;
    disp_box->layout_elem->bg_color = g_theme ? g_theme->taskbar_bg : 0xFF2E3440u;
    disp_box->layout_elem->border_color = g_theme ? g_theme->border : 0xFF4C566Au;
    disp_box->layout_elem->border_width = 1;
    disp_box->layout_elem->pad_right = 16;
    disp_box->layout_elem->pad_top = 14;
    disp_box->layout_elem->align_items = VALIGN_CENTER;
    ui_widget_add_child(root, disp_box);

    ui_widget_t *spacer = ui_box(ctx, VDIR_ROW);
    spacer->layout_elem->w_mode = VSIZE_GROW;
    ui_widget_add_child(disp_box, spacer);

    state.label = ui_label(ctx, state.display, g_theme ? g_theme->accent : 0xFF88C0D0u);
    state.label->layout_elem->font_size = 16.0f;
    ui_widget_add_child(disp_box, state.label);

    static const char *labels[5][4] = {
        { "C", "+/-", "%", "/" },
        { "7", "8",   "9", "*" },
        { "4", "5",   "6", "-" },
        { "1", "2",   "3", "+" },
        { "0", "00",  ".", "=" }
    };

    for (int r = 0; r < 5; r++) {
        ui_widget_t *row = ui_box(ctx, VDIR_ROW);
        row->layout_elem->w_mode = VSIZE_GROW;
        row->layout_elem->h_mode = VSIZE_FIXED;
        row->layout_elem->h_px = 44;
        row->layout_elem->gap = 8;
        for (int c = 0; c < 4; c++) {
            ui_widget_t *b = ui_button(ctx, labels[r][c], on_calc_button, &state);
            b->layout_elem->w_mode = VSIZE_GROW;
            b->layout_elem->h_mode = VSIZE_GROW;
            ui_widget_add_child(row, b);
        }
        ui_widget_add_child(root, row);
    }

    int running = 1;
    while (running) {
        vanilla_event_t ev;
        while (vanilla_poll_event(client, &ev) > 0) {
            if (ev.type == VANILLA_EVENT_CLOSE_REQ) { running = 0; break; }
            if (ev.type == VANILLA_EVENT_INPUT) {
                ui_handle_event(ctx, root, &ev.input);
                if (ev.input.type == EV_KEY && ev.input.value == 1) {
                    char asc = vanilla_evdev_to_ascii(ev.input.code, 0);
                    if ((asc >= '0' && asc <= '9') || asc == '+' || asc == '-' ||
                        asc == '*' || asc == '/' || asc == '=' || asc == '.' ||
                        asc == 'c' || asc == 'C' || ev.input.code == KEY_ESC) {
                        char tok[2] = { (asc == 'c' || asc == 'C' || ev.input.code == KEY_ESC) ? 'C' : asc, '\0' };
                        calc_handle_input(&state, tok);
                        state.label->label.text = state.display;
                        ui_widget_invalidate(state.label);
                    }
                }
            }
        }
        ui_render(ctx, root, &win->surface, NULL);
        vanilla_present(win, NULL);
        usleep(16000);
    }

    vanilla_destroy_window(win);
    vanilla_disconnect(client);
    return 0;
}
