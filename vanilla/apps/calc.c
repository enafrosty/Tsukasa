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
#include <math.h>
#include <errno.h>
#include <sys/poll.h>

#define CALC_WIDTH     320
#define CALC_HEIGHT    420
#define CALC_MAX_INPUT 15

enum {
    CMD_MODE_STD = 1,
    CMD_MODE_SCI,
    CMD_EXIT,
    CMD_COPY,
    CMD_PASTE,
    CMD_CLEAR,
    CMD_SHORTCUTS,
    CMD_ABOUT
};

typedef struct {
    char         display[32];
    char         expr[64];
    double       accum;
    double       current;
    char         op;
    int          has_decimal;
    int          reset_on_next;
    int          error;
    int          scientific;
    int          running;
    int          shift_down;
    int          ctrl_down;
    int          alt_down;
    int          open_menu;

    ui_ctx_t    *ui_ctx;
    ui_widget_t *root;
    ui_widget_t *menubar;
    ui_widget_t *mode_menu;
    ui_widget_t *edit_menu;
    ui_widget_t *help_menu;
    ui_widget_t *about_dlg;
    ui_widget_t *shortcuts_dlg;
    ui_widget_t *expr_label;
    ui_widget_t *disp_label;
    ui_widget_t *sci_wrap;
    ui_menu_item_t mode_items[4];
} calc_state_t;

static void calc_format_double(double val, char *buf, size_t cap)
{
    if (!buf || cap == 0)
        return;

    if (val != val || val > 1e100 || val < -1e100) {
        snprintf(buf, cap, "Error");
        return;
    }

    int neg = (val < 0.0);
    double a = neg ? -val : val;

    if (a < 1e-12) {
        snprintf(buf, cap, "0");
        return;
    }

    if (a >= 1e12 || a < 1e-6) {
        int exp = 0;
        double m = a;
        while (m >= 10.0 && exp < 99) {
            m /= 10.0;
            exp++;
        }
        while (m < 1.0 && exp > -99) {
            m *= 10.0;
            exp--;
        }
        unsigned long long scaled = (unsigned long long)(m * 1000000.0 + 0.5);
        if (scaled >= 10000000ULL) {
            scaled /= 10;
            exp++;
        }
        unsigned long long lead = scaled / 1000000ULL;
        unsigned long long frac = scaled % 1000000ULL;
        char fbuf[8];
        snprintf(fbuf, sizeof(fbuf), "%06llu", frac);
        int flen = 6;
        while (flen > 0 && fbuf[flen - 1] == '0')
            fbuf[--flen] = '\0';
        char esign = (exp < 0) ? '-' : '+';
        int eabs = (exp < 0) ? -exp : exp;
        if (flen > 0) {
            snprintf(buf, cap, "%s%llu.%se%c%02d",
                     neg ? "-" : "", lead, fbuf, esign, eabs);
        } else {
            snprintf(buf, cap, "%s%llue%c%02d",
                     neg ? "-" : "", lead, esign, eabs);
        }
        return;
    }

    unsigned long long ipart = (unsigned long long)floor(a);
    double rem = a - (double)ipart;
    unsigned long long fpart = (unsigned long long)floor(rem * 100000000.0 + 0.5);
    if (fpart >= 100000000ULL) {
        ipart++;
        fpart -= 100000000ULL;
    }

    if (fpart == 0) {
        snprintf(buf, cap, "%s%llu", neg ? "-" : "", ipart);
        return;
    }

    char fbuf[12];
    snprintf(fbuf, sizeof(fbuf), "%08llu", fpart);
    int flen = 8;
    while (flen > 0 && fbuf[flen - 1] == '0')
        fbuf[--flen] = '\0';

    snprintf(buf, cap, "%s%llu.%s", neg ? "-" : "", ipart, fbuf);
}

static void calc_sync_labels(calc_state_t *st)
{
    if (st->disp_label)
        ui_label_set_text(st->disp_label, st->display);
    if (st->expr_label)
        ui_label_set_text(st->expr_label, st->expr);
}

