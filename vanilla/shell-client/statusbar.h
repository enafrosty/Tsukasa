/*
 * Project Tsukasa — Vanilla System Status Bar Header
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

#ifndef _VANILLA_STATUSBAR_H
#define _VANILLA_STATUSBAR_H

#include <stdint.h>
#include <time.h>
#include "../include/surface.h"
#include "../include/vanilla.h"
#include "../include/theme.h"
#include "../libvanilla/icon.h"

#define STATUSBAR_HEIGHT STATUSBAR_HEIGHT_PX

#define SYSTEM_MENU_WIDTH     THEME_PX(188)
#define SYSTEM_MENU_ITEM_H    THEME_PX(28)
#define SYSTEM_MENU_SEP_AFTER 2
#define SYSTEM_MENU_SEP_H     THEME_PX(9)
#define SYSTEM_MENU_PAD_Y     THEME_PX(8)
#define SYSTEM_MENU_NUM_ITEMS 5
#define SYSTEM_MENU_HEIGHT    (SYSTEM_MENU_PAD_Y * 2 + SYSTEM_MENU_NUM_ITEMS * SYSTEM_MENU_ITEM_H + SYSTEM_MENU_SEP_H)

typedef struct {
    vanilla_window_t *win;
    int32_t           height;
    char              active_app_title[64];
    char              breadcrumb[96];
    uint32_t          cpu_usage_pct;
    uint32_t          ram_used_mb;
    uint32_t          ram_total_mb;
    uint32_t          volume_pct;
    char              clock_str[16];
    time_t            last_telemetry_sec;
    uint64_t          prev_cpu_total;
    uint64_t          prev_cpu_active;
    int               has_prev_cpu;
    int               menu_open;
    int               menu_selected_idx;
    vanilla_window_t *menu_win;
    icon_t           *emblem_icon;
    int               dirty;
    int               menu_dirty;
} statusbar_state_t;

struct shell_state;

void statusbar_init(statusbar_state_t *sb);
void statusbar_free_icons(statusbar_state_t *sb);
void statusbar_update_telemetry(statusbar_state_t *sb);
void statusbar_render(statusbar_state_t *sb, int32_t screen_w);
int  statusbar_handle_mouse_click(statusbar_state_t *sb, int32_t cx, int32_t cy, struct shell_state *st);
void statusbar_open_menu(statusbar_state_t *sb, struct shell_state *st);
void statusbar_close_menu(statusbar_state_t *sb);
void statusbar_toggle_menu(statusbar_state_t *sb, struct shell_state *st);
void statusbar_render_menu(statusbar_state_t *sb);
int  statusbar_handle_menu_click(statusbar_state_t *sb, int32_t cx, int32_t cy, struct shell_state *st);
void statusbar_set_active_title(statusbar_state_t *sb, const char *title);

#endif /* _VANILLA_STATUSBAR_H */
