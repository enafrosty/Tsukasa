/*
 * Project Tsukasa — wdemo16: guide-16 toolkit demo + ring-3 acceptance app
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

#include "tsukasa_sdk.h"
#include "../tk/tk_app.h"

#include <stdint.h>
#include <stddef.h>

#define DEMO_W 320u
#define DEMO_H 220u
#define DEMO_BG 0xFF141821u

static int str_eq(const char *a, const char *b)
{
    if (!a || !b) return 0;
    while (*a && *a == *b) { a++; b++; }
    return *a == *b;
}

static tk_app_t g_app;
static ui_box_t g_row;            /* horizontal button row */
static widget_button_t g_save, g_load, g_quit;
static widget_textbox_t g_field;
static char g_field_buf[32];
static ui_widget_t g_root;        /* plain container (whole surface) */

static int g_order_log[8];
static int g_order_n;
static int g_save_clicks_a;
static int g_save_clicks_b;
static int g_changed_count;
static int g_timer_fired;
static int g_dialog_answer = -1;

static void lis_a(ui_widget_t *w, void *ud)
{
    (void)w; (void)ud;
    g_save_clicks_a++;
    if (g_order_n < 8) g_order_log[g_order_n++] = 1;
}

static void lis_b(ui_widget_t *w, void *ud)
{
    (void)w; (void)ud;
    g_save_clicks_b++;
    if (g_order_n < 8) g_order_log[g_order_n++] = 2;
}

static void field_changed(ui_widget_t *w, void *ud)
{
    (void)w; (void)ud;
    g_changed_count++;
}

static void quit_clicked(ui_widget_t *w, void *ud)
{
    (void)w;
    tk_app_quit((tk_app_t *)ud);
}

static void on_timer(tk_app_t *app, void *ud)
{
    (void)app; (void)ud;
    g_timer_fired++;
}

static ui_gradient_t g_grad;
#define GRAD_X 16
#define GRAD_Y 130
#define GRAD_W 200
#define GRAD_H 40

static void paint_custom(tk_app_t *app)
{
    if (app->ctx.draw_gradient)
        app->ctx.draw_gradient(app->ctx.user_data, GRAD_X, GRAD_Y, GRAD_W, GRAD_H, &g_grad);
    if (app->ctx.draw_line)
        app->ctx.draw_line(app->ctx.user_data, GRAD_X, GRAD_Y, GRAD_X + GRAD_W - 1,
                           GRAD_Y + GRAD_H - 1, 0xFFFFFFFFu);
    if (app->ctx.fill_ellipse)
        app->ctx.fill_ellipse(app->ctx.user_data, 260, 150, 24, 16, 0xFFE5A93Du);
}

static void root_draw(ui_widget_t *w, widget_context_t *ctx)
{
    (void)w; (void)ctx;
    paint_custom(&g_app);
}

static int build_ui(void)
{
    if (tk_app_init(&g_app, DEMO_W, DEMO_H, DEMO_BG) != 0)
        return -1;

    ui_widget_base_init(&g_root, UI_WIDGET_PLAIN, 0, 0, (int)DEMO_W, (int)DEMO_H);
    g_root.draw = root_draw;

    ui_box_init(&g_row, UI_HORIZONTAL, 16, 16, 288, 30);
    widget_button_init(&g_save, 0, 0, 70, 26, "Save");
    widget_button_init(&g_load, 0, 0, 70, 26, "Load");
    widget_button_init(&g_quit, 0, 0, 70, 26, "Quit");
    g_save.base.pref_w = 70; g_save.base.pref_h = 26;
    g_load.base.pref_w = 70; g_load.base.pref_h = 26;
    g_quit.base.pref_w = 70; g_quit.base.pref_h = 26;
    ui_box_pack(&g_row, &g_save.base, false, false, 2);
    ui_box_pack(&g_row, &g_load.base, true, true, 2);
    ui_box_pack(&g_row, &g_quit.base, false, false, 2);

    widget_textbox_init(&g_field, 16, 60, 288, 28, g_field_buf, sizeof(g_field_buf));

    ui_widget_add_child(&g_root, &g_row.base);
    ui_widget_add_child(&g_root, &g_field.base);

    ui_gradient_init(&g_grad, true);
    ui_gradient_add_stop(&g_grad, 0, 0xFFCC2222u);
    ui_gradient_add_stop(&g_grad, 100, 0xFF2244CCu);

    ui_signal_connect(&g_quit.base, UI_SIGNAL_CLICKED, quit_clicked, &g_app);
    tk_app_set_root(&g_app, &g_root);
    return 0;
}

