/*
 * Project Tsukasa — Vanilla Desktop Shell and Taskbar Panel Implementation
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

#include "shell.h"
#include "server.h"
#include "blitter.h"
#include "font.h"
#include "launcher.h"

#include <stdio.h>
#include <string.h>
#include <unistd.h>

typedef struct {
    const char *title;
    const char *path;
    const char *badge;
    uint32_t    badge_color;
} start_menu_item_t;

static const start_menu_item_t g_start_menu_items[START_MENU_NUM_ITEMS] = {
    { "Terminal",     "/bin/terminal.elf", ">_", 0xFF5E81AC },
    { "File Manager", "/bin/filemgr.elf",  "FM", 0xFFD08770 },
    { "Notepad",      "/bin/notepad.elf",  "NP", 0xFFA3BE8C },
    { "Calculator",   "/bin/calc.elf",     "+-", 0xFFEBCB8B },
    { "Task Manager", "/bin/taskmgr.elf",  "TM", 0xFF81A1C1 },
};

static void shell_spawn_app(const char *path)
{
    char current_path[256];
    strncpy(current_path, path, sizeof(current_path) - 1);
    current_path[sizeof(current_path) - 1] = '\0';

    char *argv[] = { current_path, NULL };
    pid_t pid = spawn(current_path, argv, NULL);

    if (pid <= 0 && strncmp(current_path, "/bin/", 5) == 0) {
        char fat_path[320];
        snprintf(fat_path, sizeof(fat_path), "/fat12/%s", current_path + 5);
        char *fargv[] = { fat_path, NULL };
        pid = spawn(fat_path, fargv, NULL);
    }

    if (pid <= 0) {
        char upath[256];
        size_t len = strlen(current_path);
        size_t i;
        for (i = 0; i < len && i < sizeof(upath) - 1; i++) {
            char c = current_path[i];
            upath[i] = (c >= 'a' && c <= 'z') ? (c - 32) : c;
        }
        upath[i] = '\0';
        char *uargv[] = { upath, NULL };
        pid = spawn(upath, uargv, NULL);

        if (pid <= 0 && strncmp(upath, "/BIN/", 5) == 0) {
            char fat_upath[320];
            snprintf(fat_upath, sizeof(fat_upath), "/fat12/%s", upath + 5);
            char *fuargv[] = { fat_upath, NULL };
            pid = spawn(fat_upath, fuargv, NULL);
        }
    }
    (void)pid;
}

void shell_init(vanilla_shell_t *shell)
{
    if (!shell)
        return;

    shell->start_menu_open = 0;
    shell->last_clock_sec = 0;
    strncpy(shell->clock_str, "12:00", sizeof(shell->clock_str) - 1);
    shell->clock_str[sizeof(shell->clock_str) - 1] = '\0';
}

void shell_update_clock(vanilla_shell_t *shell, vanilla_server_t *srv)
{
    if (!shell)
        return;

    time_t now = time(NULL);
    if (now != shell->last_clock_sec && now > 0) {
        struct tm tm;
        gmtime_r(&now, &tm);

        char buf[16];
        snprintf(buf, sizeof(buf), "%02d:%02d", tm.tm_hour, tm.tm_min);

        if (strncmp(buf, shell->clock_str, sizeof(buf)) != 0) {
            strncpy(shell->clock_str, buf, sizeof(shell->clock_str) - 1);
            shell->clock_str[sizeof(shell->clock_str) - 1] = '\0';

            if (srv) {
                int32_t screen_w = (int32_t)srv->compositor.width;
                int32_t screen_h = (int32_t)srv->compositor.height;
                vanilla_rect_t clock_damage = {
                    screen_w - TASKBAR_CLOCK_W - 4,
                    screen_h - TASKBAR_HEIGHT + 4,
                    TASKBAR_CLOCK_W,
                    TASKBAR_CLOCK_H
                };
                compositor_add_damage(&srv->compositor, &clock_damage);
            }
        }
        shell->last_clock_sec = now;
    }
}

void shell_invalidate(vanilla_server_t *srv)
{
    if (!srv)
        return;

    int32_t screen_w = (int32_t)srv->compositor.width;
    int32_t screen_h = (int32_t)srv->compositor.height;
    vanilla_rect_t tb_rect = { 0, screen_h - TASKBAR_HEIGHT, screen_w, TASKBAR_HEIGHT };
    compositor_add_damage(&srv->compositor, &tb_rect);
}

void shell_invalidate_start_menu(vanilla_server_t *srv)
{
    if (!srv)
        return;
    int32_t screen_h = (int32_t)srv->compositor.height;
    vanilla_rect_t r = { 0, screen_h - TASKBAR_HEIGHT - START_MENU_HEIGHT, START_MENU_WIDTH, START_MENU_HEIGHT };
    compositor_add_damage(&srv->compositor, &r);
}

void shell_render_start_menu(vanilla_server_t *srv, const vanilla_rect_t *dirty)
{
    if (!srv || !dirty || !srv->shell.start_menu_open)
        return;

    vanilla_compositor_t *comp = &srv->compositor;
    int32_t screen_h = (int32_t)comp->height;

    int32_t sm_x = 0;
    int32_t sm_y = screen_h - TASKBAR_HEIGHT - START_MENU_HEIGHT;
    int32_t sm_w = START_MENU_WIDTH;
    int32_t sm_h = START_MENU_HEIGHT;

    vanilla_rect_t panel_rect = { sm_x, sm_y, sm_w, sm_h };
    vanilla_rect_t vis_panel;
    if (!vanilla_rect_intersect(&panel_rect, dirty, &vis_panel))
        return;

    /* Panel background */
    blt_fill_rect(comp->backbuffer, comp->pitch_px, &vis_panel, 0xFF2E3440);

    /* 1px top border and right border */
    vanilla_rect_t top_border = { sm_x, sm_y, sm_w, 1 };
    vanilla_rect_t vis_top;
    if (vanilla_rect_intersect(&top_border, dirty, &vis_top))
        blt_fill_rect(comp->backbuffer, comp->pitch_px, &vis_top, 0xFF4C566A);

    vanilla_rect_t right_border = { sm_x + sm_w - 1, sm_y, 1, sm_h };
    vanilla_rect_t vis_right;
    if (vanilla_rect_intersect(&right_border, dirty, &vis_right))
        blt_fill_rect(comp->backbuffer, comp->pitch_px, &vis_right, 0xFF4C566A);

    /* Render Start Menu items */
    for (int i = 0; i < START_MENU_NUM_ITEMS; i++) {
        const start_menu_item_t *item = &g_start_menu_items[i];
        int32_t item_y = sm_y + 12 + i * START_MENU_ITEM_H;

        vanilla_rect_t item_rect = { sm_x + 6, item_y, sm_w - 12, START_MENU_ITEM_H - 4 };
        vanilla_rect_t vis_item;
        if (!vanilla_rect_intersect(&item_rect, dirty, &vis_item))
            continue;

        /* Item badge / icon */
        vanilla_rect_t badge_rect = { item_rect.x + 4, item_rect.y + 3, 18, 18 };
        vanilla_rect_t vis_badge;
        if (vanilla_rect_intersect(&badge_rect, dirty, &vis_badge)) {
            blt_fill_rect(comp->backbuffer, comp->pitch_px, &vis_badge, item->badge_color);
            if (comp->font.info) {
                font_draw_text(comp->backbuffer, comp->pitch_px, &vis_badge, &comp->font,
                               item->badge, badge_rect.x + 2, badge_rect.y + 4, 10, 0xFFECEFF4);
            }
        }

        /* Item title text */
        if (comp->font.info) {
            font_draw_text(comp->backbuffer, comp->pitch_px, &vis_item, &comp->font,
                           item->title, item_rect.x + 28, item_rect.y + 6, 12, 0xFFECEFF4);
        }
    }
}

