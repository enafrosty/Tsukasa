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

#include "tk_app.h"

#include "tsukasa_sdk.h"

#include <stddef.h>

#define TK_DIALOG_W_MIN 200
#define TK_DIALOG_H     96

static int tk_strlen(const char *s)
{
    int n = 0;
    while (s && s[n]) n++;
    return n;
}

int tk_app_init(tk_app_t *app, uint32_t w, uint32_t h, uint32_t bg_color)
{
    int fd;
    int i;

    if (!app)
        return -1;

    fd = tk_connect();
    if (fd < 0)
        return -1;
    if (tk_surface_create(fd, w, h, &app->surf) != 0) {
        close(fd);
        return -1;
    }

    app->target.surf = &app->surf;
    tk_paint_target_reset(&app->target);
    tk_painter_bind(&app->ctx, &app->target, false);

    app->root = 0;
    app->bg_color = bg_color;
    app->dirty = 1;
    app->quit = 0;
    app->prev_buttons = 0;

    app->dialog_open = 0;
    app->dialog_question = 0;
    app->dialog_result = 0;
    app->dialog_done = 0;
    app->dialog_title = 0;
    app->dialog_text = 0;

    for (i = 0; i < TK_TIMER_MAX; i++)
        app->timers[i].in_use = 0;
    app->next_timer_id = 1;

    app->clipboard[0] = '\0';

    app->inject_head = app->inject_tail = app->inject_count = 0;
    return 0;
}

void tk_app_set_root(tk_app_t *app, ui_widget_t *root)
{
    if (!app) return;
    app->root = root;
    app->dirty = 1;
}

void tk_app_request_redraw(tk_app_t *app)
{
    if (app) app->dirty = 1;
}

void tk_app_quit(tk_app_t *app)
{
    if (app) app->quit = 1;
}

void tk_app_shutdown(tk_app_t *app)
{
    if (!app) return;
    tk_surface_teardown(&app->surf);
    if (app->surf.conn_fd >= 0) {
        close(app->surf.conn_fd);
        app->surf.conn_fd = -1;
    }
}

void tk_app_set_clipboard(tk_app_t *app, const char *text)
{
    int i = 0;
    if (!app) return;
    if (text) {
        while (text[i] && i < TK_CLIPBOARD_MAX - 1) {
            app->clipboard[i] = text[i];
            i++;
        }
    }
    app->clipboard[i] = '\0';
}

const char *tk_app_get_clipboard(tk_app_t *app)
{
    return app ? app->clipboard : "";
}

uint32_t tk_app_set_timer(tk_app_t *app, uint32_t interval_ms, tk_timer_fn fn, void *userdata)
{
    int i;
    if (!app || !fn)
        return 0;
    for (i = 0; i < TK_TIMER_MAX; i++) {
        if (!app->timers[i].in_use) {
            uint64_t iv = ((uint64_t)interval_ms + 9) / 10;
            if (iv == 0) iv = 1;
            app->timers[i].in_use = 1;
            app->timers[i].id = app->next_timer_id++;
            app->timers[i].interval_ticks = iv;
            app->timers[i].next_due = ticks() + iv;
            app->timers[i].fn = fn;
            app->timers[i].userdata = userdata;
            return app->timers[i].id;
        }
    }
    return 0;
}

void tk_app_kill_timer(tk_app_t *app, uint32_t timer_id)
{
    int i;
    if (!app) return;
    for (i = 0; i < TK_TIMER_MAX; i++) {
        if (app->timers[i].in_use && app->timers[i].id == timer_id) {
            app->timers[i].in_use = 0;
            return;
        }
    }
}

static void timers_fire_due(tk_app_t *app)
{
    uint64_t now = ticks();
    int i;
    for (i = 0; i < TK_TIMER_MAX; i++) {
        if (app->timers[i].in_use && now >= app->timers[i].next_due) {
            app->timers[i].next_due = now + app->timers[i].interval_ticks;
            app->timers[i].fn(app, app->timers[i].userdata);
        }
    }
}

int tk_app_inject_pointer(tk_app_t *app, int x, int y, uint32_t buttons)
{
    tk_inject_t *r;
    if (!app || app->inject_count >= TK_INJECT_MAX)
        return -1;
    r = &app->inject[app->inject_tail];
    r->is_key = 0;
    r->x = x;
    r->y = y;
    r->buttons = buttons;
    r->keycode = 0;
    r->pressed = 0;
    app->inject_tail = (app->inject_tail + 1) % TK_INJECT_MAX;
    app->inject_count++;
    return 0;
}

int tk_app_inject_key(tk_app_t *app, uint32_t keycode, int pressed)
{
    tk_inject_t *r;
    if (!app || app->inject_count >= TK_INJECT_MAX)
        return -1;
    r = &app->inject[app->inject_tail];
    r->is_key = 1;
    r->x = 0;
    r->y = 0;
    r->buttons = 0;
    r->keycode = keycode;
    r->pressed = pressed;
    app->inject_tail = (app->inject_tail + 1) % TK_INJECT_MAX;
    app->inject_count++;
    return 0;
}