static void calc_clear_all(calc_state_t *st)
{
    st->accum = 0.0;
    st->current = 0.0;
    st->op = 0;
    st->has_decimal = 0;
    st->reset_on_next = 0;
    st->error = 0;
    strcpy(st->display, "0");
    st->expr[0] = '\0';
}

static void calc_backspace(calc_state_t *st)
{
    if (st->error) {
        calc_clear_all(st);
        return;
    }
    if (st->reset_on_next)
        return;

    int dlen = (int)strlen(st->display);
    if (dlen > 1) {
        st->display[dlen - 1] = '\0';
        if (strcmp(st->display, "-") == 0)
            strcpy(st->display, "0");
    } else {
        strcpy(st->display, "0");
    }
    st->has_decimal = (strchr(st->display, '.') != NULL);
    st->current = strtod(st->display, NULL);
}

static void calc_execute_op(calc_state_t *st)
{
    if (st->op == '+') {
        st->accum += st->current;
    } else if (st->op == '-') {
        st->accum -= st->current;
    } else if (st->op == '*') {
        st->accum *= st->current;
    } else if (st->op == '/') {
        if (st->current != 0.0) {
            st->accum /= st->current;
        } else {
            strcpy(st->display, "Error");
            st->error = 1;
            st->reset_on_next = 1;
            st->op = 0;
            return;
        }
    } else {
        st->accum = st->current;
    }

    calc_format_double(st->accum, st->display, sizeof(st->display));
    if (strcmp(st->display, "Error") == 0) {
        st->error = 1;
        st->op = 0;
    } else {
        st->current = st->accum;
    }
    st->reset_on_next = 1;
    st->has_decimal = (strchr(st->display, '.') != NULL);
}

