/*
 * Project Tsukasa — Vanilla Display Server Host Emulator Main Entry Point
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

#include "sdl_backend.h"
#include "host_compat.h"
#include "../server/server.h"
#include "../server/blitter.h"
#include "../server/font.h"
#include "../server/shell.h"
#include "../server/launcher.h"

#include <stdio.h>
#include <string.h>

#define SCREEN_WIDTH  1024
#define SCREEN_HEIGHT 768

static void draw_demo_window1(vanilla_server_t *srv)
{
    vanilla_server_window_t *w = &srv->windows[0];
    if (!w->in_use || !w->surface.pixels)
        return;

    uint32_t pitch_px = w->surface.pitch / sizeof(uint32_t);
    vanilla_rect_t full = { 0, 0, (int32_t)w->surface.width, (int32_t)w->surface.height };
    blt_fill_rect(w->surface.pixels, pitch_px, &full, 0xFF2E3440);

    /* Menu bar */
    vanilla_rect_t mbar = { 0, 0, (int32_t)w->surface.width, 24 };
    blt_fill_rect(w->surface.pixels, pitch_px, &mbar, 0xFF3B4252);

    /* Status bar */
    vanilla_rect_t sbar = { 0, (int32_t)w->surface.height - 22, (int32_t)w->surface.width, 22 };
    blt_fill_rect(w->surface.pixels, pitch_px, &sbar, 0xFF3B4252);

    vanilla_font_t *font = &srv->compositor.font;
    if (font->info) {
        font_draw_text(w->surface.pixels, pitch_px, &full, font,
                       "File   Edit   View   Terminal   Help", 10, 5, 12, 0xFFD8DEE9);

        font_draw_text(w->surface.pixels, pitch_px, &full, font,
                       "/* Project Tsukasa - Project Vanilla */", 16, 36, 13, 0xFF81A1C1);
        font_draw_text(w->surface.pixels, pitch_px, &full, font,
                       "#include <stdio.h>", 16, 56, 13, 0xFFB48EAD);
        font_draw_text(w->surface.pixels, pitch_px, &full, font,
                       "#include <vanilla/surface.h>", 16, 76, 13, 0xFFB48EAD);
        font_draw_text(w->surface.pixels, pitch_px, &full, font,
                       "int main(int argc, char *argv[]) {", 16, 106, 13, 0xFF88C0D0);
        font_draw_text(w->surface.pixels, pitch_px, &full, font,
                       "    printf(\"Welcome to Project Tsukasa!\\n\");", 16, 126, 13, 0xFFA3BE8C);
        font_draw_text(w->surface.pixels, pitch_px, &full, font,
                       "    return 0;", 16, 146, 13, 0xFFEBCB8B);
        font_draw_text(w->surface.pixels, pitch_px, &full, font,
                       "}", 16, 166, 13, 0xFF88C0D0);

        font_draw_text(w->surface.pixels, pitch_px, &full, font,
                       "Ln 7, Col 2  |  UTF-8  |  C Source", 10, (int32_t)w->surface.height - 18, 11, 0xFF98A2B3);
    }
}

static void setup_demo_window1(vanilla_server_t *srv)
{
    vanilla_server_window_t *w = &srv->windows[0];
    w->in_use = 1;
    w->window_id = 1;
    w->client_fd = -1;
    w->x = 80;
    w->y = 80;
    w->width = 440;
    w->height = 320;
    w->flags = WINDOW_FLAG_RESIZABLE;
    w->z_index = 1;
    w->layer = LAYER_NORMAL;
    w->is_mapped = 1;
    w->is_focused = 0;
    w->is_snapped = SNAP_NONE;
    w->restore_x = 80;
    w->restore_y = 80;
    w->restore_w = 440;
    w->restore_h = 320;
    strncpy(w->title, "Text Editor - main.c", sizeof(w->title) - 1);

    surface_create_shm(&w->surface, w->width, w->height);
    draw_demo_window1(srv);
}

