/*
 * Project Tsukasa — Project Vanilla System Settings & About
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

#define SETTINGS_WIDTH   520
#define SETTINGS_HEIGHT  360

typedef struct {
    int          current_tab;
    ui_widget_t *root, *card_sys, *card_app;
} settings_state_t;

static unsigned long read_key_val(const char *fn, const char *key)
{
    FILE *fp = fopen(fn, "r");
    if (!fp) return 0;
    char line[128];
    unsigned long val = 0;
    while (fgets(line, sizeof(line), fp)) {
        if (!strncmp(line, key, strlen(key))) {
            char *p = line + strlen(key);
            while (*p == ' ' || *p == ':') p++;
            val = strtoul(p, NULL, 10);
            break;
        }
    }
    fclose(fp);
    return val;
}

static ui_widget_t *make_card(ui_ctx_t *ctx, const char *title, const char **lines, int n,
                              uint32_t bg, uint32_t bdr, uint32_t acc, uint32_t fg)
{
    ui_widget_t *c = ui_box(ctx, VDIR_COLUMN);
    c->layout_elem->pad_left = c->layout_elem->pad_right = c->layout_elem->pad_top = c->layout_elem->pad_bottom = 12;
    c->layout_elem->gap = 6;
    c->layout_elem->bg_color = bg;
    c->layout_elem->border_color = bdr;
    c->layout_elem->border_width = 1;
    c->layout_elem->corner_radius = 6;
    ui_widget_add_child(c, ui_label(ctx, title, acc));
    for (int i = 0; i < n; i++) ui_widget_add_child(c, ui_label(ctx, lines[i], fg));
    return c;
}

static void update_active_tab(settings_state_t *st, int tab)
{
    st->current_tab = tab;
    st->card_sys->layout_elem->h_mode = (tab == 0) ? VSIZE_GROW : VSIZE_FIXED;
    st->card_sys->layout_elem->h_px = 0;
    st->card_sys->layout_elem->clip_children = (tab != 0);
    st->card_app->layout_elem->h_mode = (tab == 1) ? VSIZE_GROW : VSIZE_FIXED;
    st->card_app->layout_elem->h_px = 0;
    st->card_app->layout_elem->clip_children = (tab != 1);
    ui_widget_invalidate(st->root);
}

static void on_tab_event(ui_widget_t *w, const ui_event_t *ev, void *ud)
{
    (void)w;
    if (ev->type == UI_EVENT_VALUE_CHANGED) update_active_tab((settings_state_t *)ud, ev->toggle.state);
}

int main(int argc, char **argv)
{
    (void)argc; (void)argv;
    settings_state_t st;
    memset(&st, 0, sizeof(st));

    vanilla_client_t *client = vanilla_connect(NULL);
    if (!client) return 1;
    vanilla_window_t *win = vanilla_create_window(client, "Settings", 180, 110, SETTINGS_WIDTH, SETTINGS_HEIGHT, WINDOW_FLAG_NONE);
    if (!win) { vanilla_disconnect(client); return 1; }
    vanilla_map_window(win);

    static uint8_t ui_arena[128 * 1024];
    ui_ctx_t *ctx = ui_ctx_init(ui_arena, sizeof(ui_arena), NULL);
    if (!ctx) { vanilla_destroy_window(win); vanilla_disconnect(client); return 1; }

    uint32_t bg = g_theme ? g_theme->bg_base : 0xFF2E3440u, elev = g_theme ? g_theme->bg_elevated : 0xFF3B4252u;
    uint32_t bdr = g_theme ? g_theme->border : 0xFF4C566Au, acc = g_theme ? g_theme->accent : 0xFF88C0D0u;
    uint32_t fg = g_theme ? g_theme->fg_primary : 0xFFECEFF4u;

    st.root = ui_box(ctx, VDIR_COLUMN);
    st.root->layout_elem->bg_color = bg;

    static const char *tabs[] = { "System Info", "Appearance" };
    ui_widget_t *tbar = ui_tab_bar(ctx, tabs, 2, on_tab_event, &st);
    ui_widget_add_child(st.root, tbar);

    ui_widget_t *content = ui_box(ctx, VDIR_COLUMN);
    content->layout_elem->pad_left = content->layout_elem->pad_right = content->layout_elem->pad_top = content->layout_elem->pad_bottom = 16;
    content->layout_elem->gap = 12;
    ui_widget_add_child(st.root, content);

    char mem[80];
    unsigned long tot = read_key_val("/sys/memory", "pmm_total_pages"), usd = read_key_val("/sys/memory", "pmm_used_pages"), fre = read_key_val("/sys/memory", "pmm_free_pages");
    if (tot > 0) snprintf(mem, sizeof(mem), "Physical Memory: %lu MB Total, %lu MB Used, %lu MB Free", (tot * 4UL) / 1024, (usd * 4UL) / 1024, (fre * 4UL) / 1024);
    else strcpy(mem, "Physical Memory: 256 MB Total (PMM Monitored)");

    const char *sys_l[] = { "Distribution:  Project Tsukasa OS (x86_64 Long Mode)", "Architecture:  x86_64 (SMP, SSE2, Ring 3 Isolated)", "Display Server: Project Vanilla 1.0 (Zero-Copy SHM)", "Kernel ABI:    SPEC-C01 Linux-Compatible Fast Syscalls", mem, "Graphics Output: /dev/fb0 (32 bpp ARGB32 VESA/GOP)", "Input Device:    /dev/input/events (Unified Evdev)", "Network NIC:     Intel 8254x / VirtIO-Net Gigabit" };
    st.card_sys = make_card(ctx, "Operating System & Hardware", sys_l, 8, elev, bdr, acc, fg);
    ui_widget_add_child(content, st.card_sys);

    const char *app_l[] = { "Current Theme: Nord Polar Night (Dark Mode)", "Wallpaper:     /assets/wallpaper.bmp", "Font Family:   Roboto Medium / Bitmap 8x8" };
    st.card_app = make_card(ctx, "Desktop Theme", app_l, 3, elev, bdr, acc, fg);
    ui_widget_add_child(content, st.card_app);

    update_active_tab(&st, 0);
    ui_widget_set_focus(ctx, tbar);

    int running = 1;
    while (running) {
        vanilla_event_t ev;
        while (vanilla_poll_event(client, &ev) > 0) {
            if (ev.type == VANILLA_EVENT_CLOSE_REQ) { running = 0; break; }
            if (ev.type == VANILLA_EVENT_INPUT)
                ui_handle_event(ctx, st.root, &ev.input);
        }
        ui_render(ctx, st.root, &win->surface, NULL);
        vanilla_present(win, NULL);
        usleep(20000);
    }
    vanilla_destroy_window(win);
    vanilla_disconnect(client);
    return 0;
}