static void calc_handle_input(calc_state_t *st, const char *tok)
{
    if (!tok || tok[0] == '\0')
        return;

    if (strcmp(tok, "C") == 0) {
        calc_clear_all(st);
        return;
    }

    if (st->error) {
        if ((tok[0] >= '0' && tok[0] <= '9') || strcmp(tok, ".") == 0 ||
            strcmp(tok, "pi") == 0) {
            calc_clear_all(st);
        } else {
            return;
        }
    }

    if (strcmp(tok, "+/-") == 0) {
        st->current = -st->current;
        calc_format_double(st->current, st->display, sizeof(st->display));
        st->has_decimal = (strchr(st->display, '.') != NULL);
    } else if (strcmp(tok, "%") == 0) {
        st->current /= 100.0;
        calc_format_double(st->current, st->display, sizeof(st->display));
        st->has_decimal = (strchr(st->display, '.') != NULL);
    } else if (strcmp(tok, "sqrt") == 0) {
        if (st->current < 0.0) {
            snprintf(st->expr, sizeof(st->expr), "sqrt(%s)", st->display);
            strcpy(st->display, "Error");
            st->error = 1;
            st->reset_on_next = 1;
        } else {
            char arg[32];
            calc_format_double(st->current, arg, sizeof(arg));
            snprintf(st->expr, sizeof(st->expr), "sqrt(%s) =", arg);
            st->current = sqrt(st->current);
            calc_format_double(st->current, st->display, sizeof(st->display));
            st->reset_on_next = 1;
        }
    } else if (strcmp(tok, "x^2") == 0) {
        char arg[32];
        calc_format_double(st->current, arg, sizeof(arg));
        snprintf(st->expr, sizeof(st->expr), "sqr(%s) =", arg);
        st->current = st->current * st->current;
        calc_format_double(st->current, st->display, sizeof(st->display));
        if (strcmp(st->display, "Error") == 0)
            st->error = 1;
        st->reset_on_next = 1;
    } else if (strcmp(tok, "1/x") == 0) {
        char arg[32];
        calc_format_double(st->current, arg, sizeof(arg));
        if (st->current == 0.0) {
            snprintf(st->expr, sizeof(st->expr), "1/(%s)", arg);
            strcpy(st->display, "Error");
            st->error = 1;
        } else {
            snprintf(st->expr, sizeof(st->expr), "1/(%s) =", arg);
            st->current = 1.0 / st->current;
            calc_format_double(st->current, st->display, sizeof(st->display));
        }
        st->reset_on_next = 1;
    } else if (strcmp(tok, "pi") == 0) {
        st->current = M_PI;
        calc_format_double(st->current, st->display, sizeof(st->display));
        st->has_decimal = 1;
        st->reset_on_next = 1;
    } else if (strlen(tok) == 1 && strchr("+-*/", tok[0]) != NULL) {
        if (st->op && !st->reset_on_next)
            calc_execute_op(st);
        else
            st->accum = st->current;

        if (!st->error) {
            char lhs[32];
            st->op = tok[0];
            st->reset_on_next = 1;
            st->has_decimal = 0;
            calc_format_double(st->accum, lhs, sizeof(lhs));
            snprintf(st->expr, sizeof(st->expr), "%s %c", lhs, st->op);
        }
    } else if (strcmp(tok, "=") == 0) {
        if (st->op) {
            char lhs[32];
            char rhs[32];
            calc_format_double(st->accum, lhs, sizeof(lhs));
            calc_format_double(st->current, rhs, sizeof(rhs));
            snprintf(st->expr, sizeof(st->expr), "%s %c %s =", lhs, st->op, rhs);
            calc_execute_op(st);
            st->op = 0;
        }
    } else if (strcmp(tok, ".") == 0) {
        if (st->reset_on_next) {
            strcpy(st->display, "0.");
            st->current = 0.0;
            st->has_decimal = 1;
            st->reset_on_next = 0;
        } else if (!st->has_decimal && strlen(st->display) < CALC_MAX_INPUT) {
            strcat(st->display, ".");
            st->has_decimal = 1;
        }
    } else if (tok[0] >= '0' && tok[0] <= '9') {
        if (st->reset_on_next || strcmp(st->display, "0") == 0) {
            if (strcmp(tok, "00") == 0) {
                strcpy(st->display, "0");
            } else {
                strncpy(st->display, tok, sizeof(st->display) - 1);
                st->display[sizeof(st->display) - 1] = '\0';
            }
            st->reset_on_next = 0;
            st->has_decimal = 0;
        } else if (strlen(st->display) + strlen(tok) <= CALC_MAX_INPUT) {
            strcat(st->display, tok);
        }
        st->current = strtod(st->display, NULL);
    }
}

static void calc_copy_display(calc_state_t *st)
{
    if (clipboard_set(st->display, strlen(st->display)) == 0) {
        snprintf(st->expr, sizeof(st->expr), "Copied: %s", st->display);
        calc_sync_labels(st);
    }
}

static void calc_paste_clipboard(calc_state_t *st)
{
    char buf[64];
    int n = clipboard_get(buf, sizeof(buf));
    if (n <= 0)
        return;

    buf[sizeof(buf) - 1] = '\0';
    char *endp = NULL;
    double val = strtod(buf, &endp);
    if (endp == buf)
        return;

    st->error = 0;
    st->current = val;
    calc_format_double(val, st->display, sizeof(st->display));
    st->has_decimal = (strchr(st->display, '.') != NULL);
    st->reset_on_next = 0;
    snprintf(st->expr, sizeof(st->expr), "Pasted");
    calc_sync_labels(st);
}

static void calc_sync_mode_ui(calc_state_t *st)
{
    st->mode_items[0].flags = st->scientific ? 0u : UI_MENU_ITEM_CHECKED;
    st->mode_items[1].flags = st->scientific ? UI_MENU_ITEM_CHECKED : 0u;
    if (st->mode_menu)
        ui_menu_set_rich_items(st->mode_menu, st->mode_items, 4);

    if (st->sci_wrap && st->sci_wrap->layout_elem) {
        ui_widget_set_hidden(st->sci_wrap, !st->scientific);
        st->sci_wrap->layout_elem->h_mode = VSIZE_FIXED;
        st->sci_wrap->layout_elem->h_px = st->scientific ? 44 : 0;
        st->sci_wrap->layout_elem->pad_bottom = st->scientific ? 8 : 0;
        ui_widget_invalidate(st->root);
    }
}

