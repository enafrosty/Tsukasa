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
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define CALC_WIDTH   280
#define CALC_HEIGHT  360

typedef struct {
    char label[8];
    int  x;
    int  y;
    int  w;
    int  h;
    int  pressed;
} calc_btn_t;

typedef struct {
    char       display[32];
    double     accum;
    double     current;
    char       op;
    int        has_decimal;
    int        reset_on_next;
    calc_btn_t buttons[20];
    int        num_buttons;
    int        dirty;
} calc_state_t;

static void calc_init_buttons(calc_state_t *st)
{
    static const char *labels[5][4] = {
        { "C", "+/-", "%", "/" },
        { "7", "8",   "9", "*" },
        { "4", "5",   "6", "-" },
        { "1", "2",   "3", "+" },
        { "0", ".",   "=", "=" }
    };

    int idx = 0;
    int start_x = 16;
    int start_y = 90;
    int btn_w = 56;
    int btn_h = 44;
    int gap = 8;

    for (int r = 0; r < 5; r++) {
        for (int c = 0; c < 4; c++) {
            if (r == 4 && c == 3)
                continue; /* Zero spans 2 columns */

            st->buttons[idx].x = start_x + c * (btn_w + gap);
            st->buttons[idx].y = start_y + r * (btn_h + gap);
            st->buttons[idx].w = (r == 4 && c == 0) ? (btn_w * 2 + gap) : btn_w;
            st->buttons[idx].h = btn_h;
            st->buttons[idx].pressed = 0;
            strncpy(st->buttons[idx].label, labels[r][c], sizeof(st->buttons[idx].label) - 1);
            idx++;
            if (r == 4 && c == 0)
                c++;
        }
    }
    st->num_buttons = idx;
}

static void calc_execute_op(calc_state_t *st)
{
    if (st->op == '+')
        st->accum += st->current;
    else if (st->op == '-')
        st->accum -= st->current;
    else if (st->op == '*')
        st->accum *= st->current;
    else if (st->op == '/') {
        if (st->current != 0.0)
            st->accum /= st->current;
        else {
            strcpy(st->display, "Error");
            st->reset_on_next = 1;
            st->dirty = 1;
            return;
        }
    } else {
        st->accum = st->current;
    }

    /* Format result nicely */
    long int_part = (long)st->accum;
    if (st->accum == (double)int_part) {
        snprintf(st->display, sizeof(st->display), "%ld", int_part);
    } else {
        snprintf(st->display, sizeof(st->display), "%.4g", st->accum);
    }
    st->current = st->accum;
    st->reset_on_next = 1;
    st->dirty = 1;
}

static double calc_parse_double(const char *s)
{
    if (!s) return 0.0;
    double res = 0.0;
    double sign = 1.0;
    if (*s == '-') {
        sign = -1.0;
        s++;
    } else if (*s == '+') {
        s++;
    }
    while (*s >= '0' && *s <= '9') {
        res = res * 10.0 + (*s - '0');
        s++;
    }
    if (*s == '.') {
        s++;
        double frac = 0.1;
        while (*s >= '0' && *s <= '9') {
            res += (*s - '0') * frac;
            frac *= 0.1;
            s++;
        }
    }
    return res * sign;
}

static void calc_handle_input(calc_state_t *st, const char *token)
{
    if (strcmp(token, "C") == 0) {
        st->accum = 0;
        st->current = 0;
        st->op = 0;
        st->has_decimal = 0;
        st->reset_on_next = 0;
        strcpy(st->display, "0");
        st->dirty = 1;
        return;
    }

    if (strcmp(token, "+/-") == 0) {
        st->current = -st->current;
        long int_part = (long)st->current;
        if (st->current == (double)int_part)
            snprintf(st->display, sizeof(st->display), "%ld", int_part);
        else
            snprintf(st->display, sizeof(st->display), "%.4g", st->current);
        st->dirty = 1;
        return;
    }

    if (strcmp(token, "%") == 0) {
        st->current /= 100.0;
        snprintf(st->display, sizeof(st->display), "%.4g", st->current);
        st->dirty = 1;
        return;
    }

    if (strcmp(token, "+") == 0 || strcmp(token, "-") == 0 ||
        strcmp(token, "*") == 0 || strcmp(token, "/") == 0) {
        if (st->op && !st->reset_on_next)
            calc_execute_op(st);
        else
            st->accum = st->current;

        st->op = token[0];
        st->reset_on_next = 1;
        st->has_decimal = 0;
        return;
    }

    if (strcmp(token, "=") == 0) {
        if (st->op) {
            calc_execute_op(st);
            st->op = 0;
        }
        return;
    }

    if (strcmp(token, ".") == 0) {
        if (st->reset_on_next) {
            strcpy(st->display, "0.");
            st->current = 0.0;
            st->has_decimal = 1;
            st->reset_on_next = 0;
            st->dirty = 1;
            return;
        }
        if (!st->has_decimal) {
            strcat(st->display, ".");
            st->has_decimal = 1;
            st->dirty = 1;
        }
        return;
    }

    /* Digit 0-9 */
    if (token[0] >= '0' && token[0] <= '9') {
        if (st->reset_on_next || strcmp(st->display, "0") == 0) {
            st->display[0] = token[0];
            st->display[1] = '\0';
            st->reset_on_next = 0;
            st->has_decimal = 0;
        } else if (strlen(st->display) < 14) {
            strcat(st->display, token);
        }
        st->current = calc_parse_double(st->display);
        st->dirty = 1;
    }
}