/* dialogs (in-surface modal overlay) */

static void dialog_ok_clicked(void *user_data)
{
    tk_app_t *app = (tk_app_t *)user_data;
    app->dialog_result = 1;
    app->dialog_done = 1;
}

static void dialog_cancel_clicked(void *user_data)
{
    tk_app_t *app = (tk_app_t *)user_data;
    app->dialog_result = 0;
    app->dialog_done = 1;
}

static void dialog_layout(tk_app_t *app)
{
    int tw = tk_strlen(app->dialog_text) * 8 + 40;
    int dw = tw > TK_DIALOG_W_MIN ? tw : TK_DIALOG_W_MIN;
    int dh = TK_DIALOG_H;
    int dx, dy, by;

    if (dw > (int)app->surf.w - 8) dw = (int)app->surf.w - 8;
    dx = ((int)app->surf.w - dw) / 2;
    dy = ((int)app->surf.h - dh) / 2;
    by = dy + dh - 34;

    if (app->dialog_question) {
        widget_button_init(&app->dialog_ok, dx + dw / 2 - 92, by, 84, 24, "OK");
        widget_button_init(&app->dialog_cancel, dx + dw / 2 + 8, by, 84, 24, "Cancel");
        app->dialog_cancel.on_click = dialog_cancel_clicked;
    } else {
        widget_button_init(&app->dialog_ok, dx + (dw - 84) / 2, by, 84, 24, "OK");
    }
    app->dialog_ok.on_click = dialog_ok_clicked;
}

static void accumulate_full(tk_app_t *app);

static void dialog_draw(tk_app_t *app)
{
    int tw = tk_strlen(app->dialog_text) * 8 + 40;
    int dw = tw > TK_DIALOG_W_MIN ? tw : TK_DIALOG_W_MIN;
    int dh = TK_DIALOG_H;
    int dx, dy;

    if (dw > (int)app->surf.w - 8) dw = (int)app->surf.w - 8;
    dx = ((int)app->surf.w - dw) / 2;
    dy = ((int)app->surf.h - dh) / 2;

    tk_paint_fill_rect(&app->surf, dx - 2, dy - 2, dw + 4, dh + 4, 0xFF4C8DFFu);
    tk_paint_fill_rect(&app->surf, dx, dy, dw, dh, 0xFF23272Fu);
    tk_paint_text(&app->surf, dx + 12, dy + 10, app->dialog_title, 0xFFFFFFFFu);
    tk_paint_text(&app->surf, dx + 12, dy + 30, app->dialog_text, 0xFFC8D0DCu);

    widget_button_draw(&app->ctx, &app->dialog_ok);
    if (app->dialog_question)
        widget_button_draw(&app->ctx, &app->dialog_cancel);

    accumulate_full(app);
}

static void accumulate_full(tk_app_t *app)
{
    app->target.dirty = 1;
    app->target.dx = 0;
    app->target.dy = 0;
    app->target.dw = (int)app->surf.w;
    app->target.dh = (int)app->surf.h;
}

/* Redraw the frame and push it: full background, widget tree, dialog overlay, then one DAMAGE + ack round... */
static int frame_flush(tk_app_t *app)
{
    wsd_damage_t dmg;
    wsd_hdr_t h;

    tk_paint_fill_rect(&app->surf, 0, 0, (int)app->surf.w, (int)app->surf.h,
                       app->bg_color);
    if (app->root)
        ui_widget_draw_tree(app->root, &app->ctx);
    if (app->dialog_open)
        dialog_draw(app);
    accumulate_full(app);

    dmg.surface_id = app->surf.surface_id;
    dmg.x = app->target.dx;
    dmg.y = app->target.dy;
    dmg.w = app->target.dw;
    dmg.h = app->target.dh;
    if (!tk_send_frame(app->surf.conn_fd, WSD_MSG_DAMAGE, &dmg,
                       (uint32_t)sizeof(dmg)))
        return -1;

    for (int spin = 0; spin < 20000; spin++) {
        uint8_t payload[64];
        if (!tk_recv_all(app->surf.conn_fd, &h, (int)sizeof(h)))
            return -1;
        if (h.magic != WSD_MAGIC || h.payload_size > sizeof(payload))
            return -1;
        if (h.payload_size > 0 &&
            !tk_recv_all(app->surf.conn_fd, payload, (int)h.payload_size))
            return -1;
        if (h.msg_type == WSD_EVT_DAMAGE_ACK) {
            tk_paint_target_reset(&app->target);
            app->dirty = 0;
            return 0;
        }
        if (h.msg_type == WSD_EVT_POINTER &&
            h.payload_size == sizeof(wsd_evt_pointer_t)) {
            const wsd_evt_pointer_t *p = (const wsd_evt_pointer_t *)payload;
            tk_app_inject_pointer(app, p->x, p->y, p->buttons);
        } else if (h.msg_type == WSD_EVT_KEY &&
                   h.payload_size == sizeof(wsd_evt_key_t)) {
            const wsd_evt_key_t *k = (const wsd_evt_key_t *)payload;
            tk_app_inject_key(app, k->keycode, k->pressed);
        }
    }
    return -1;
}

