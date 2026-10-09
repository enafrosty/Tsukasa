/*
 * Project Tsukasa — Vanilla System Status Bar Implementation
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

#include "statusbar.h"
#include "shell_client.h"
#include "../apps/app_common.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <time.h>

typedef struct {
    const char *title;
    const char *path;
    const char *badge;
} system_menu_item_t;

static const system_menu_item_t g_system_menu_items[SYSTEM_MENU_NUM_ITEMS] = {
    { "About Tsukasa",    "/bin/sysfetch",     "TS" },
    { "Display Settings", "/bin/settings.elf", "CF" },
    { "Task Manager",     "/bin/taskmgr.elf",  "TM" },
    { "Reboot System",    "/bin/reboot",       "RB" },
    { "Power Off",        "/bin/poweroff",     "PW" },
};

static unsigned long read_sysfs_key_val(const char *filename, const char *key)
{
    FILE *fp = fopen(filename, "r");
    if (!fp)
        return 0;

    char line[256];
    size_t klen = strlen(key);
    unsigned long val = 0;

    while (fgets(line, sizeof(line), fp)) {
        if (strncmp(line, key, klen) == 0) {
            const char *p = line + klen;
            while (*p == ' ' || *p == ':')
                p++;
            val = strtoul(p, NULL, 10);
            break;
        }
    }
    fclose(fp);
    return val;
}

static uint32_t count_proc_processes(void)
{
    FILE *fp = fopen("/proc/processes", "r");
    if (!fp)
        return 0;

    char line[256];
    uint32_t count = 0;
    int is_header = 1;

    while (fgets(line, sizeof(line), fp)) {
        if (is_header) {
            is_header = 0;
            continue;
        }
        count++;
    }
    fclose(fp);
    return count;
}

void statusbar_init(statusbar_state_t *sb)
{
    if (!sb)
        return;

    memset(sb, 0, sizeof(*sb));
    sb->height = STATUSBAR_HEIGHT;
    strncpy(sb->active_app_title, "Tsukasa Desktop", sizeof(sb->active_app_title) - 1);
    strncpy(sb->breadcrumb, "Tsukasa OS", sizeof(sb->breadcrumb) - 1);
    strncpy(sb->clock_str, "12:00:00", sizeof(sb->clock_str) - 1);
    sb->cpu_usage_pct = 2;
    sb->ram_used_mb = 96;
    sb->ram_total_mb = 256;
    sb->volume_pct = 100;
    sb->menu_open = 0;
    sb->menu_selected_idx = 0;
    sb->menu_win = NULL;
    sb->dirty = 1;
}

void statusbar_update_telemetry(statusbar_state_t *sb)
{
    if (!sb)
        return;

    time_t now = time(NULL);
    if (sb->last_telemetry_sec != 0 && (now - sb->last_telemetry_sec) < 1)
        return;

    sb->last_telemetry_sec = now;

    /* Query physical memory */
    unsigned long total_pages = read_sysfs_key_val("/sys/memory", "pmm_total_pages");
    unsigned long used_pages = read_sysfs_key_val("/sys/memory", "pmm_used_pages");

    if (total_pages > 0) {
        sb->ram_total_mb = (uint32_t)((total_pages * 4096UL) / (1024 * 1024));
        sb->ram_used_mb = (uint32_t)((used_pages * 4096UL) / (1024 * 1024));
    } else {
        sb->ram_total_mb = 256;
        sb->ram_used_mb = 96;
    }

    /* Query process count for CPU activity estimation */
    uint32_t procs = count_proc_processes();
    if (procs > 0) {
        uint32_t est = 1 + (procs * 2);
        if (est > 99)
            est = 99;
        sb->cpu_usage_pct = est;
    } else {
        sb->cpu_usage_pct = 3;
    }

    /* Format digital clock */
    if (now > 0) {
        struct tm tm_val;
#if defined(_WIN32)
        gmtime_s(&tm_val, &now);
#else
        gmtime_r(&now, &tm_val);
#endif
        char buf[16];
        snprintf(buf, sizeof(buf), "%02d:%02d:%02d", tm_val.tm_hour, tm_val.tm_min, tm_val.tm_sec);
        if (strncmp(buf, sb->clock_str, sizeof(buf)) != 0) {
            strncpy(sb->clock_str, buf, sizeof(sb->clock_str) - 1);
            sb->clock_str[sizeof(sb->clock_str) - 1] = '\0';
        }
    }

    sb->dirty = 1;
}

