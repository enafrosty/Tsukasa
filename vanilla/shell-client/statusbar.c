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
#include <math.h>

typedef enum {
    SYS_MENU_ACTION_SPAWN = 0,
    SYS_MENU_ACTION_REBOOT = 1,
    SYS_MENU_ACTION_POWEROFF = 2,
} system_menu_action_t;

typedef struct {
    const char          *title;
    system_menu_action_t action;
    const char          *path;
    const char          *badge;
} system_menu_item_t;

static const system_menu_item_t g_system_menu_items[SYSTEM_MENU_NUM_ITEMS] = {
    { "About Tsukasa",    SYS_MENU_ACTION_SPAWN,    "/bin/settings.elf", "TS" },
    { "Display Settings", SYS_MENU_ACTION_SPAWN,    "/bin/settings.elf", "CF" },
    { "Task Manager",     SYS_MENU_ACTION_SPAWN,    "/bin/taskmgr.elf",  "TM" },
    { "Reboot System",    SYS_MENU_ACTION_REBOOT,   NULL,                "RB" },
    { "Power Off",        SYS_MENU_ACTION_POWEROFF, NULL,                "PW" },
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

static int read_proc_stat_cpu(uint64_t *active_out, uint64_t *total_out)
{
    FILE *fp = fopen("/proc/stat", "r");
    if (!fp)
        return -1;

    char line[256];
    int ok = -1;
    while (fgets(line, sizeof(line), fp)) {
        if (strncmp(line, "cpu ", 4) == 0) {
            const char *p = line + 4;
            uint64_t cols[10];
            int ncols = 0;
            while (*p && ncols < 10) {
                while (*p == ' ' || *p == '\t')
                    p++;
                if (*p < '0' || *p > '9')
                    break;
                cols[ncols++] = (uint64_t)strtoull(p, (char **)&p, 10);
            }
            if (ncols >= 4) {
                uint64_t total = 0;
                for (int i = 0; i < ncols; i++)
                    total += cols[i];
                uint64_t idle = cols[3] + (ncols >= 5 ? cols[4] : 0ULL);
                uint64_t active = (total >= idle) ? (total - idle) : 0ULL;
                if (active_out)
                    *active_out = active;
                if (total_out)
                    *total_out = total;
                ok = 0;
            }
            break;
        }
    }
    fclose(fp);
    return ok;
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
    sb->cpu_usage_pct = 0;
    sb->ram_used_mb = 96;
    sb->ram_total_mb = 256;
    sb->volume_pct = 100;
    sb->menu_open = 0;
    sb->menu_selected_idx = 0;
    sb->menu_win = NULL;
    sb->emblem_icon = icon_load("tsukasa");
    sb->dirty = 1;
    sb->menu_dirty = 0;
}

void statusbar_free_icons(statusbar_state_t *sb)
{
    if (!sb)
        return;

    if (sb->emblem_icon) {
        icon_free(sb->emblem_icon);
        sb->emblem_icon = NULL;
    }
}

void statusbar_update_telemetry(statusbar_state_t *sb)
{
    if (!sb)
        return;

    time_t now = time(NULL);
    if (sb->last_telemetry_sec != 0 && now == sb->last_telemetry_sec)
        return;

    uint32_t prev_ram_total = sb->ram_total_mb;
    uint32_t prev_ram_used = sb->ram_used_mb;
    uint32_t prev_cpu = sb->cpu_usage_pct;
    char prev_clock[16];
    strncpy(prev_clock, sb->clock_str, sizeof(prev_clock) - 1);
    prev_clock[sizeof(prev_clock) - 1] = '\0';
    int first_update = (sb->last_telemetry_sec == 0);

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

    /* Query CPU activity delta from /proc/stat */
    uint64_t stat_active = 0;
    uint64_t stat_total = 0;
    if (read_proc_stat_cpu(&stat_active, &stat_total) == 0) {
        if (sb->has_prev_cpu && stat_total > sb->prev_cpu_total) {
            uint64_t dt_total = stat_total - sb->prev_cpu_total;
            uint64_t dt_active = (stat_active >= sb->prev_cpu_active)
                                     ? (stat_active - sb->prev_cpu_active) : 0ULL;
            uint32_t pct = (uint32_t)((dt_active * 100ULL) / dt_total);
            if (pct > 100u)
                pct = 100u;
            sb->cpu_usage_pct = pct;
        } else if (!sb->has_prev_cpu) {
            uint32_t pct = (stat_total > 0) ? (uint32_t)((stat_active * 100ULL) / stat_total) : 0u;
            if (pct > 100u)
                pct = 100u;
            sb->cpu_usage_pct = pct;
        }
        sb->prev_cpu_active = stat_active;
        sb->prev_cpu_total = stat_total;
        sb->has_prev_cpu = 1;
    } else {
        sb->cpu_usage_pct = 0;
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

    if (first_update ||
        sb->ram_total_mb != prev_ram_total ||
        sb->ram_used_mb != prev_ram_used ||
        sb->cpu_usage_pct != prev_cpu ||
        strcmp(sb->clock_str, prev_clock) != 0) {
        sb->dirty = 1;
    }
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

static void sb_fill_rounded_rect(vanilla_surface_t *surf, int32_t rx, int32_t ry,
                                 int32_t rw, int32_t rh, int32_t radius, uint32_t color)
{
    if (!surf || !surf->pixels || rw <= 0 || rh <= 0)
        return;

    uint32_t base_a = (color >> 24) & 0xFFu;
    if (base_a == 0)
        return;

    if (radius <= 0) {
        app_fill_rect(surf, rx, ry, rw, rh, color);
        return;
    }

    if (radius * 2 > rw) radius = rw / 2;
    if (radius * 2 > rh) radius = rh / 2;
    if (radius <= 0) {
        app_fill_rect(surf, rx, ry, rw, rh, color);
        return;
    }

    int32_t surf_w = (int32_t)surf->width;
    int32_t surf_h = (int32_t)surf->height;
    uint32_t pitch_px = surf->pitch / sizeof(uint32_t);
    uint32_t rgb = color & 0x00FFFFFFu;

    float r = (float)radius;
    float inner_r = (r > 0.5f) ? (r - 0.5f) : 0.0f;
    float outer_r = r + 0.5f;
    float inner_r2 = inner_r * inner_r;
    float outer_r2 = outer_r * outer_r;

    for (int32_t cy = 0; cy < rh; cy++) {
        int32_t py = ry + cy;
        if (py < 0 || py >= surf_h)
            continue;
        float fy = (float)cy + 0.5f;
        uint32_t *row = surf->pixels + (size_t)py * pitch_px;

        for (int32_t cx = 0; cx < rw; cx++) {
            int32_t px = rx + cx;
            if (px < 0 || px >= surf_w)
                continue;
            float fx = (float)cx + 0.5f;

            float cov = 1.0f;
            float dx = 0.0f;
            float dy = 0.0f;
            int in_corner = 0;

            if (fx < r && fy < r) {
                dx = r - fx;
                dy = r - fy;
                in_corner = 1;
            } else if (fx > (float)rw - r && fy < r) {
                dx = fx - ((float)rw - r);
                dy = r - fy;
                in_corner = 1;
            } else if (fx < r && fy > (float)rh - r) {
                dx = r - fx;
                dy = fy - ((float)rh - r);
                in_corner = 1;
            } else if (fx > (float)rw - r && fy > (float)rh - r) {
                dx = fx - ((float)rw - r);
                dy = fy - ((float)rh - r);
                in_corner = 1;
            }

            if (in_corner) {
                float d2 = dx * dx + dy * dy;
                if (d2 >= outer_r2) {
                    cov = 0.0f;
                } else if (d2 > inner_r2) {
                    cov = outer_r - sqrtf(d2);
                    if (cov < 0.0f) cov = 0.0f;
                    if (cov > 1.0f) cov = 1.0f;
                }
            }

            if (cov <= 0.0f)
                continue;
            uint32_t eff_a = (uint32_t)((float)base_a * cov + 0.5f);
            if (eff_a == 0)
                continue;
            row[px] = shell_blend_argb(row[px], (eff_a << 24) | rgb);
        }
    }
}

static void sb_render_emblem_icon(statusbar_state_t *sb, vanilla_surface_t *surf,
                                  int32_t bx, int32_t by, int32_t bsz, uint32_t emblem_fg)
{
    int rendered = 0;
    if (sb->emblem_icon && bsz > 0 && bsz <= 512) {
        uint32_t stack_buf[64 * 64];
        uint32_t *buf = stack_buf;
        if (bsz > 64) {
            buf = (uint32_t *)malloc((size_t)bsz * (size_t)bsz * sizeof(uint32_t));
        }
        if (buf) {
            memset(buf, 0, (size_t)bsz * (size_t)bsz * sizeof(uint32_t));
            if (icon_render(sb->emblem_icon, bsz, buf, bsz) == 0) {
                int32_t surf_w = (int32_t)surf->width;
                int32_t surf_h = (int32_t)surf->height;
                uint32_t pitch_px = surf->pitch / sizeof(uint32_t);
                int32_t x0 = bx < 0 ? 0 : bx;
                int32_t y0 = by < 0 ? 0 : by;
                int32_t x1 = (bx + bsz > surf_w) ? surf_w : (bx + bsz);
                int32_t y1 = (by + bsz > surf_h) ? surf_h : (by + bsz);
                for (int32_t py = y0; py < y1; py++) {
                    const uint32_t *src_row = buf + (size_t)(py - by) * (size_t)bsz;
                    uint32_t *dst_row = surf->pixels + (size_t)py * pitch_px;
                    for (int32_t px = x0; px < x1; px++) {
                        dst_row[px] = shell_blend_argb(dst_row[px], src_row[px - bx]);
                    }
                }
                rendered = 1;
            }
            if (buf != stack_buf)
                free(buf);
        }
    }

    if (!rendered) {
        float star_sz = THEME_F(14.0f);
        int32_t sw = 0;
        int32_t sh = 0;
        app_measure_text_ex("*", star_sz, APP_FONT_BOLD, &sw, &sh);
        vanilla_rect_t clip = { bx, by, bsz, bsz };
        app_draw_text_clipped(surf, &clip, bx + (bsz - sw) / 2, by + (bsz - sh) / 2,
                              "*", star_sz, APP_FONT_BOLD, emblem_fg);
    }
}

void statusbar_render(statusbar_state_t *sb, int32_t screen_w)
{
    if (!sb || !sb->win || !sb->win->surface.pixels)
        return;

    vanilla_surface_t *surf = &sb->win->surface;
    int32_t w = (int32_t)surf->width;
    int32_t h = (int32_t)surf->height;

    /* Reset translucent background cleanly so 1 Hz updates do not accumulate alpha */
    uint32_t pitch_px = surf->pitch / sizeof(uint32_t);
    for (int32_t y = 0; y < h; y++) {
        uint32_t *row = surf->pixels + (size_t)y * pitch_px;
        for (int32_t x = 0; x < w; x++) {
            row[x] = THEME_COLOR_STATUSBAR_BG;
        }
    }

    /* 1px bottom border stroke */
    app_fill_rect(surf, 0, h - 1, w, 1, THEME_COLOR_STATUSBAR_BORDER);

    /* Left: Tsukasa emblem button */
    int32_t btn_x = THEME_PX(6);
    int32_t btn_y = THEME_PX(4);
    int32_t btn_sz = THEME_PX(20);
    uint32_t emblem_bg = sb->menu_open ? THEME_COLOR_FROST_CYAN : THEME_COLOR_DOCK_PILL;
    sb_fill_rounded_rect(surf, btn_x, btn_y, btn_sz, btn_sz, THEME_PX(4), emblem_bg);
    uint32_t emblem_fg = sb->menu_open ? g_theme->bg_base : THEME_COLOR_FROST_CYAN;
    sb_render_emblem_icon(sb, surf, btn_x, btn_y, btn_sz, emblem_fg);

    /* Right: System telemetry string (CPU, RAM, Clock) */
    char telem[64];
    snprintf(telem, sizeof(telem), "CPU %u%%  |  RAM %uM/%uM  |  %s",
             sb->cpu_usage_pct, sb->ram_used_mb, sb->ram_total_mb, sb->clock_str);
    float telem_sz = THEME_F(12.0f);
    int32_t telem_w = 0;
    int32_t telem_h = 0;
    app_measure_text_ex(telem, telem_sz, APP_FONT_MEDIUM, &telem_w, &telem_h);
    if (telem_h <= 0)
        telem_h = (int32_t)telem_sz;
    int32_t telem_x = w - telem_w - THEME_PX(12);
    int32_t telem_y = (h - telem_h) / 2;
    if (telem_x > THEME_PX(220)) {
        vanilla_rect_t telem_clip = { telem_x, 0, telem_w + THEME_PX(4), h - 1 };
        app_draw_text_clipped(surf, &telem_clip, telem_x, telem_y, telem,
                              telem_sz, APP_FONT_MEDIUM, g_theme->fg_primary);
    }

    /* Center: Contextual breadcrumbs path */
    const char *crumb = sb->breadcrumb[0] ? sb->breadcrumb : "Tsukasa OS";
    float crumb_sz = THEME_F(12.0f);
    int32_t crumb_w = 0;
    int32_t crumb_h = 0;
    app_measure_text_ex(crumb, crumb_sz, APP_FONT_REGULAR, &crumb_w, &crumb_h);
    if (crumb_h <= 0)
        crumb_h = (int32_t)crumb_sz;
    int32_t crumb_x = (screen_w - crumb_w) / 2;
    int32_t crumb_y = (h - crumb_h) / 2;
    int crumb_drawn = 0;
    if (crumb_x > THEME_PX(180) && crumb_x + crumb_w + THEME_PX(12) < telem_x) {
        vanilla_rect_t crumb_clip = { crumb_x, 0, crumb_w + THEME_PX(2), h - 1 };
        app_draw_text_clipped(surf, &crumb_clip, crumb_x, crumb_y, crumb,
                              crumb_sz, APP_FONT_REGULAR, g_theme->fg_muted);
        crumb_drawn = 1;
    }

    /* Left: Active application title in bold highlight */
    const char *title = sb->active_app_title[0] ? sb->active_app_title : "Tsukasa Desktop";
    float title_sz = THEME_F(13.0f);
    int32_t title_h = 0;
    app_measure_text_ex(title, title_sz, APP_FONT_BOLD, NULL, &title_h);
    if (title_h <= 0)
        title_h = (int32_t)title_sz;
    int32_t title_x = THEME_PX(34);
    int32_t title_y = (h - title_h) / 2;
    int32_t title_limit_x = (crumb_drawn ? crumb_x : telem_x) - THEME_PX(12);
    if (title_limit_x > title_x) {
        vanilla_rect_t title_clip = { title_x, 0, title_limit_x - title_x, h - 1 };
        app_draw_text_clipped(surf, &title_clip, title_x, title_y, title,
                              title_sz, APP_FONT_BOLD, g_theme->fg_primary);
    }
}

static int32_t sb_menu_item_y(int idx)
{
    int32_t y = SYSTEM_MENU_PAD_Y + idx * SYSTEM_MENU_ITEM_H;
    if (idx > SYSTEM_MENU_SEP_AFTER)
        y += SYSTEM_MENU_SEP_H;
    return y;
}

void statusbar_open_menu(statusbar_state_t *sb, shell_state_t *st)
{
    if (!sb || !st || !st->client)
        return;

    if (sb->menu_open && sb->menu_win)
        return;

    sb->menu_open = 1;
    sb->menu_selected_idx = -1;

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
    sb->menu_dirty = 0;
}

void statusbar_close_menu(statusbar_state_t *sb)
{
    if (!sb)
        return;

    sb->menu_open = 0;
    sb->menu_dirty = 0;
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

    /* Sharp 0px menu panel surface contrast (MENU_RADIUS_PX = 0) */
    uint32_t bg_col = shell_menu_bg();
    uint32_t border_col = shell_menu_border();
    uint32_t divider_col = shell_menu_divider();
    uint32_t sel_bg = shell_menu_selection_bg(bg_col);

    app_fill_rect(surf, 0, 0, w, h, bg_col);

    /* Sharp 1px outer border */
    int32_t bw = g_theme->border_width > 0 ? g_theme->border_width : 1;
    app_fill_rect(surf, 0, 0, w, bw, border_col);
    app_fill_rect(surf, w - bw, 0, bw, h, border_col);
    app_fill_rect(surf, 0, 0, bw, h, border_col);
    app_fill_rect(surf, 0, h - bw, w, bw, border_col);

    float item_sz = THEME_F(13.0f);
    float badge_sz = THEME_F(10.0f);
    int32_t item_th = 0;
    app_measure_text_ex("Ag", item_sz, APP_FONT_MEDIUM, NULL, &item_th);
    if (item_th <= 0)
        item_th = (int32_t)item_sz;

    /* Menu items */
    for (int i = 0; i < SYSTEM_MENU_NUM_ITEMS; i++) {
        const system_menu_item_t *item = &g_system_menu_items[i];
        int32_t item_y = sb_menu_item_y(i);
        int32_t item_x = THEME_PX(4);
        int32_t item_w = w - THEME_PX(8);
        int32_t item_h = SYSTEM_MENU_ITEM_H - THEME_PX(2);

        if (i == sb->menu_selected_idx) {
            app_fill_rect(surf, item_x, item_y, item_w, item_h, sel_bg);
            app_fill_rect(surf, item_x, item_y, THEME_PX(2), item_h, g_theme->accent);
        }

        /* Badge square */
        int32_t badge_sz_px = THEME_PX(18);
        int32_t badge_x = item_x + THEME_PX(6);
        int32_t badge_y = item_y + (item_h - badge_sz_px) / 2;
        app_fill_rect(surf, badge_x, badge_y, badge_sz_px, badge_sz_px, THEME_COLOR_DOCK_PILL);
        if (item->badge) {
            int32_t btw = 0;
            int32_t bth = 0;
            app_measure_text_ex(item->badge, badge_sz, APP_FONT_BOLD, &btw, &bth);
            if (bth <= 0)
                bth = (int32_t)badge_sz;
            vanilla_rect_t bclip = { badge_x, badge_y, badge_sz_px, badge_sz_px };
            app_draw_text_clipped(surf, &bclip,
                                  badge_x + (badge_sz_px - btw) / 2,
                                  badge_y + (badge_sz_px - bth) / 2,
                                  item->badge, badge_sz, APP_FONT_BOLD, THEME_COLOR_FROST_CYAN);
        }

        /* Item title */
        int32_t text_x = badge_x + badge_sz_px + THEME_PX(8);
        int32_t text_y = item_y + (item_h - item_th) / 2;
        vanilla_rect_t tclip = { text_x, item_y, item_x + item_w - text_x - THEME_PX(4), item_h };
        app_draw_text_clipped(surf, &tclip, text_x, text_y, item->title,
                              item_sz, APP_FONT_MEDIUM, g_theme->fg_primary);

        /* Subtle 1px section divider after Task Manager */
        if (i == SYSTEM_MENU_SEP_AFTER) {
            int32_t div_y = item_y + SYSTEM_MENU_ITEM_H + (SYSTEM_MENU_SEP_H / 2);
            app_fill_rect(surf, THEME_PX(8), div_y, w - THEME_PX(16), 1, divider_col);
        }
    }
}

int statusbar_handle_menu_click(statusbar_state_t *sb, int32_t cx, int32_t cy, shell_state_t *st)
{
    (void)cx;
    if (!sb || !st)
        return 0;

    for (int i = 0; i < SYSTEM_MENU_NUM_ITEMS; i++) {
        int32_t iy = sb_menu_item_y(i);
        if (cy >= iy && cy < iy + SYSTEM_MENU_ITEM_H) {
            const system_menu_item_t *item = &g_system_menu_items[i];
            statusbar_close_menu(sb);
            if (item->action == SYS_MENU_ACTION_SPAWN && item->path) {
                shell_spawn_app(item->path);
            } else if (item->action == SYS_MENU_ACTION_REBOOT) {
#if !defined(_WIN32) && !defined(__linux__) && !defined(TEST_SHELL_RUNNER)
                reboot(0x01234567);
#endif
            } else if (item->action == SYS_MENU_ACTION_POWEROFF) {
#if !defined(_WIN32) && !defined(__linux__) && !defined(TEST_SHELL_RUNNER)
                reboot(0x4321fedc);
#endif
            }
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

    if (sb->menu_open) {
        statusbar_close_menu(sb);
        return 1;
    }

    return 0;
}