/* event translation + routing */

static void route_event(tk_app_t *app, const ui_tk_event_t *ev)
{
    if (app->dialog_open) {
        if (ev->type == UI_TK_EV_POINTER) {
            widget_button_handle_mouse(&app->dialog_ok, ev->x, ev->y,
                                       ev->down, ev->clicked, app);
            if (app->dialog_question)
                widget_button_handle_mouse(&app->dialog_cancel, ev->x, ev->y,
                                           ev->down, ev->clicked, app);
        }
        app->dirty = 1;
        return;
    }
    if (app->root && ui_widget_dispatch(app->root, ev))
        app->dirty = 1;
}

static void translate_pointer(tk_app_t *app, int x, int y, uint32_t buttons)
{
    ui_tk_event_t ev;
    uint32_t changed = buttons ^ app->prev_buttons;

    ev.type = UI_TK_EV_POINTER;
    ev.x = x;
    ev.y = y;
    ev.buttons = buttons;
    ev.down = (buttons & 1u) != 0;
    ev.clicked = ((changed & 1u) != 0) && ((buttons & 1u) != 0);
    ev.keycode = 0;
    ev.pressed = false;
    app->prev_buttons = buttons;
    route_event(app, &ev);
}

static void translate_key(tk_app_t *app, uint32_t keycode, int pressed)
{
    ui_tk_event_t ev;
    ev.type = UI_TK_EV_KEY;
    ev.x = 0;
    ev.y = 0;
    ev.buttons = app->prev_buttons;
    ev.down = false;
    ev.clicked = false;
    ev.keycode = keycode;
    ev.pressed = pressed ? true : false;
    route_event(app, &ev);
}

static void drain_injected(tk_app_t *app)
{
    while (app->inject_count > 0) {
        tk_inject_t r = app->inject[app->inject_head];
        app->inject_head = (app->inject_head + 1) % TK_INJECT_MAX;
        app->inject_count--;
        if (r.is_key)
            translate_key(app, r.keycode, r.pressed);
        else
            translate_pointer(app, r.x, r.y, r.buttons);
    }
}

static void drain_server(tk_app_t *app)
{
    for (;;) {
        vfs_pollfd_t pf;
        wsd_hdr_t h;
        uint8_t payload[64];

        pf.fd = app->surf.conn_fd;
        pf.events = (int16_t)(VFS_POLLIN | VFS_POLLHUP);
        pf.revents = 0;
        poll(&pf, 1, 0);
        if (!(pf.revents & VFS_POLLIN)) {
            if (pf.revents & VFS_POLLHUP)
                app->quit = 1;
            return;
        }
        if (!tk_recv_all(app->surf.conn_fd, &h, (int)sizeof(h)) ||
            h.magic != WSD_MAGIC || h.payload_size > sizeof(payload)) {
            app->quit = 1;
            return;
        }
        if (h.payload_size > 0 &&
            !tk_recv_all(app->surf.conn_fd, payload, (int)h.payload_size)) {
            app->quit = 1;
            return;
        }
        if (h.msg_type == WSD_EVT_POINTER &&
            h.payload_size == sizeof(wsd_evt_pointer_t)) {
            const wsd_evt_pointer_t *p = (const wsd_evt_pointer_t *)payload;
            translate_pointer(app, p->x, p->y, p->buttons);
        } else if (h.msg_type == WSD_EVT_KEY &&
                   h.payload_size == sizeof(wsd_evt_key_t)) {
            const wsd_evt_key_t *k = (const wsd_evt_key_t *)payload;
            translate_key(app, k->keycode, k->pressed);
        } else if (h.msg_type == WSD_EVT_CLOSE) {
            app->quit = 1;
            return;
        }
    }
}

int tk_app_pump(tk_app_t *app)
{
    if (!app || app->quit)
        return 0;
    drain_injected(app);
    drain_server(app);
    timers_fire_due(app);
    if (app->dirty) {
        if (frame_flush(app) != 0)
            app->quit = 1;
    }
    if (app->quit)
        return 0;
    sched_yield();
    return 1;
}

void tk_app_run(tk_app_t *app)
{
    while (tk_app_pump(app)) {
    }
}

static int dialog_run(tk_app_t *app, const char *title, const char *text,
                      int question)
{
    if (!app)
        return 0;
    app->dialog_open = 1;
    app->dialog_question = question;
    app->dialog_result = 0;
    app->dialog_done = 0;
    app->dialog_title = title ? title : "";
    app->dialog_text = text ? text : "";
    dialog_layout(app);
    app->dirty = 1;

    while (!app->dialog_done && !app->quit)
        tk_app_pump(app);

    app->dialog_open = 0;
    app->dirty = 1;
    return app->dialog_result;
}

int tk_dialog_message(tk_app_t *app, const char *title, const char *text)
{
    dialog_run(app, title, text, 0);
    return 1;
}

int tk_dialog_question(tk_app_t *app, const char *title, const char *text)
{
    return dialog_run(app, title, text, 1);
}
