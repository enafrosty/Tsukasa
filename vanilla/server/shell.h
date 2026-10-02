/*
 * Project Tsukasa — Vanilla Desktop Shell and Taskbar Panel Header
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

#ifndef _VANILLA_SHELL_H
#define _VANILLA_SHELL_H

#include <stdint.h>
#include <time.h>
#include "../include/surface.h"
#include "../include/theme.h"

#if defined(_WIN32)
struct tm *gmtime_r(const time_t *timer, struct tm *result);
#endif

#define TASKBAR_HEIGHT       THEME_PX(36)
#define TASKBAR_START_X      THEME_PX(4)
#define TASKBAR_START_W      THEME_PX(64)
#define TASKBAR_START_H      THEME_PX(28)
#define TASKBAR_PILL_START_X THEME_PX(74)
#define TASKBAR_PILL_W       THEME_PX(120)
#define TASKBAR_PILL_H       THEME_PX(28)
#define TASKBAR_PILL_GAP     THEME_PX(4)
#define TASKBAR_CLOCK_W      THEME_PX(72)
#define TASKBAR_CLOCK_H      THEME_PX(28)

#define START_MENU_WIDTH     THEME_PX(180)
#define START_MENU_HEIGHT    THEME_PX(170)
#define START_MENU_ITEM_H    THEME_PX(28)
#define START_MENU_NUM_ITEMS 5

struct vanilla_server;

typedef struct vanilla_shell {
    int    start_menu_open;
    int    selected_idx;
    time_t last_clock_sec;
    char   clock_str[16];
} vanilla_shell_t;

void shell_init(vanilla_shell_t *shell);
void shell_update_clock(vanilla_shell_t *shell, struct vanilla_server *srv);
void shell_render(struct vanilla_server *srv, const vanilla_rect_t *dirty);
void shell_render_start_menu(struct vanilla_server *srv, const vanilla_rect_t *dirty);
int  shell_handle_click(struct vanilla_server *srv, int32_t x, int32_t y, uint32_t button);
int  shell_handle_key(struct vanilla_server *srv, uint16_t code, int pressed);
void shell_invalidate(struct vanilla_server *srv);
void shell_invalidate_start_menu(struct vanilla_server *srv);

#endif /* _VANILLA_SHELL_H */