void shell_render(vanilla_server_t *srv, const vanilla_rect_t *dirty)
{
    if (!srv || !dirty)
        return;

    vanilla_compositor_t *comp = &srv->compositor;
    int32_t screen_w = (int32_t)comp->width;
    int32_t screen_h = (int32_t)comp->height;

    vanilla_rect_t tb_rect = { 0, screen_h - TASKBAR_HEIGHT, screen_w, TASKBAR_HEIGHT };
    vanilla_rect_t vis_tb;
    if (vanilla_rect_intersect(&tb_rect, dirty, &vis_tb)) {
        /* Taskbar panel background: Nord Polar Night */
        blt_fill_rect(comp->backbuffer, comp->pitch_px, &vis_tb, 0xFF242933);

        /* 1px top border line */
        vanilla_rect_t border_rect = { 0, screen_h - TASKBAR_HEIGHT, screen_w, 1 };
        vanilla_rect_t vis_border;
        if (vanilla_rect_intersect(&border_rect, dirty, &vis_border))
            blt_fill_rect(comp->backbuffer, comp->pitch_px, &vis_border, 0xFF4C566A);

        /* Start button */
        vanilla_rect_t start_rect = { TASKBAR_START_X, screen_h - TASKBAR_HEIGHT + 4, TASKBAR_START_W, TASKBAR_START_H };
        vanilla_rect_t vis_start;
        if (vanilla_rect_intersect(&start_rect, dirty, &vis_start)) {
            uint32_t btn_color = srv->shell.start_menu_open ? 0xFF81A1C1 : 0xFF5E81AC;
            blt_fill_rect(comp->backbuffer, comp->pitch_px, &vis_start, btn_color);
            if (comp->font.info) {
                font_draw_text(comp->backbuffer, comp->pitch_px, &vis_start, &comp->font,
                               "Start", start_rect.x + 14, start_rect.y + 7, 13, 0xFFECEFF4);
            }
        }

        /* Window pills */
        int pill_idx = 0;
        for (int i = 0; i < VANILLA_MAX_WINDOWS; i++) {
            vanilla_server_window_t *win = &srv->windows[i];
            if (!win->in_use || (win->flags & WINDOW_FLAG_BORDERLESS))
                continue;

            int32_t px = TASKBAR_PILL_START_X + pill_idx * (TASKBAR_PILL_W + TASKBAR_PILL_GAP);
            if (px + TASKBAR_PILL_W > screen_w - TASKBAR_CLOCK_W - 8)
                break;

            vanilla_rect_t pill_rect = { px, screen_h - TASKBAR_HEIGHT + 4, TASKBAR_PILL_W, TASKBAR_PILL_H };
            vanilla_rect_t vis_pill;
            if (vanilla_rect_intersect(&pill_rect, dirty, &vis_pill)) {
                uint32_t bg = win->is_focused ? 0xFF4C566A : (win->is_mapped ? 0xFF2E3440 : 0xFF21252B);
                blt_fill_rect(comp->backbuffer, comp->pitch_px, &vis_pill, bg);

                if (win->is_focused) {
                    vanilla_rect_t ind = { pill_rect.x + 2, pill_rect.y + pill_rect.h - 2, pill_rect.w - 4, 2 };
                    vanilla_rect_t vis_ind;
                    if (vanilla_rect_intersect(&ind, dirty, &vis_ind))
                        blt_fill_rect(comp->backbuffer, comp->pitch_px, &vis_ind, 0xFF88C0D0);
                }

                if (comp->font.info) {
                    const char *title = win->title[0] ? win->title : "App";
                    uint32_t fg = win->is_focused ? 0xFFECEFF4 : (win->is_mapped ? 0xFFD8DEE9 : 0xFF7B88A1);
                    font_draw_text(comp->backbuffer, comp->pitch_px, &vis_pill, &comp->font,
                                   title, pill_rect.x + 8, pill_rect.y + 7, 12, fg);
                }
            }
            pill_idx++;
        }

        /* System Tray / Digital Clock */
        vanilla_rect_t clock_rect = { screen_w - TASKBAR_CLOCK_W - 4, screen_h - TASKBAR_HEIGHT + 4, TASKBAR_CLOCK_W, TASKBAR_CLOCK_H };
        vanilla_rect_t vis_clock;
        if (vanilla_rect_intersect(&clock_rect, dirty, &vis_clock)) {
            blt_fill_rect(comp->backbuffer, comp->pitch_px, &vis_clock, 0xFF2E3440);
            if (comp->font.info) {
                font_draw_text(comp->backbuffer, comp->pitch_px, &vis_clock, &comp->font,
                               srv->shell.clock_str, clock_rect.x + 16, clock_rect.y + 7, 12, 0xFFECEFF4);
            }
        }
    }

    /* Render Start Menu popup panel if open */
    if (srv->shell.start_menu_open)
        shell_render_start_menu(srv, dirty);
}