void statusbar_set_active_title(statusbar_state_t *sb, const char *title)
{
    if (!sb)
        return;

    if (!title || !title[0]) {
        strncpy(sb->active_app_title, "Tsukasa Desktop", sizeof(sb->active_app_title) - 1);
    } else {
        strncpy(sb->active_app_title, title, sizeof(sb->active_app_title) - 1);
    }
    sb->active_app_title[sizeof(sb->active_app_title) - 1] = '\0';
    sb->dirty = 1;
}

void statusbar_render(statusbar_state_t *sb, int32_t screen_w)
{
    if (!sb || !sb->win || !sb->win->surface.pixels)
        return;

    vanilla_surface_t *surf = &sb->win->surface;
    int32_t w = (int32_t)surf->width;
    int32_t h = (int32_t)surf->height;

    /* Background translucent glass */
    app_fill_rect(surf, 0, 0, w, h, THEME_COLOR_STATUSBAR_BG);

    /* 1px bottom border stroke */
    app_fill_rect(surf, 0, h - 1, w, 1, THEME_COLOR_STATUSBAR_BORDER);

    /* Left: Tsukasa snowflake emblem button */
    uint32_t emblem_bg = sb->menu_open ? THEME_COLOR_FROST_CYAN : THEME_COLOR_DOCK_PILL;
    app_fill_rect(surf, THEME_PX(6), THEME_PX(4), THEME_PX(20), THEME_PX(20), emblem_bg);
    uint32_t emblem_fg = sb->menu_open ? g_theme->bg_base : THEME_COLOR_FROST_CYAN;
    app_draw_text(surf, THEME_PX(12), THEME_PX(7), "*", emblem_fg);

    /* Left: Active application title in bold highlight */
    const char *title = sb->active_app_title[0] ? sb->active_app_title : "Tsukasa Desktop";
    app_draw_text(surf, THEME_PX(32), THEME_PX(7), title, g_theme->fg_primary);

    /* Center: Contextual breadcrumbs path */
    const char *crumb = sb->breadcrumb[0] ? sb->breadcrumb : "Tsukasa OS";
    int32_t crumb_len = (int32_t)strlen(crumb);
    int32_t crumb_w = crumb_len * 8;
    int32_t crumb_x = (screen_w - crumb_w) / 2;
    if (crumb_x > THEME_PX(180)) {
        app_draw_text(surf, crumb_x, THEME_PX(7), crumb, g_theme->fg_muted);
    }

    /* Right: System telemetry string (CPU, RAM, Clock) */
    char telem[64];
    snprintf(telem, sizeof(telem), "CPU %u%%  |  RAM %uM/%uM  |  %s",
             sb->cpu_usage_pct, sb->ram_used_mb, sb->ram_total_mb, sb->clock_str);
    int32_t telem_len = (int32_t)strlen(telem);
    int32_t telem_w = telem_len * 8;
    int32_t telem_x = w - telem_w - THEME_PX(12);

    if (telem_x > THEME_PX(240)) {
        app_draw_text(surf, telem_x, THEME_PX(7), telem, g_theme->fg_primary);
    }
}

void statusbar_open_menu(statusbar_state_t *sb, shell_state_t *st)
{
    if (!sb || !st || !st->client)
        return;

    if (sb->menu_open && sb->menu_win)
        return;

    sb->menu_open = 1;
    sb->menu_selected_idx = 0;

    if (!sb->menu_win) {
        int32_t sm_x = THEME_PX(6);
        int32_t sm_y = STATUSBAR_HEIGHT + THEME_PX(2);
        sb->menu_win = vanilla_create_window(st->client, "System Menu", sm_x, sm_y,
                                            SYSTEM_MENU_WIDTH, SYSTEM_MENU_HEIGHT,
                                            WINDOW_FLAG_POPUP | WINDOW_FLAG_ALWAYS_TOP |
                                            WINDOW_FLAG_BORDERLESS | WINDOW_FLAG_TRANSPARENT);
    }

    if (sb->menu_win) {
        vanilla_map_window(sb->menu_win);
        statusbar_render_menu(sb);
        vanilla_present(sb->menu_win, NULL);
    }
    sb->dirty = 1;
}

