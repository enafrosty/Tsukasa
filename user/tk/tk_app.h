/*
 * Project Tsukasa — tk_app: application object + event loop for SDK GUI apps
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

#ifndef TSUKASA_TK_APP_H
#define TSUKASA_TK_APP_H

#include "tk_client.h"
#include "tk_painter.h"
#include "../include/libwidget.h"

#define TK_TIMER_MAX     8
#define TK_CLIPBOARD_MAX 4096
#define TK_INJECT_MAX    32

typedef struct tk_app tk_app_t;
typedef void (*tk_timer_fn)(tk_app_t *app, void *userdata);

typedef struct tk_timer {
    int in_use;
    uint32_t id;
    uint64_t interval_ticks;
    uint64_t next_due;
    tk_timer_fn fn;
    void *userdata;
} tk_timer_t;

typedef struct tk_inject {
    int is_key;
    int x, y;
    uint32_t buttons;
    uint32_t keycode;
    int pressed;
} tk_inject_t;

struct tk_app {
    tk_surface_t surf;
    tk_paint_target_t target;
    widget_context_t ctx;
    ui_widget_t *root;
    uint32_t bg_color;
    int dirty;
    int quit;
    uint32_t prev_buttons;

    int dialog_open;
    int dialog_question;
    int dialog_result;
    int dialog_done;
    const char *dialog_title;
    const char *dialog_text;
    widget_button_t dialog_ok;
    widget_button_t dialog_cancel;

    tk_timer_t timers[TK_TIMER_MAX];
    uint32_t next_timer_id;

    char clipboard[TK_CLIPBOARD_MAX];

    tk_inject_t inject[TK_INJECT_MAX];
    int inject_head, inject_tail, inject_count;
};

/* Connect + create the surface. 0 on success. */
int  tk_app_init(tk_app_t *app, uint32_t w, uint32_t h, uint32_t bg_color);
void tk_app_set_root(tk_app_t *app, ui_widget_t *root);
void tk_app_request_redraw(tk_app_t *app);

/* One loop iteration: injected events -> server events -> timers -> redraw + DAMAGE/ack when dirty. */
int  tk_app_pump(tk_app_t *app);
void tk_app_run(tk_app_t *app);
void tk_app_quit(tk_app_t *app);
/* Teardown: destroy the surface, close the connection. */
void tk_app_shutdown(tk_app_t *app);

/* Timers (interval in ms, 10 ms granularity). id > 0, or 0 on failure. */
uint32_t tk_app_set_timer(tk_app_t *app, uint32_t interval_ms, tk_timer_fn fn, void *userdata);
void     tk_app_kill_timer(tk_app_t *app, uint32_t timer_id);

void        tk_app_set_clipboard(tk_app_t *app, const char *text);
const char *tk_app_get_clipboard(tk_app_t *app);

int tk_app_inject_pointer(tk_app_t *app, int x, int y, uint32_t buttons);
int tk_app_inject_key(tk_app_t *app, uint32_t keycode, int pressed);

/* Modal dialogs: block in a nested pump until dismissed. */
int tk_dialog_message(tk_app_t *app, const char *title, const char *text);
int tk_dialog_question(tk_app_t *app, const char *title, const char *text);

#endif /* TSUKASA_TK_APP_H */