int shell_handle_click(vanilla_server_t *srv, int32_t x, int32_t y, uint32_t button)
{
    if (!srv || button != BTN_LEFT)
        return 0;

    int32_t screen_w = (int32_t)srv->compositor.width;
    int32_t screen_h = (int32_t)srv->compositor.height;
    int32_t sm_x = 0;
    int32_t sm_y = screen_h - TASKBAR_HEIGHT - START_MENU_HEIGHT;

    /* Start button on taskbar */
    if (x >= TASKBAR_START_X && x < TASKBAR_START_X + TASKBAR_START_W &&
        y >= screen_h - TASKBAR_HEIGHT + 4 && y < screen_h - TASKBAR_HEIGHT + 4 + TASKBAR_START_H) {
        srv->shell.start_menu_open = !srv->shell.start_menu_open;
        shell_invalidate_start_menu(srv);
        shell_invalidate(srv);
        return 1;
    }

    /* Start Menu popup click hit-testing */
    if (srv->shell.start_menu_open) {
        if (x >= sm_x && x < sm_x + START_MENU_WIDTH &&
            y >= sm_y && y < sm_y + START_MENU_HEIGHT) {
            int rel_y = y - (sm_y + 12);
            if (rel_y >= 0) {
                int item_idx = rel_y / START_MENU_ITEM_H;
                if (item_idx >= 0 && item_idx < START_MENU_NUM_ITEMS) {
                    shell_spawn_app(g_start_menu_items[item_idx].path);
                    srv->shell.start_menu_open = 0;
                    shell_invalidate_start_menu(srv);
                    shell_invalidate(srv);
                    return 1;
                }
            }
            return 1;
        }
    }

    /* Window pills on taskbar */
    if (y >= screen_h - TASKBAR_HEIGHT) {
        if (srv->shell.start_menu_open) {
            srv->shell.start_menu_open = 0;
            shell_invalidate_start_menu(srv);
        }

        int pill_idx = 0;
        for (int i = 0; i < VANILLA_MAX_WINDOWS; i++) {
            vanilla_server_window_t *win = &srv->windows[i];
            if (!win->in_use || (win->flags & WINDOW_FLAG_BORDERLESS))
                continue;

            int32_t px = TASKBAR_PILL_START_X + pill_idx * (TASKBAR_PILL_W + TASKBAR_PILL_GAP);
            if (px + TASKBAR_PILL_W > screen_w - TASKBAR_CLOCK_W - 8)
                break;

            if (x >= px && x < px + TASKBAR_PILL_W &&
                y >= screen_h - TASKBAR_HEIGHT + 4 && y < screen_h - TASKBAR_HEIGHT + 4 + TASKBAR_PILL_H) {
                if (!win->is_mapped) {
                    win->is_mapped = 1;
                    vanilla_server_focus_window(srv, win->window_id);
                    wm_raise_window(srv, win->window_id);
                } else if (win->is_focused) {
                    win->is_mapped = 0;
                    win->is_focused = 0;
                    if (srv->focused_window_id == win->window_id)
                        srv->focused_window_id = 0;
                    wm_invalidate_window(srv, win);
                } else {
                    vanilla_server_focus_window(srv, win->window_id);
                    wm_raise_window(srv, win->window_id);
                }
                shell_invalidate(srv);
                return 1;
            }
            pill_idx++;
        }

        shell_invalidate(srv);
        return 1;
    }

    return 0;
}