static void draw_demo_window2(vanilla_server_t *srv)
{
    vanilla_server_window_t *w = &srv->windows[1];
    if (!w->in_use || !w->surface.pixels)
        return;

    uint32_t pitch_px = w->surface.pitch / sizeof(uint32_t);
    vanilla_rect_t full = { 0, 0, (int32_t)w->surface.width, (int32_t)w->surface.height };
    blt_fill_rect(w->surface.pixels, pitch_px, &full, 0xFF2E3440);

    /* Section header bar */
    vanilla_rect_t hdr = { 0, 0, (int32_t)w->surface.width, 28 };
    blt_fill_rect(w->surface.pixels, pitch_px, &hdr, 0xFF3B4252);

    int32_t bar_w = (int32_t)w->surface.width - 32;
    if (bar_w < 50) bar_w = 50;

    /* CPU Progress Bar background & fill */
    vanilla_rect_t cpu_bg = { 16, 60, bar_w, 16 };
    blt_fill_rect(w->surface.pixels, pitch_px, &cpu_bg, 0xFF4C566A);
    vanilla_rect_t cpu_fill = { 16, 60, (bar_w * 24) / 100, 16 };
    blt_fill_rect(w->surface.pixels, pitch_px, &cpu_fill, 0xFFA3BE8C);

    /* Memory Progress Bar background & fill */
    vanilla_rect_t mem_bg = { 16, 108, bar_w, 16 };
    blt_fill_rect(w->surface.pixels, pitch_px, &mem_bg, 0xFF4C566A);
    vanilla_rect_t mem_fill = { 16, 108, (bar_w * 38) / 100, 16 };
    blt_fill_rect(w->surface.pixels, pitch_px, &mem_fill, 0xFF88C0D0);

    /* Process table header background */
    vanilla_rect_t tbl_hdr = { 16, 140, bar_w, 22 };
    blt_fill_rect(w->surface.pixels, pitch_px, &tbl_hdr, 0xFF3B4252);

    vanilla_font_t *font = &srv->compositor.font;
    if (font->info) {
        font_draw_text(w->surface.pixels, pitch_px, &full, font,
                       "Performance & Processes", 12, 6, 13, 0xFFECEFF4);

        font_draw_text(w->surface.pixels, pitch_px, &full, font,
                       "CPU Utilization: 24%", 16, 42, 12, 0xFFECEFF4);

        font_draw_text(w->surface.pixels, pitch_px, &full, font,
                       "Physical Memory: 97 MB / 256 MB (38%)", 16, 90, 12, 0xFFECEFF4);

        font_draw_text(w->surface.pixels, pitch_px, &full, font,
                       "PID     Process Name          CPU%       Memory", 22, 144, 11, 0xFFECEFF4);

        font_draw_text(w->surface.pixels, pitch_px, &full, font,
                       "  1     init                  0.1%        1.2 MB", 22, 172, 12, 0xFFD8DEE9);
        font_draw_text(w->surface.pixels, pitch_px, &full, font,
                       "  2     vanilla_srv           4.2%       18.5 MB", 22, 196, 12, 0xFFD8DEE9);
        font_draw_text(w->surface.pixels, pitch_px, &full, font,
                       "  3     text_editor           1.4%        8.2 MB", 22, 220, 12, 0xFFD8DEE9);
        font_draw_text(w->surface.pixels, pitch_px, &full, font,
                       "  4     sysmon                2.6%        6.4 MB", 22, 244, 12, 0xFFD8DEE9);
        font_draw_text(w->surface.pixels, pitch_px, &full, font,
                       "  5     tsh (terminal)        0.0%        2.8 MB", 22, 268, 12, 0xFFD8DEE9);
    }
}

static void setup_demo_window2(vanilla_server_t *srv)
{
    vanilla_server_window_t *w = &srv->windows[1];
    w->in_use = 1;
    w->window_id = 2;
    w->client_fd = -1;
    w->x = 420;
    w->y = 140;
    w->width = 460;
    w->height = 340;
    w->flags = WINDOW_FLAG_RESIZABLE;
    w->z_index = 2;
    w->layer = LAYER_NORMAL;
    w->is_mapped = 1;
    w->is_focused = 1;
    w->is_snapped = SNAP_NONE;
    w->restore_x = 420;
    w->restore_y = 140;
    w->restore_w = 460;
    w->restore_h = 340;
    strncpy(w->title, "System Monitor", sizeof(w->title) - 1);

    surface_create_shm(&w->surface, w->width, w->height);
    draw_demo_window2(srv);
}