static void click_at(int x, int y)
{
    tk_app_inject_pointer(&g_app, x, y, 1);
    tk_app_inject_pointer(&g_app, x, y, 0);
    tk_app_pump(&g_app);
}

static int btn_cx(const widget_button_t *b) { return b->base.x + b->base.w / 2; }
static int btn_cy(const widget_button_t *b) { return b->base.y + b->base.h / 2; }

static void dialog_prober(tk_app_t *app, void *ud)
{
    /* Runs from the timer WHILE the modal dialog pump is active: probe that a click on Save is blocked, answer... */
    (void)ud;
    static int phase = 0;
    if (!app->dialog_open || phase > 0)
        return;
    phase = 1;
    tk_app_inject_pointer(app, btn_cx(&g_save), btn_cy(&g_save), 1);
    tk_app_inject_pointer(app, btn_cx(&g_save), btn_cy(&g_save), 0);
    tk_app_inject_pointer(app, app->dialog_ok.base.x + 10,
                          app->dialog_ok.base.y + 10, 1);
    tk_app_inject_pointer(app, app->dialog_ok.base.x + 10,
                          app->dialog_ok.base.y + 10, 0);
}

static int frame_ok(void);

static int run_selftest(void)
{
    int w_before, w_after;
    uint32_t px, want;

    if (build_ui() != 0)
        return 61;

    /* M2 — box reflow (programmatic; interactive resize is 15-v2): the expander's width must shrink by exactly... */
    w_before = g_load.base.w;
    ui_box_set_geometry(&g_row, 16, 16, 228, 30);
    w_after = g_load.base.w;
    if (w_after != w_before - 60) {
        tk_app_shutdown(&g_app);
        return 62;
    }
    ui_box_set_geometry(&g_row, 16, 16, 288, 30);
    if (g_load.base.w != w_before) {
        tk_app_shutdown(&g_app);
        return 62;
    }

    ui_signal_connect(&g_save.base, UI_SIGNAL_CLICKED, lis_a, 0);
    ui_signal_connect(&g_save.base, UI_SIGNAL_CLICKED, lis_b, 0);
    click_at(btn_cx(&g_save), btn_cy(&g_save));
    if (g_save_clicks_a != 1 || g_save_clicks_b != 1 ||
        g_order_n != 2 || g_order_log[0] != 1 || g_order_log[1] != 2) {
        tk_app_shutdown(&g_app);
        return 63;
    }
    if (ui_signal_disconnect(&g_save.base, UI_SIGNAL_CLICKED, lis_b) != 0) {
        tk_app_shutdown(&g_app);
        return 64;
    }
    click_at(btn_cx(&g_save), btn_cy(&g_save));
    if (g_save_clicks_a != 2 || g_save_clicks_b != 1) {
        tk_app_shutdown(&g_app);
        return 64;
    }

    ui_signal_connect(&g_field.base, UI_SIGNAL_CHANGED, field_changed, 0);
    click_at(g_field.base.x + 10, g_field.base.y + 10);
    tk_app_inject_key(&g_app, 'h', 1);
    tk_app_inject_key(&g_app, 'h', 0);
    tk_app_inject_key(&g_app, 'i', 1);
    tk_app_inject_key(&g_app, 'i', 0);
    tk_app_pump(&g_app);
    if (!(g_field_buf[0] == 'h' && g_field_buf[1] == 'i' && g_field_buf[2] == '\0') ||
        g_changed_count < 2) {
        tk_app_shutdown(&g_app);
        return 65;
    }
    tk_app_inject_key(&g_app, '\b', 1);
    tk_app_inject_key(&g_app, '\b', 0);
    tk_app_pump(&g_app);
    if (!(g_field_buf[0] == 'h' && g_field_buf[1] == '\0')) {
        tk_app_shutdown(&g_app);
        return 65;
    }

    /* M4 — modal dialog blocks the parent. */
    {
        int clicks_before = g_save_clicks_a;
        uint32_t tid = tk_app_set_timer(&g_app, 20, dialog_prober, 0);
        if (!tid) {
            tk_app_shutdown(&g_app);
            return 67;
        }
        g_dialog_answer = tk_dialog_question(&g_app, "Confirm", "Proceed?");
        tk_app_kill_timer(&g_app, tid);
        if (g_dialog_answer != 1 || g_save_clicks_a != clicks_before) {
            tk_app_shutdown(&g_app);
            return 66;
        }
        click_at(btn_cx(&g_save), btn_cy(&g_save));
        if (g_save_clicks_a != clicks_before + 1) {
            tk_app_shutdown(&g_app);
            return 66;
        }
    }

    /* M6 — timer fires repeatedly (>=2). */
    {
        uint32_t tid = tk_app_set_timer(&g_app, 10, on_timer, 0);
        uint64_t t0 = ticks();
        int spins = 0;
        if (!tid) {
            tk_app_shutdown(&g_app);
            return 67;
        }
        while (g_timer_fired < 2 && (ticks() - t0) < 100 && spins < 100000000) {
            tk_app_pump(&g_app);
            spins++;
        }
        tk_app_kill_timer(&g_app, tid);
        if (g_timer_fired < 2) {
            tk_app_shutdown(&g_app);
            return 67;
        }
    }

    tk_app_set_clipboard(&g_app, "tsukasa16");
    if (!str_eq(tk_app_get_clipboard(&g_app), "tsukasa16")) {
        tk_app_shutdown(&g_app);
        return 68;
    }

    /* M5b — painter pixels: render one frame, then assert the gradient midpoint/endpoints against... */
    tk_app_request_redraw(&g_app);
    tk_app_pump(&g_app);

    px = g_app.surf.px[(GRAD_Y + 5) * (int)g_app.surf.w + GRAD_X];
    want = ui_gradient_sample(&g_grad, 0) | 0xFF000000u;
    if (px != want) { tk_app_shutdown(&g_app); return 69; }

    px = g_app.surf.px[(GRAD_Y + 5) * (int)g_app.surf.w + (GRAD_X + GRAD_W - 1)];
    want = ui_gradient_sample(&g_grad, 100) | 0xFF000000u;
    if (px != want) { tk_app_shutdown(&g_app); return 69; }

    px = g_app.surf.px[(GRAD_Y) * (int)g_app.surf.w + GRAD_X];
    if (px != (0xFFFFFFFFu)) { tk_app_shutdown(&g_app); return 69; }

    px = g_app.surf.px[150 * (int)g_app.surf.w + 260];
    if (px != 0xFFE5A93Du) { tk_app_shutdown(&g_app); return 69; }

    tk_app_request_redraw(&g_app);
    if (frame_ok() != 0) { tk_app_shutdown(&g_app); return 70; }

    tk_app_shutdown(&g_app);
    return 42;
}

/* One explicit pump that must succeed (flush + ack). */
static int frame_ok(void)
{
    int before = g_app.quit;
    tk_app_pump(&g_app);
    return (g_app.quit && !before) ? -1 : 0;
}

static int run_interactive(void)
{
    if (build_ui() != 0)
        return 61;
    tk_app_run(&g_app);
    tk_app_shutdown(&g_app);
    return 0;
}

int main(int argc, char **argv)
{
    if (argc < 2)
        return 60;
    if (str_eq(argv[1], "selftest"))
        return run_selftest();
    if (str_eq(argv[1], "run"))
        return run_interactive();
    return 60;
}