static void on_calc_button(ui_widget_t *w, const ui_event_t *ev, void *ud)
{
    if (ev->type != UI_EVENT_CLICK || !w || !w->button.label)
        return;
    calc_state_t *st = (calc_state_t *)ud;
    calc_handle_input(st, w->button.label);
    calc_sync_labels(st);
}

static void on_dialog_closed(ui_widget_t *w, const ui_event_t *ev, void *ud)
{
    (void)ev;
    calc_state_t *st = (calc_state_t *)ud;
    if (w && w == st->about_dlg && w->modal.result == UI_DIALOG_RES_CANCEL)
        calc_copy_display(st);
}

static void calc_execute_menu_cmd(calc_state_t *st, uint32_t cmd)
{
    switch (cmd) {
    case CMD_MODE_STD:
        st->scientific = 0;
        calc_sync_mode_ui(st);
        break;
    case CMD_MODE_SCI:
        st->scientific = 1;
        calc_sync_mode_ui(st);
        break;
    case CMD_EXIT:
        st->running = 0;
        break;
    case CMD_COPY:
        calc_copy_display(st);
        break;
    case CMD_PASTE:
        calc_paste_clipboard(st);
        break;
    case CMD_CLEAR:
        calc_clear_all(st);
        calc_sync_labels(st);
        break;
    case CMD_SHORTCUTS:
        if (st->shortcuts_dlg)
            ui_ctx_show_modal(st->ui_ctx, st->shortcuts_dlg);
        break;
    case CMD_ABOUT:
        if (st->about_dlg)
            ui_ctx_show_modal(st->ui_ctx, st->about_dlg);
        break;
    default:
        break;
    }
}

static void on_menu_item(ui_widget_t *w, const ui_event_t *ev, void *ud)
{
    if (ev->type != UI_EVENT_VALUE_CHANGED || !w || !w->menu.rich_items)
        return;
    calc_state_t *st = (calc_state_t *)ud;
    int32_t idx = ev->toggle.state;
    if (idx < 0 || idx >= w->menu.count)
        return;
    st->open_menu = -1;
    if (st->menubar) {
        st->menubar->menu.selected = -1;
        ui_widget_invalidate(st->menubar);
    }
    calc_execute_menu_cmd(st, w->menu.rich_items[idx].id);
}

static void on_menubar_event(ui_widget_t *w, const ui_event_t *ev, void *ud)
{
    if (ev->type != UI_EVENT_VALUE_CHANGED || !w)
        return;
    calc_state_t *st = (calc_state_t *)ud;
    int32_t idx = ev->toggle.state;

    if (st->open_menu == idx) {
        st->open_menu = -1;
        w->menu.selected = -1;
        ui_ctx_hide_popup(st->ui_ctx);
        ui_widget_invalidate(w);
        return;
    }

    ui_widget_t *pop = NULL;
    if (idx == 0)
        pop = st->mode_menu;
    else if (idx == 1)
        pop = st->edit_menu;
    else if (idx == 2)
        pop = st->help_menu;

    if (!pop)
        return;

    st->open_menu = idx;
    int32_t px = ui_menubar_item_x(w, idx);
    int32_t py = w->layout_elem ? (w->layout_elem->computed_y + w->layout_elem->computed_h) : 28;
    ui_ctx_show_popup(st->ui_ctx, pop, px, py);
}

static int calc_update_modifier_state(calc_state_t *st,
                                      const struct input_event *iev)
{
    if (iev->code == KEY_LEFTSHIFT || iev->code == KEY_RIGHTSHIFT) {
        st->shift_down = (iev->value != 0);
        return 1;
    }
    if (iev->code == KEY_LEFTCTRL || iev->code == KEY_RIGHTCTRL) {
        st->ctrl_down = (iev->value != 0);
        return 1;
    }
    if (iev->code == KEY_LEFTALT || iev->code == KEY_RIGHTALT) {
        st->alt_down = (iev->value != 0);
        return 1;
    }
    return 0;
}