void statusbar_close_menu(statusbar_state_t *sb)
{
    if (!sb)
        return;

    sb->menu_open = 0;
    if (sb->menu_win) {
        vanilla_unmap_window(sb->menu_win);
    }
    sb->dirty = 1;
}

void statusbar_toggle_menu(statusbar_state_t *sb, shell_state_t *st)
{
    if (!sb)
        return;

    if (sb->menu_open)
        statusbar_close_menu(sb);
    else
        statusbar_open_menu(sb, st);
}

void statusbar_render_menu(statusbar_state_t *sb)
{
    if (!sb || !sb->menu_win || !sb->menu_win->surface.pixels)
        return;

    vanilla_surface_t *surf = &sb->menu_win->surface;
    int32_t w = (int32_t)surf->width;
    int32_t h = (int32_t)surf->height;

    /* Panel background */
    app_fill_rect(surf, 0, 0, w, h, g_theme->bg_base);

    /* 1px border */
    int32_t bw = g_theme->border_width > 0 ? g_theme->border_width : 1;
    app_fill_rect(surf, 0, 0, w, bw, g_theme->border);
    app_fill_rect(surf, w - bw, 0, bw, h, g_theme->border);
    app_fill_rect(surf, 0, 0, bw, h, g_theme->border);
    app_fill_rect(surf, 0, h - bw, w, bw, g_theme->border);

    /* Menu items */
    for (int i = 0; i < SYSTEM_MENU_NUM_ITEMS; i++) {
        const system_menu_item_t *item = &g_system_menu_items[i];
        int32_t item_y = THEME_PX(10) + i * SYSTEM_MENU_ITEM_H;
        int32_t item_w = w - THEME_PX(16);
        int32_t item_h = SYSTEM_MENU_ITEM_H - THEME_PX(4);

        if (i == sb->menu_selected_idx)
            app_fill_rect(surf, THEME_PX(8), item_y, item_w, item_h, g_theme->bg_elevated);

        /* Badge square */
        int32_t badge_x = THEME_PX(8) + THEME_PX(4);
        int32_t badge_y = item_y + THEME_PX(3);
        app_fill_rect(surf, badge_x, badge_y, THEME_PX(18), THEME_PX(18), THEME_COLOR_DOCK_PILL);
        if (item->badge)
            app_draw_text(surf, badge_x + THEME_PX(2), badge_y + THEME_PX(4), item->badge, THEME_COLOR_FROST_CYAN);

        /* Item title */
        app_draw_text(surf, THEME_PX(8) + THEME_PX(28), item_y + THEME_PX(6), item->title, g_theme->fg_primary);
    }
}

int statusbar_handle_menu_click(statusbar_state_t *sb, int32_t cx, int32_t cy, shell_state_t *st)
{
    (void)cx;
    if (!sb || !st)
        return 0;

    int rel_y = cy - THEME_PX(10);
    if (rel_y >= 0) {
        int item_idx = rel_y / SYSTEM_MENU_ITEM_H;
        if (item_idx >= 0 && item_idx < SYSTEM_MENU_NUM_ITEMS) {
            shell_spawn_app(g_system_menu_items[item_idx].path);
            statusbar_close_menu(sb);
            return 1;
        }
    }
    return 0;
}

int statusbar_handle_mouse_click(statusbar_state_t *sb, int32_t cx, int32_t cy, shell_state_t *st)
{
    (void)cy;
    if (!sb || !st)
        return 0;

    /* Check Tsukasa snowflake emblem button */
    if (cx >= THEME_PX(4) && cx < THEME_PX(32)) {
        statusbar_toggle_menu(sb, st);
        return 1;
    }

    return 0;
}
