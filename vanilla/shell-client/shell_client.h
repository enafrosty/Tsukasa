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
#include "statusbar.h"
#include "dock.h"

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

#define START_MENU_WIDTH     THEME_PX(188)
#define START_MENU_HEADER_H  THEME_PX(24)
#define START_MENU_SEP_H     THEME_PX(7)
#define START_MENU_ITEMS_Y   (THEME_PX(6) + START_MENU_HEADER_H + START_MENU_SEP_H)
#define START_MENU_ITEM_H    THEME_PX(28)
#define START_MENU_NUM_ITEMS 5
#define START_MENU_HEIGHT    (START_MENU_ITEMS_Y + START_MENU_NUM_ITEMS * START_MENU_ITEM_H + THEME_PX(6))

#define LAUNCHER_WIDTH        THEME_PX(440)
#define LAUNCHER_HEIGHT       THEME_PX(280)
#define LAUNCHER_SEARCH_MAX   64
#define LAUNCHER_MAX_APPS     16
#define LAUNCHER_ITEM_HEIGHT  THEME_PX(36)

#define SHELL_MAX_WINDOWS 64

typedef struct {
    uint32_t window_id;
    char     title[VANILLA_TITLE_MAX];
    char     exec_name[32];
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

typedef struct shell_state {
    vanilla_client_t    *client;
    vanilla_window_t    *taskbar_win;
    vanilla_window_t    *statusbar_win;
    vanilla_window_t    *dock_win;
    int32_t              screen_w;
    int32_t              screen_h;
    statusbar_state_t    statusbar;
    dock_state_t         dock;
    vanilla_shell_t      shell;
    vanilla_launcher_t   launcher;
    shell_window_list_t  win_list;
    uint16_t             mod_state;
    int                  running;
    int                  taskbar_dirty;
} shell_state_t;

struct vanilla_server;

static inline const char *shell_strcasestr(const char *haystack, const char *needle)
{
    if (!haystack || !needle)
        return NULL;
    if (*needle == '\0')
        return haystack;

    for (const char *h = haystack; *h != '\0'; h++) {
        const char *hp = h;
        const char *np = needle;
        while (*hp != '\0' && *np != '\0') {
            unsigned char hc = (unsigned char)*hp;
            unsigned char nc = (unsigned char)*np;
            if (hc >= 'A' && hc <= 'Z')
                hc = (unsigned char)(hc + 32);
            if (nc >= 'A' && nc <= 'Z')
                nc = (unsigned char)(nc + 32);
            if (hc != nc)
                break;
            hp++;
            np++;
        }
        if (*np == '\0')
            return h;
    }
    return NULL;
}

static inline int shell_strncasecmp(const char *a, const char *b, size_t n)
{
    if (!a || !b)
        return (a == b) ? 0 : (a ? 1 : -1);

    for (size_t i = 0; i < n; i++) {
        unsigned char ca = (unsigned char)a[i];
        unsigned char cb = (unsigned char)b[i];
        if (ca >= 'A' && ca <= 'Z')
            ca = (unsigned char)(ca + 32);
        if (cb >= 'A' && cb <= 'Z')
            cb = (unsigned char)(cb + 32);
        if (ca != cb)
            return (int)ca - (int)cb;
        if (ca == '\0')
            return 0;
    }
    return 0;
}

static inline uint32_t shell_blend_argb(uint32_t dst, uint32_t src)
{
    uint32_t sa = (src >> 24) & 0xFFu;
    if (sa == 0)
        return dst;
    if (sa == 255u)
        return src;

    uint32_t da = (dst >> 24) & 0xFFu;
    if (da == 0)
        return src;

    uint32_t inv = 255u - sa;
    uint32_t da_inv = da * inv;
    uint32_t sa_255 = sa * 255u;
    uint32_t num_a = sa_255 + da_inv;
    if (num_a == 0)
        return 0;

    uint32_t sr = (src >> 16) & 0xFFu;
    uint32_t sg = (src >> 8) & 0xFFu;
    uint32_t sb = src & 0xFFu;
    uint32_t dr = (dst >> 16) & 0xFFu;
    uint32_t dg = (dst >> 8) & 0xFFu;
    uint32_t db = dst & 0xFFu;

    uint32_t out_a = (num_a + 127u) / 255u;
    if (out_a > 255u)
        out_a = 255u;
    uint32_t half = num_a / 2u;
    uint32_t r = (sr * sa_255 + dr * da_inv + half) / num_a;
    uint32_t g = (sg * sa_255 + dg * da_inv + half) / num_a;
    uint32_t b = (sb * sa_255 + db * da_inv + half) / num_a;
    return (out_a << 24) | (r << 16) | (g << 8) | b;
}

static inline uint32_t shell_menu_bg(void)
{
    if (g_theme && g_theme->menu_bg)
        return g_theme->menu_bg;
    return g_theme ? g_theme->bg_elevated : 0xFF434C5Eu;
}

static inline uint32_t shell_menu_border(void)
{
    if (g_theme && g_theme->menu_border)
        return g_theme->menu_border;
    return g_theme ? g_theme->border : 0xFF5E6A80u;
}

static inline uint32_t shell_menu_divider(void)
{
    if (g_theme && g_theme->menu_divider)
        return g_theme->menu_divider;
    return shell_menu_border();
}

static inline uint32_t shell_menu_selection_bg(uint32_t base_bg)
{
    uint32_t sel = g_theme ? g_theme->selection : 0x4488C0D0u;
    return shell_blend_argb(base_bg, sel);
}

void shell_init(vanilla_shell_t *shell);
void shell_free_icons(void);
void shell_update_clock(vanilla_shell_t *shell, struct vanilla_server *srv);
void shell_spawn_app(const char *path);

void launcher_init(vanilla_launcher_t *launcher);
int  launcher_fuzzy_match(const char *pattern, const char *target);
void launcher_update_matches(vanilla_launcher_t *launcher);
int  launcher_handle_key_state(vanilla_launcher_t *launcher, uint16_t code, int pressed, uint16_t mod_state);
void shell_close_launcher(shell_state_t *st);
void shell_close_start_menu(shell_state_t *st);
void shell_handle_event(shell_state_t *st, const vanilla_event_t *ev);

#endif /* _VANILLA_SHELL_CLIENT_H */