static void calc_handle_key(calc_state_t *st, const struct input_event *iev)
{
    if (calc_update_modifier_state(st, iev))
        return;

    if (iev->value != 1)
        return;

    if (st->ctrl_down) {
        if (iev->code == KEY_C) {
            calc_copy_display(st);
            return;
        }
        if (iev->code == KEY_V) {
            calc_paste_clipboard(st);
            return;
        }
        if (iev->code == KEY_Q) {
            st->running = 0;
            return;
        }
    }

    if (st->alt_down) {
        if (iev->code == KEY_1) {
            st->scientific = 0;
            calc_sync_mode_ui(st);
            return;
        }
        if (iev->code == KEY_2) {
            st->scientific = 1;
            calc_sync_mode_ui(st);
            return;
        }
    }

    char asc = vanilla_evdev_to_ascii(iev->code, st->shift_down);
    if ((asc >= '0' && asc <= '9') || asc == '+' || asc == '-' ||
        asc == '*' || asc == '/' || asc == '%') {
        char tok[2] = { asc, '\0' };
        calc_handle_input(st, tok);
    } else if (asc == '\n' || asc == '=' ||
               iev->code == KEY_ENTER || iev->code == KEY_KPENTER) {
        calc_handle_input(st, "=");
    } else if (asc == '.' || asc == ',') {
        calc_handle_input(st, ".");
    } else if (asc == 'c' || asc == 'C' || iev->code == KEY_ESC) {
        calc_handle_input(st, "C");
    } else if (iev->code == KEY_BACKSPACE) {
        calc_backspace(st);
    } else {
        return;
    }

    calc_sync_labels(st);
}

