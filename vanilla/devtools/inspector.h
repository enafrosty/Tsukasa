/*
 * Project Tsukasa — Display Server Developer Tools & Window Inspector Header
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

#ifndef _VANILLA_DEVTOOLS_INSPECTOR_H
#define _VANILLA_DEVTOOLS_INSPECTOR_H

#include <stdint.h>
#include <stddef.h>
#include "../include/vanilla.h"
#include "../include/protocol.h"
#include "../include/theme.h"
#include "vendor/microui/microui.h"

#define DEVTOOLS_DEFAULT_WIDTH   800
#define DEVTOOLS_DEFAULT_HEIGHT  600
#define DEVTOOLS_QUERY_INTERVAL  500

typedef enum {
    DEVTOOLS_PANEL_INSPECTOR = 0,
    DEVTOOLS_PANEL_PERF      = 1,
    DEVTOOLS_PANEL_THEME     = 2,
} devtools_panel_t;

typedef struct {
    uint32_t window_id;
    int32_t  pid;
    char     title[VANILLA_TITLE_MAX];
    int32_t  x, y;
    uint32_t w, h;
    int32_t  z_index;
    uint8_t  layer;
    uint8_t  is_mapped;
    uint8_t  is_focused;
    int32_t  damage_x, damage_y, damage_w, damage_h;
    uint32_t frame_count;
    uint32_t prev_frame_count;
    uint32_t fps;
} devtools_win_entry_t;

typedef struct {
    vanilla_client_t       *client;
    vanilla_window_t       *win;
    int                     dbg_fd;
    devtools_panel_t        active_panel;
    mu_Context              mu_ctx;
    vanilla_surface_t      *surface;
    int                     running;

    /* Window inspector metrics */
    devtools_win_entry_t    windows[VANILLA_MAX_WINDOWS];
    uint32_t                window_count;
    uint64_t                last_query_time_ms;

    /* Compositor metrics */
    vanilla_dbg_compositor_t comp_stats;

    /* Theme editor state */
    vanilla_theme_t         active_theme;
    vanilla_theme_t         original_theme;
    int                     theme_loaded;
    int                     theme_saved;
    char                    status_msg[64];
} devtools_state_t;

/* Renderer callbacks for microui */
void mu_renderer_draw_rect(mu_Rect rect, mu_Color color);
void mu_renderer_draw_text(const char *text, mu_Vec2 pos, mu_Color color);
void mu_renderer_draw_icon(int icon, mu_Rect rect, mu_Color color);
int  mu_renderer_get_text_width(const char *text, int len);
int  mu_renderer_get_text_height(void);

#endif /* _VANILLA_DEVTOOLS_INSPECTOR_H */
