/*
 * Project Tsukasa — Vanilla Floating Aurora Dock Header
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

#ifndef _VANILLA_DOCK_H
#define _VANILLA_DOCK_H

#include <stdint.h>
#include <stddef.h>
#include "../include/surface.h"
#include "../include/vanilla.h"
#include "../include/theme.h"
#include "../libvanilla/icon.h"

#define DOCK_MAX_ITEMS 24
#define DOCK_ITEM_SIZE 36
#define DOCK_ITEM_GAP  8
#define DOCK_PAD_X     12
#define DOCK_PAD_Y     8

#define DOCK_PINNED_COUNT 6

#define DOCK_MENU_WIDTH  THEME_PX(152)
#define DOCK_MENU_ITEM_H THEME_PX(26)
#define DOCK_MENU_SEP_H  THEME_PX(7)
#define DOCK_MENU_PAD_Y  THEME_PX(6)
#define DOCK_MENU_HEIGHT (DOCK_MENU_PAD_Y * 2 + 3 * DOCK_MENU_ITEM_H + DOCK_MENU_SEP_H)

typedef enum {
    DOCK_ITEM_PINNED = 0,
    DOCK_ITEM_RUNNING = 1,
    DOCK_ITEM_SEPARATOR = 2
} dock_item_type_t;

typedef struct {
    dock_item_type_t type;
    char             name[32];
    char             exec_path[128];
    char             icon_name[32];
    uint32_t         window_id;
    int              is_mapped;
    int              is_focused;
    int              is_minimized;
    int32_t          x_offset;
    icon_t          *icon;
} dock_item_t;

typedef struct {
    vanilla_window_t *win;
    int32_t           x;
    int32_t           y;
    int32_t           width;
    int32_t           height;
    int               item_count;
    dock_item_t       items[DOCK_MAX_ITEMS];
    int               hovered_idx;
    int               pressed_idx;
    int               dirty;

    /* Context menu for right-click */
    int               menu_open;
    int               menu_item_idx;
    vanilla_window_t *menu_win;
    int               menu_dirty;
} dock_state_t;

struct shell_state;

void dock_init(dock_state_t *dock);
void dock_free_icons(dock_state_t *dock);
void dock_recompute_geometry(dock_state_t *dock, int32_t screen_w, int32_t screen_h);
void dock_render(dock_state_t *dock);
void dock_sync_windows(dock_state_t *dock, const void *win_list_ptr, int32_t screen_w, int32_t screen_h);
int  dock_handle_mouse_click(dock_state_t *dock, int32_t cx, int32_t cy, int btn, struct shell_state *st);
void dock_open_menu(dock_state_t *dock, int item_idx, struct shell_state *st);
void dock_close_menu(dock_state_t *dock);
void dock_render_menu(dock_state_t *dock);
int  dock_handle_menu_click(dock_state_t *dock, int32_t cx, int32_t cy, struct shell_state *st);

#endif /* _VANILLA_DOCK_H */