int main(int argc, char **argv)
{
    (void)argc;
    (void)argv;

    calc_state_t state;
    memset(&state, 0, sizeof(state));
    state.running = 1;
    state.open_menu = -1;
    strcpy(state.display, "0");

    vanilla_client_t *client = vanilla_connect(NULL);
    if (!client)
        return 1;

    vanilla_window_t *win = vanilla_create_window(client, "Calculator", 860, 110,
                                                  CALC_WIDTH, CALC_HEIGHT,
                                                  WINDOW_FLAG_RESIZABLE);
    if (!win) {
        vanilla_disconnect(client);
        return 1;
    }

    vanilla_set_size_hints(client, win->window_id, 240, 300, 0, 0, 0, 0);
    vanilla_map_window(win);

    static uint8_t layout_arena[64 * 1024];
    static uint8_t ui_arena[128 * 1024];
    vanilla_layout_t *lctx = vlayout_init(layout_arena, sizeof(layout_arena));
    state.ui_ctx = ui_ctx_init(ui_arena, sizeof(ui_arena), lctx);
    if (!state.ui_ctx) {
        vanilla_destroy_window(win);
        vanilla_disconnect(client);
        return 1;
    }

    uint32_t bg_base = g_theme ? g_theme->bg_base : 0xFF2E3440u;
    uint32_t bg_elev = g_theme ? g_theme->bg_elevated : 0xFF3B4252u;
    uint32_t border_col = g_theme ? g_theme->border : 0xFF4C566Au;
    uint32_t fg_muted = g_theme ? g_theme->fg_muted : 0xFFD8DEE9u;
    uint32_t accent = g_theme ? g_theme->accent : 0xFF88C0D0u;

    state.root = ui_box(state.ui_ctx, VDIR_COLUMN);
    state.root->layout_elem->w_mode = VSIZE_GROW;
    state.root->layout_elem->h_mode = VSIZE_GROW;
    state.root->layout_elem->bg_color = bg_base;

    static const char *bar_labels[] = { "Mode", "Edit", "Help" };
    state.menubar = ui_menubar(state.ui_ctx, bar_labels, 3, on_menubar_event, &state);
    ui_widget_add_child(state.root, state.menubar);

    state.mode_items[0] = (ui_menu_item_t){ "Standard",   "Alt+1", UI_MENU_ITEM_CHECKED,   CMD_MODE_STD };
    state.mode_items[1] = (ui_menu_item_t){ "Scientific", "Alt+2", 0,                      CMD_MODE_SCI };
    state.mode_items[2] = (ui_menu_item_t){ NULL,         NULL,    UI_MENU_ITEM_SEPARATOR, 0 };
    state.mode_items[3] = (ui_menu_item_t){ "Exit",       "Ctrl+Q", 0,                     CMD_EXIT };
    state.mode_menu = ui_menu_ex(state.ui_ctx, state.mode_items, 4, on_menu_item, &state);

    static const ui_menu_item_t edit_items[] = {
        { "Copy",  "Ctrl+C", 0,                      CMD_COPY },
        { "Paste", "Ctrl+V", 0,                      CMD_PASTE },
        { NULL,    NULL,     UI_MENU_ITEM_SEPARATOR, 0 },
        { "Clear", "Esc",    0,                      CMD_CLEAR },
    };
    state.edit_menu = ui_menu_ex(state.ui_ctx, edit_items, 4, on_menu_item, &state);

    static const ui_menu_item_t help_items[] = {
        { "Keyboard Shortcuts", NULL, 0,                      CMD_SHORTCUTS },
        { NULL,                 NULL, UI_MENU_ITEM_SEPARATOR, 0 },
        { "About Calculator",   NULL, 0,                      CMD_ABOUT },
    };
    state.help_menu = ui_menu_ex(state.ui_ctx, help_items, 3, on_menu_item, &state);

    state.about_dlg = ui_dialog_confirm(
        state.ui_ctx,
        "About Calculator",
        "Project Vanilla Calculator\nStandard & Scientific arithmetic\nBuilt with libvanilla-ui",
        "OK", "Copy Result", on_dialog_closed, &state);

    state.shortcuts_dlg = ui_dialog_confirm(
        state.ui_ctx,
        "Keyboard Shortcuts",
        "0-9, + - * / . % : Calculate\nEnter / = : Evaluate   Esc / C : Clear\nCtrl+C / Ctrl+V : Copy / Paste",
        "OK", "Close", NULL, NULL);

    ui_widget_t *body = ui_box(state.ui_ctx, VDIR_COLUMN);
    body->layout_elem->w_mode = VSIZE_GROW;
    body->layout_elem->h_mode = VSIZE_GROW;
    body->layout_elem->pad_left = 12;
    body->layout_elem->pad_right = 12;
    body->layout_elem->pad_top = 12;
    body->layout_elem->pad_bottom = 12;
    body->layout_elem->gap = 8;
    ui_widget_add_child(state.root, body);

    ui_widget_t *disp_box = ui_box(state.ui_ctx, VDIR_COLUMN);
    disp_box->layout_elem->w_mode = VSIZE_GROW;
    disp_box->layout_elem->h_mode = VSIZE_FIXED;
    disp_box->layout_elem->h_px = 68;
    disp_box->layout_elem->pad_left = 12;
    disp_box->layout_elem->pad_right = 12;
    disp_box->layout_elem->pad_top = 8;
    disp_box->layout_elem->pad_bottom = 8;
    disp_box->layout_elem->gap = 2;
    disp_box->layout_elem->bg_color = bg_elev;
    disp_box->layout_elem->border_color = border_col;
    disp_box->layout_elem->border_width = 1;
    disp_box->layout_elem->corner_radius = g_theme ? g_theme->radius_sm : 4;
    disp_box->layout_elem->clip_children = 1;
    ui_widget_add_child(body, disp_box);

    ui_widget_t *expr_row = ui_box(state.ui_ctx, VDIR_ROW);
    expr_row->layout_elem->w_mode = VSIZE_GROW;
    expr_row->layout_elem->h_mode = VSIZE_FIXED;
    expr_row->layout_elem->h_px = 18;
    expr_row->layout_elem->align_items = VALIGN_CENTER;
    ui_widget_add_child(disp_box, expr_row);

    ui_widget_t *expr_spacer = ui_box(state.ui_ctx, VDIR_ROW);
    expr_spacer->layout_elem->w_mode = VSIZE_GROW;
    ui_widget_add_child(expr_row, expr_spacer);

    state.expr_label = ui_label_ex(state.ui_ctx, state.expr, fg_muted, 12.0f, APP_FONT_REGULAR);
    ui_widget_add_child(expr_row, state.expr_label);

    ui_widget_t *disp_row = ui_box(state.ui_ctx, VDIR_ROW);
    disp_row->layout_elem->w_mode = VSIZE_GROW;
    disp_row->layout_elem->h_mode = VSIZE_GROW;
    disp_row->layout_elem->align_items = VALIGN_CENTER;
    ui_widget_add_child(disp_box, disp_row);

    ui_widget_t *disp_spacer = ui_box(state.ui_ctx, VDIR_ROW);
    disp_spacer->layout_elem->w_mode = VSIZE_GROW;
    ui_widget_add_child(disp_row, disp_spacer);

    state.disp_label = ui_label_ex(state.ui_ctx, state.display, accent, 24.0f, APP_FONT_BOLD);
    ui_widget_add_child(disp_row, state.disp_label);

    ui_widget_t *keypad_area = ui_box(state.ui_ctx, VDIR_COLUMN);
    keypad_area->layout_elem->w_mode = VSIZE_GROW;
    keypad_area->layout_elem->h_mode = VSIZE_GROW;
    keypad_area->layout_elem->gap = 0;
    ui_widget_add_child(body, keypad_area);

    state.sci_wrap = ui_box(state.ui_ctx, VDIR_ROW);
    state.sci_wrap->layout_elem->w_mode = VSIZE_GROW;
    state.sci_wrap->layout_elem->h_mode = VSIZE_FIXED;
    state.sci_wrap->layout_elem->h_px = 0;
    state.sci_wrap->layout_elem->gap = 8;
    state.sci_wrap->layout_elem->clip_children = 1;
    ui_widget_set_hidden(state.sci_wrap, 1);

    static const char *sci_labels[4] = { "sqrt", "x^2", "1/x", "pi" };
    for (int c = 0; c < 4; c++) {
        ui_widget_t *sb = ui_button(state.ui_ctx, sci_labels[c], on_calc_button, &state);
        sb->layout_elem->w_mode = VSIZE_GROW;
        sb->layout_elem->h_mode = VSIZE_GROW;
        ui_widget_set_font(sb, 0.0f, APP_FONT_MEDIUM);
        ui_widget_add_child(state.sci_wrap, sb);
    }
    ui_widget_add_child(keypad_area, state.sci_wrap);

    ui_widget_t *grid = ui_box(state.ui_ctx, VDIR_COLUMN);
    grid->layout_elem->w_mode = VSIZE_GROW;
    grid->layout_elem->h_mode = VSIZE_GROW;
    grid->layout_elem->gap = 8;
    ui_widget_add_child(keypad_area, grid);

    static const char *labels[5][4] = {
        { "C", "+/-", "%", "/" },
        { "7", "8",   "9", "*" },
        { "4", "5",   "6", "-" },
        { "1", "2",   "3", "+" },
        { "0", "00",  ".", "=" }
    };

    for (int r = 0; r < 5; r++) {
        ui_widget_t *row = ui_box(state.ui_ctx, VDIR_ROW);
        row->layout_elem->w_mode = VSIZE_GROW;
        row->layout_elem->h_mode = VSIZE_GROW;
        row->layout_elem->gap = 8;
        for (int c = 0; c < 4; c++) {
            ui_widget_t *b = ui_button(state.ui_ctx, labels[r][c], on_calc_button, &state);
            b->layout_elem->w_mode = VSIZE_GROW;
            b->layout_elem->h_mode = VSIZE_GROW;
            int is_op = (c == 3 || (r == 0 && c > 0));
            ui_widget_set_font(b, 0.0f, is_op ? APP_FONT_BOLD : APP_FONT_MEDIUM);
            ui_widget_add_child(row, b);
        }
        ui_widget_add_child(grid, row);
    }

    ui_render(state.ui_ctx, state.root, &win->surface, NULL);
    vanilla_present(win, NULL);

    ui_widget_t *prev_popup = ui_ctx_get_popup(state.ui_ctx);
    ui_widget_t *prev_modal = ui_ctx_get_modal(state.ui_ctx);

    struct pollfd pfd;
    pfd.fd = vanilla_client_get_fd(client);
    pfd.events = POLLIN;
    pfd.revents = 0;

    while (state.running) {
        vanilla_event_t ev;
        int ev_ret;
        int force_present = 0;

        while ((ev_ret = vanilla_poll_event(client, &ev)) > 0) {
            if (ev.type == VANILLA_EVENT_CLOSE_REQ) {
                state.running = 0;
                break;
            }
            if (ev.type == VANILLA_EVENT_CONFIGURE && ev.window_id == win->window_id) {
                vanilla_ack_configure(client, win->window_id, ev.configure.serial);
                ui_widget_invalidate(state.root);
                force_present = 1;
            }
            if (ev.type == VANILLA_EVENT_INPUT ||
                ev.type == VANILLA_EVENT_DOUBLE_CLICK ||
                ev.type == VANILLA_EVENT_TRIPLE_CLICK) {
                int modal_open = (ui_ctx_get_modal(state.ui_ctx) != NULL);
                int popup_open = (ui_ctx_get_popup(state.ui_ctx) != NULL);

                if (ev.type == VANILLA_EVENT_INPUT && ev.input.type == EV_KEY &&
                    !modal_open && !popup_open && ev.input.value == 1 &&
                    (ev.input.code == KEY_ENTER || ev.input.code == KEY_KPENTER)) {
                    calc_handle_key(&state, &ev.input);
                    continue;
                }

                ui_handle_event(state.ui_ctx, state.root, &ev.input);

                if (ui_ctx_get_popup(state.ui_ctx) == NULL && state.open_menu != -1) {
                    state.open_menu = -1;
                    state.menubar->menu.selected = -1;
                    ui_widget_invalidate(state.menubar);
                }

                /* Update modifier keys even when an overlay is open so releases are never missed. */
                if (ev.type == VANILLA_EVENT_INPUT && ev.input.type == EV_KEY) {
                    if (!calc_update_modifier_state(&state, &ev.input) &&
                        !modal_open && !popup_open) {
                        calc_handle_key(&state, &ev.input);
                    }
                }
            }
        }

        if (!state.running || ev_ret < 0)
            break;

        ui_widget_t *cur_popup = ui_ctx_get_popup(state.ui_ctx);
        ui_widget_t *cur_modal = ui_ctx_get_modal(state.ui_ctx);
        int dirty = force_present ||
                    state.root->dirty ||
                    (cur_popup && !cur_popup->hidden && cur_popup->dirty) ||
                    (cur_modal && !cur_modal->hidden && cur_modal->dirty) ||
                    (cur_popup != prev_popup) ||
                    (cur_modal != prev_modal);

        if (dirty) {
            ui_render(state.ui_ctx, state.root, &win->surface, NULL);
            vanilla_present(win, NULL);
            prev_popup = cur_popup;
            prev_modal = cur_modal;
        }

        pfd.revents = 0;
        int pret = poll(&pfd, 1, -1);
        if (pret < 0) {
            if (errno == EINTR)
                continue;
            break;
        }
        if (pret > 0 && (pfd.revents & (POLLERR | POLLHUP | POLLNVAL)) &&
            !(pfd.revents & POLLIN)) {
            break;
        }
    }

    vanilla_destroy_window(win);
    vanilla_disconnect(client);
    return 0;
}
