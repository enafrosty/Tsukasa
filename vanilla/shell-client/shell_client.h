/*
 * Project Tsukasa — Vanilla Desktop Shell and Launcher Client Header
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

#ifndef _VANILLA_SHELL_CLIENT_H
#define _VANILLA_SHELL_CLIENT_H

#include <stdint.h>
#include <time.h>
#include <sys/types.h>
#include "../include/surface.h"
#include "../include/theme.h"
#include "../include/vanilla.h"
#include "../input/keymap.h"

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

#define LAUNCHER_WIDTH        THEME_PX(440)
#define LAUNCHER_HEIGHT       THEME_PX(280)
#define LAUNCHER_SEARCH_MAX   64
#define LAUNCHER_MAX_APPS     16
#define LAUNCHER_ITEM_HEIGHT  THEME_PX(36)

#define SHELL_MAX_WINDOWS 64

typedef struct {
    uint32_t window_id;
    char     title[VANILLA_TITLE_MAX];
    int      is_mapped;
    int      is_focused;
    int      is_minimized;
    uint32_t flags;
} shell_window_entry_t;

typedef struct {
    shell_window_entry_t windows[SHELL_MAX_WINDOWS];
    int                  count;
} shell_window_list_t;

typedef struct {
    const char *name;
    const char *description;
    const char *exec_path;
    uint32_t    icon_color;
} vanilla_app_entry_t;

typedef struct {
    int app_index;
    int score;
} vanilla_match_result_t;

typedef struct vanilla_launcher {
    int                    visible;
    char                   query[LAUNCHER_SEARCH_MAX];
    int                    query_len;
    int                    selected_idx;
    int                    match_count;
    vanilla_match_result_t matches[LAUNCHER_MAX_APPS];
    vanilla_window_t      *win;
    int                    dirty;
} vanilla_launcher_t;

typedef struct vanilla_shell {
    int              start_menu_open;
    int              selected_idx;
    time_t           last_clock_sec;
    char             clock_str[16];
    vanilla_window_t *start_menu_win;
    int              start_menu_dirty;
} vanilla_shell_t;

typedef struct {
    vanilla_client_t    *client;
    vanilla_window_t    *taskbar_win;
    int32_t              screen_w;
    int32_t              screen_h;
    vanilla_shell_t      shell;
    vanilla_launcher_t   launcher;
    shell_window_list_t  win_list;
    uint16_t             mod_state;
    int                  running;
    int                  taskbar_dirty;
} shell_state_t;

struct vanilla_server;

void shell_init(vanilla_shell_t *shell);
void shell_update_clock(vanilla_shell_t *shell, struct vanilla_server *srv);
void shell_spawn_app(const char *path);

void launcher_init(vanilla_launcher_t *launcher);
int  launcher_fuzzy_match(const char *pattern, const char *target);
void launcher_update_matches(vanilla_launcher_t *launcher);
int  launcher_handle_key_state(vanilla_launcher_t *launcher, uint16_t code, int pressed, uint16_t mod_state);
void shell_handle_event(shell_state_t *st, const vanilla_event_t *ev);

#endif /* _VANILLA_SHELL_CLIENT_H */