static void calc_render(vanilla_surface_t *surf, calc_state_t *st)
{
    app_fill_rect(surf, 0, 0, CALC_WIDTH, CALC_HEIGHT, APP_COLOR_BG);

    /* LCD Screen Display */
    int lcd_x = 16;
    int lcd_y = 16;
    int lcd_w = CALC_WIDTH - 32;
    int lcd_h = 56;

    app_fill_rect(surf, lcd_x, lcd_y, lcd_w, lcd_h, 0xFF1E222Bu);
    app_draw_rect(surf, lcd_x, lcd_y, lcd_w, lcd_h, APP_COLOR_BORDER);

    /* Right-aligned text */
    int text_len = (int)strlen(st->display);
    int text_w = text_len * 16; /* scale 2 */
    int tx = lcd_x + lcd_w - text_w - 12;
    int ty = lcd_y + (lcd_h - 16) / 2;
    app_draw_text_scale(surf, tx, ty, st->display, 2, APP_COLOR_PRIMARY);

    /* Button matrix */
    for (int i = 0; i < st->num_buttons; i++) {
        calc_btn_t *b = &st->buttons[i];
        app_draw_button(surf, b->x, b->y, b->w, b->h, b->label, b->pressed);
    }
}

int main(int argc, char **argv)
{
    (void)argc;
    (void)argv;

    calc_state_t state;
    memset(&state, 0, sizeof(state));
    strcpy(state.display, "0");
    state.dirty = 1;
    calc_init_buttons(&state);

    vanilla_client_t *client = vanilla_connect(NULL);
    if (!client)
        return 1;

    vanilla_window_t *win = vanilla_create_window(client, "Calculator", 160, 120,
                                                  CALC_WIDTH, CALC_HEIGHT,
                                                  WINDOW_FLAG_NONE);
    if (!win) {
        vanilla_disconnect(client);
        return 1;
    }

    vanilla_map_window(win);

    int running = 1;
    int shift_down = 0;

    while (running) {
        vanilla_event_t ev;
        while (vanilla_poll_event(client, &ev) > 0) {
            if (ev.type == VANILLA_EVENT_CLOSE_REQ) {
                running = 0;
                break;
            } else if (ev.type == VANILLA_EVENT_INPUT) {
                struct input_event *iev = &ev.input;
                if (iev->type == EV_KEY) {
                    if (iev->code == BTN_LEFT) {
                        if (iev->value == 1) {
                            int cx = (int)iev->pad1;
                            int cy = (int)iev->pad2;
                            for (int i = 0; i < state.num_buttons; i++) {
                                calc_btn_t *b = &state.buttons[i];
                                if (cx >= b->x && cx < b->x + b->w &&
                                    cy >= b->y && cy < b->y + b->h) {
                                    b->pressed = 1;
                                    calc_handle_input(&state, b->label);
                                    state.dirty = 1;
                                    break;
                                }
                            }
                        } else {
                            for (int i = 0; i < state.num_buttons; i++) {
                                if (state.buttons[i].pressed) {
                                    state.buttons[i].pressed = 0;
                                    state.dirty = 1;
                                }
                            }
                        }
                    } else if (iev->code == KEY_LEFTSHIFT || iev->code == KEY_RIGHTSHIFT) {
                        shift_down = (iev->value != 0);
                    } else if (iev->value == 1) {
                        char asc = app_evdev_to_ascii(iev->code, shift_down);
                        if (asc >= '0' && asc <= '9') {
                            char tok[2] = { asc, '\0' };
                            calc_handle_input(&state, tok);
                        } else if (asc == '+' || asc == '-' || asc == '*' || asc == '/') {
                            char tok[2] = { asc, '\0' };
                            calc_handle_input(&state, tok);
                        } else if (asc == '\n' || asc == '=') {
                            calc_handle_input(&state, "=");
                        } else if (asc == '.' || asc == ',') {
                            calc_handle_input(&state, ".");
                        } else if (asc == 'c' || asc == 'C' || iev->code == KEY_ESC) {
                            calc_handle_input(&state, "C");
                        }
                    }
                }
            }
        }

        if (state.dirty) {
            calc_render(&win->surface, &state);
            vanilla_present(win, NULL);
            state.dirty = 0;
        }

        usleep(16000);
    }

    vanilla_destroy_window(win);
    vanilla_disconnect(client);
    return 0;
}