int main(int argc, char *argv[])
{
    (void)argc;
    (void)argv;

    printf("============================================================\n");
    printf("  Project Tsukasa - Vanilla Display Server Host Emulator\n");
    printf("============================================================\n");
    printf("[Host Emulator] Initializing Vanilla Display Server...\n");

    vanilla_server_t srv;
    memset(&srv, 0, sizeof(srv));

    if (compositor_init_offscreen(&srv.compositor, SCREEN_WIDTH, SCREEN_HEIGHT) < 0) {
        printf("[Host Emulator] Fatal: failed to initialize offscreen compositor.\n");
        return 1;
    }

    srv.cursor_x = SCREEN_WIDTH / 2;
    srv.cursor_y = SCREEN_HEIGHT / 2;
    srv.running = 1;
    srv.focused_window_id = 2;
    srv.next_window_id = 3;
    srv.next_z_index = 3;

    shell_init(&srv.shell);
    launcher_init(&srv.launcher);

    vanilla_sdl_backend_t backend;
    if (sdl_backend_init(&backend, SCREEN_WIDTH, SCREEN_HEIGHT,
                         "Project Vanilla Display Server - Host Emulator") < 0) {
        printf("[Host Emulator] Fatal: failed to initialize SDL2 backend.\n");
        compositor_destroy(&srv.compositor);
        return 1;
    }

    printf("[Host Emulator] SDL2 window ready (1024x768). Spawning sample windows...\n");
    setup_demo_window1(&srv);
    setup_demo_window2(&srv);

    printf("[Host Emulator] Prototyping controls:\n");
    printf("  - Mouse: Left click to focus/drag windows, use titlebar [X] [] [_]\n");
    printf("  - Snapping: Drag window to top to maximize, drag to left/right for 50%% split\n");
    printf("  - Taskbar: Click Start for launcher, click window pills to minimize/restore\n");
    printf("  - Hotkey: Press Alt + Space to open Quick Launcher and search apps\n");
    fflush(stdout);

    /* Initial full-frame render */
    compositor_damage_all(&srv.compositor);
    int init_count = srv.compositor.dirty_count;
    vanilla_rect_t init_rects[MAX_DIRTY_RECTS];
    memcpy(init_rects, srv.compositor.dirty_rects, init_count * sizeof(vanilla_rect_t));
    compositor_render_frame(&srv);
    sdl_backend_present(&backend, srv.compositor.backbuffer, init_rects, init_count);

    /* Main prototyping event and frame loop */
    while (backend.running && srv.running) {
        sdl_backend_poll_events(&backend, &srv);
        shell_update_clock(&srv.shell, &srv);

        /* Adapt mock surfaces if window dimensions changed (e.g. snapped or maximized) */
        for (int i = 0; i < 2; i++) {
            vanilla_server_window_t *w = &srv.windows[i];
            if (w->in_use && (w->width != w->surface.width || w->height != w->surface.height)) {
                surface_destroy(&w->surface);
                surface_create_shm(&w->surface, w->width, w->height);
                if (i == 0)
                    draw_demo_window1(&srv);
                else
                    draw_demo_window2(&srv);
                wm_invalidate_window(&srv, w);
            }
        }

        if (srv.compositor.dirty_count > 0) {
            int dcount = srv.compositor.dirty_count;
            vanilla_rect_t drects[MAX_DIRTY_RECTS];
            memcpy(drects, srv.compositor.dirty_rects, dcount * sizeof(vanilla_rect_t));

            compositor_render_frame(&srv);
            sdl_backend_present(&backend, srv.compositor.backbuffer, drects, dcount);
        }

        SDL_Delay(16); /* Throttles to ~60 FPS */
    }

    printf("[Host Emulator] Shutting down...\n");
    surface_destroy(&srv.windows[0].surface);
    surface_destroy(&srv.windows[1].surface);
    sdl_backend_destroy(&backend);
    compositor_destroy(&srv.compositor);

    printf("[Host Emulator] Clean exit.\n");
    return 0;
}
