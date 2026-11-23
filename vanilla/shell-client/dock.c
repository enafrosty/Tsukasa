/*
 * Project Tsukasa — Vanilla Floating Aurora Dock Implementation
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

#include "dock.h"
#include "shell_client.h"
#include "../apps/app_common.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <math.h>

static const struct {
    const char *name;
    const char *exec_path;
    const char *icon_name;
    const char *badge;
} g_pinned_defaults[DOCK_PINNED_COUNT] = {
    { "Terminal",     "/bin/terminal.elf", "terminal", "TR" },
    { "File Manager", "/bin/filemgr.elf",  "filemgr",  "FM" },
    { "Notepad",      "/bin/notepad.elf",  "notepad",  "NP" },
    { "Calculator",   "/bin/calc.elf",     "calc",     "CA" },
    { "Settings",     "/bin/settings.elf", "settings", "ST" },
    { "Task Manager", "/bin/taskmgr.elf",  "taskmgr",  "TM" },
};

static inline void dock_blend_pixel(uint32_t *dst, uint32_t src)
{
    *dst = shell_blend_argb(*dst, src);
}

static inline float dock_clampf(float v, float lo, float hi)
{
    if (v < lo)
        return lo;
    if (v > hi)
        return hi;
    return v;
}

static inline float dock_rounded_box_sd(float px, float py, float w, float h, float r)
{
    float half_w = w * 0.5f;
    float half_h = h * 0.5f;
    float qx = fabsf(px - half_w) - (half_w - r);
    float qy = fabsf(py - half_h) - (half_h - r);
    if (qx > 0.0f && qy > 0.0f) {
        return sqrtf(qx * qx + qy * qy) - r;
    }
    float d = qx > qy ? qx : qy;
    return d - r;
}

static void dock_fill_rounded_rect(vanilla_surface_t *surf, int32_t rx, int32_t ry,
                                   int32_t rw, int32_t rh, int32_t radius, uint32_t color)
{
    if (!surf || !surf->pixels || rw <= 0 || rh <= 0)
        return;

    int32_t surf_w = (int32_t)surf->width;
    int32_t surf_h = (int32_t)surf->height;
    uint32_t pitch_px = surf->pitch / sizeof(uint32_t);

    if (radius * 2 > rw)
        radius = rw / 2;
    if (radius * 2 > rh)
        radius = rh / 2;
    if (radius <= 0) {
        app_fill_rect(surf, rx, ry, rw, rh, color);
        return;
    }

    int32_t x0 = rx < 0 ? 0 : rx;
    int32_t y0 = ry < 0 ? 0 : ry;
    int32_t x1 = (rx + rw) > surf_w ? surf_w : (rx + rw);
    int32_t y1 = (ry + rh) > surf_h ? surf_h : (ry + rh);
    if (x0 >= x1 || y0 >= y1)
        return;

    uint32_t sa = (color >> 24) & 0xFFu;
    uint32_t rgb = color & 0x00FFFFFFu;
    if (sa == 0)
        return;

    float r = (float)radius;
    float fw = (float)rw;
    float fh = (float)rh;

    for (int32_t py = y0; py < y1; py++) {
        uint32_t *row = surf->pixels + (size_t)py * pitch_px;
        float fy = (float)(py - ry) + 0.5f;
        for (int32_t px = x0; px < x1; px++) {
            float fx = (float)(px - rx) + 0.5f;
            float sd = dock_rounded_box_sd(fx, fy, fw, fh, r);
            float cov = dock_clampf(0.5f - sd, 0.0f, 1.0f);
            if (cov <= 0.0f)
                continue;
            if (cov >= 0.999f) {
                dock_blend_pixel(&row[px], color);
            } else {
                uint32_t eff_a = (uint32_t)((float)sa * cov + 0.5f);
                if (eff_a > 0)
                    dock_blend_pixel(&row[px], (eff_a << 24) | rgb);
            }
        }
    }
}

void dock_init(dock_state_t *dock)
{
    if (!dock)
        return;

    memset(dock, 0, sizeof(*dock));
    dock->item_count = DOCK_PINNED_COUNT;
    dock->hovered_idx = -1;
    dock->pressed_idx = -1;
    dock->menu_open = 0;
    dock->menu_item_idx = -1;
    dock->menu_win = NULL;
    dock->dirty = 1;

    for (int i = 0; i < DOCK_PINNED_COUNT; i++) {
        dock_item_t *item = &dock->items[i];
        item->type = DOCK_ITEM_PINNED;
        strncpy(item->name, g_pinned_defaults[i].name, sizeof(item->name) - 1);
        strncpy(item->exec_path, g_pinned_defaults[i].exec_path, sizeof(item->exec_path) - 1);
        strncpy(item->icon_name, g_pinned_defaults[i].icon_name, sizeof(item->icon_name) - 1);
        item->window_id = 0;
        item->is_mapped = 0;
        item->is_focused = 0;
        item->is_minimized = 0;
        item->icon = icon_load(g_pinned_defaults[i].icon_name);
        if (!item->icon)
            item->icon = icon_load(g_pinned_defaults[i].name);
    }
}

void dock_free_icons(dock_state_t *dock)
{
    if (!dock)
        return;

    for (int i = 0; i < dock->item_count; i++) {
        if (dock->items[i].icon) {
            icon_free(dock->items[i].icon);
            dock->items[i].icon = NULL;
        }
    }
}

void dock_recompute_geometry(dock_state_t *dock, int32_t screen_w, int32_t screen_h)
{
    if (!dock)
        return;

    int32_t item_sz = THEME_PX(DOCK_ITEM_SIZE);
    int32_t item_gap = THEME_PX(DOCK_ITEM_GAP);
    int32_t pad_x = THEME_PX(DOCK_PAD_X);
    int32_t pad_y = THEME_PX(DOCK_PAD_Y);

    int32_t content_w = 0;
    for (int i = 0; i < dock->item_count; i++) {
        dock->items[i].x_offset = pad_x + content_w;
        if (dock->items[i].type == DOCK_ITEM_SEPARATOR)
            content_w += THEME_PX(6) + item_gap;
        else
            content_w += item_sz + item_gap;
    }
    if (dock->item_count > 0)
        content_w -= item_gap;

    dock->width = content_w + (pad_x * 2);
    dock->height = THEME_PX(DOCK_HEIGHT_PX);
    if (dock->height < item_sz + (pad_y * 2))
        dock->height = item_sz + (pad_y * 2);
    dock->x = (screen_w - dock->width) / 2;
    dock->y = screen_h - dock->height - THEME_PX(12);
}

void dock_render(dock_state_t *dock)
{
    if (!dock || !dock->win || !dock->win->surface.pixels)
        return;

    vanilla_surface_t *surf = &dock->win->surface;
    int32_t w = (int32_t)surf->width;
    int32_t h = (int32_t)surf->height;
    if (w <= 0 || h <= 0)
        return;

    uint32_t pitch_px = surf->pitch / sizeof(uint32_t);

    /* Clear surface pixels to fully transparent 0x00000000 to prevent black corners */
    for (int32_t y = 0; y < h; y++) {
        uint32_t *row = surf->pixels + (size_t)y * pitch_px;
        memset(row, 0, (size_t)w * sizeof(uint32_t));
    }

    /* Render frosted glass capsule with smooth AA DOCK_RADIUS_PX corner radius */
    int32_t radius = THEME_PX(DOCK_RADIUS_PX);
    if (radius * 2 > h) radius = h / 2;
    if (radius * 2 > w) radius = w / 2;
    if (radius < 1) radius = 1;

    float r = (float)radius;
    float fw = (float)w;
    float fh = (float)h;
    uint32_t bg = THEME_COLOR_DOCK_BG;
    uint32_t bd = THEME_COLOR_DOCK_BORDER;
    uint32_t bg_a = (bg >> 24) & 0xFFu;
    uint32_t bg_rgb = bg & 0x00FFFFFFu;
    uint32_t bd_a = (bd >> 24) & 0xFFu;
    uint32_t bd_rgb = bd & 0x00FFFFFFu;

    for (int32_t y = 0; y < h; y++) {
        uint32_t *row = surf->pixels + (size_t)y * pitch_px;
        float fy = (float)y + 0.5f;
        for (int32_t x = 0; x < w; x++) {
            float fx = (float)x + 0.5f;
            float sd = dock_rounded_box_sd(fx, fy, fw, fh, r);
            float outer_cov = dock_clampf(0.5f - sd, 0.0f, 1.0f);
            if (outer_cov <= 0.0f)
                continue;

            float inner_cov = dock_clampf(-0.5f - sd, 0.0f, 1.0f);
            float border_cov = outer_cov - inner_cov;

            if (outer_cov >= 0.999f && border_cov <= 0.001f) {
                row[x] = bg;
                continue;
            }

            uint32_t fill_a = (uint32_t)((float)bg_a * outer_cov + 0.5f);
            uint32_t px_col = (fill_a << 24) | bg_rgb;
            if (border_cov > 0.001f) {
                uint32_t rim_a = (uint32_t)((float)bd_a * border_cov + 0.5f);
                if (rim_a > 0)
                    px_col = shell_blend_argb(px_col, (rim_a << 24) | bd_rgb);
            }
            row[x] = px_col;
        }
    }

    int32_t item_sz = THEME_PX(DOCK_ITEM_SIZE);
    int32_t pad_y = THEME_PX(DOCK_PAD_Y);
    int32_t pill_r = THEME_PX(DOCK_RADIUS_PX);
    uint32_t hover_pill_color = (THEME_COLOR_FROST_CYAN & 0x00FFFFFFu) | 0x1E000000u;

    /* Render dock items */
    for (int i = 0; i < dock->item_count; i++) {
        dock_item_t *item = &dock->items[i];

        if (item->type == DOCK_ITEM_SEPARATOR) {
            app_fill_rect(surf, item->x_offset + THEME_PX(3), pad_y + THEME_PX(4),
                          1, item_sz - THEME_PX(8), THEME_COLOR_DOCK_BORDER);
            continue;
        }

        /* Hover and focus highlight pills behind icon */
        if (i == dock->hovered_idx && !item->is_focused) {
            dock_fill_rounded_rect(surf, item->x_offset - THEME_PX(2), pad_y - THEME_PX(2),
                                   item_sz + THEME_PX(4), item_sz + THEME_PX(4),
                                   pill_r, hover_pill_color);
        }
        if (item->is_focused) {
            dock_fill_rounded_rect(surf, item->x_offset - THEME_PX(2), pad_y - THEME_PX(2),
                                   item_sz + THEME_PX(4), item_sz + THEME_PX(4),
                                   pill_r, THEME_COLOR_DOCK_PILL);
        }

        /* Vector or fallback icon render */
        int rendered = 0;
        if (item->icon) {
            if (icon_render_to_surface(item->icon, surf, item->x_offset, pad_y, item_sz) == 0)
                rendered = 1;
        }

        if (!rendered) {
            /* Fallback badge for unpinned dynamic apps or missing icons */
            uint32_t fallback_bg = item->is_focused ? g_theme->accent : g_theme->bg_elevated;
            dock_fill_rounded_rect(surf, item->x_offset, pad_y, item_sz, item_sz, pill_r, fallback_bg);
            char badge[3];
            badge[0] = item->name[0] ? item->name[0] : 'A';
            badge[1] = item->name[1] ? item->name[1] : 'P';
            badge[2] = '\0';
            int32_t bw = 0;
            int32_t bh = 0;
            float bsz = THEME_F(13.0f);
            app_measure_text_ex(badge, bsz, APP_FONT_BOLD, &bw, &bh);
            app_draw_text_ex(surf, item->x_offset + (item_sz - bw) / 2,
                             pad_y + (item_sz - bh) / 2,
                             badge, bsz, APP_FONT_BOLD, g_theme->fg_primary);
        }

        /* Active/running AA indicator pill centered cleanly inside capsule */
        if (item->is_mapped || item->window_id != 0) {
            uint32_t dot_color = item->is_minimized ? THEME_COLOR_SLATE : THEME_COLOR_FROST_CYAN;
            int32_t dot_w = item->is_focused ? THEME_PX(14) : THEME_PX(6);
            int32_t dot_h = THEME_PX(3);
            if (dot_h < 2)
                dot_h = 2;
            int32_t dot_r = dot_h / 2;
            int32_t dot_x = item->x_offset + (item_sz - dot_w) / 2;
            int32_t dot_y = pad_y + item_sz + THEME_PX(4);
            if (dot_y + dot_h <= h - THEME_PX(2)) {
                dock_fill_rounded_rect(surf, dot_x, dot_y, dot_w, dot_h, dot_r, dot_color);
            }
        }
    }
}

static int match_app_name(const char *win_title, const char *exec_name, const char *app_name)
{
    if (!app_name)
        return 0;

    if (win_title && win_title[0]) {
        size_t alen = strlen(app_name);
        if (shell_strncasecmp(win_title, app_name, alen) == 0)
            return 1;

        if (shell_strcasestr(win_title, app_name) != NULL)
            return 1;
    }

    if (exec_name && exec_name[0]) {
        size_t alen = strlen(app_name);
        if (shell_strncasecmp(exec_name, app_name, alen) == 0)
            return 1;

        if (shell_strcasestr(exec_name, app_name) != NULL)
            return 1;
    }

    return 0;
}

void dock_sync_windows(dock_state_t *dock, const void *win_list_ptr, int32_t screen_w, int32_t screen_h)
{
    if (!dock || !win_list_ptr)
        return;

    const shell_window_list_t *wl = (const shell_window_list_t *)win_list_ptr;

    /* 1. Synchronize pinned items against running windows */
    for (int i = 0; i < DOCK_PINNED_COUNT; i++) {
        dock_item_t *pinned = &dock->items[i];
        int found = 0;

        for (int w = 0; w < wl->count; w++) {
            const shell_window_entry_t *we = &wl->windows[w];
            if (!we->window_id || (we->flags & WINDOW_FLAG_BORDERLESS))
                continue;

            if (match_app_name(we->title, we->exec_name, pinned->name) ||
                match_app_name(we->title, we->exec_name, pinned->icon_name) ||
                (pinned->window_id != 0 && pinned->window_id == we->window_id)) {
                pinned->window_id = we->window_id;
                pinned->is_mapped = we->is_mapped;
                pinned->is_focused = we->is_focused;
                pinned->is_minimized = we->is_minimized;
                found = 1;
                break;
            }
        }

        if (!found) {
            pinned->window_id = 0;
            pinned->is_mapped = 0;
            pinned->is_focused = 0;
            pinned->is_minimized = 0;
        }
    }

    /* 2. Prune old unpinned items no longer in window list */
    int idx = DOCK_PINNED_COUNT;
    while (idx < dock->item_count) {
        if (dock->items[idx].type == DOCK_ITEM_SEPARATOR) {
            idx++;
            continue;
        }

        int exists = 0;
        int owned_by_pinned = 0;
        for (int pi = 0; pi < DOCK_PINNED_COUNT; pi++) {
            if (dock->items[pi].window_id == dock->items[idx].window_id) {
                owned_by_pinned = 1;
                break;
            }
        }
        if (!owned_by_pinned) {
            for (int w = 0; w < wl->count; w++) {
                if (wl->windows[w].window_id == dock->items[idx].window_id) {
                    dock->items[idx].is_mapped = wl->windows[w].is_mapped;
                    dock->items[idx].is_focused = wl->windows[w].is_focused;
                    dock->items[idx].is_minimized = wl->windows[w].is_minimized;
                    exists = 1;
                    break;
                }
            }
        }

        if (!exists) {
            if (dock->items[idx].icon)
                icon_free(dock->items[idx].icon);
            for (int k = idx; k < dock->item_count - 1; k++)
                dock->items[k] = dock->items[k + 1];
            dock->item_count--;
        } else {
            idx++;
        }
    }

    /* Remove trailing or isolated separator if no unpinned items remain */
    int has_unpinned = 0;
    for (int i = DOCK_PINNED_COUNT; i < dock->item_count; i++) {
        if (dock->items[i].type == DOCK_ITEM_RUNNING) {
            has_unpinned = 1;
            break;
        }
    }
    if (!has_unpinned && dock->item_count > DOCK_PINNED_COUNT) {
        dock->item_count = DOCK_PINNED_COUNT;
    }

    /* 3. Append new unpinned running windows */
    for (int w = 0; w < wl->count; w++) {
        const shell_window_entry_t *we = &wl->windows[w];
        if (!we->window_id || (we->flags & WINDOW_FLAG_BORDERLESS))
            continue;

        int already_tracked = 0;
        for (int i = 0; i < dock->item_count; i++) {
            if (dock->items[i].window_id == we->window_id) {
                already_tracked = 1;
                break;
            }
        }

        if (!already_tracked && dock->item_count < DOCK_MAX_ITEMS - 1) {
            if (dock->item_count == DOCK_PINNED_COUNT) {
                dock_item_t *sep = &dock->items[dock->item_count++];
                memset(sep, 0, sizeof(*sep));
                sep->type = DOCK_ITEM_SEPARATOR;
            }

            dock_item_t *dyn = &dock->items[dock->item_count++];
            memset(dyn, 0, sizeof(*dyn));
            dyn->type = DOCK_ITEM_RUNNING;
            strncpy(dyn->name, we->title, sizeof(dyn->name) - 1);
            strncpy(dyn->exec_path, we->exec_name, sizeof(dyn->exec_path) - 1);
            dyn->window_id = we->window_id;
            dyn->is_mapped = we->is_mapped;
            dyn->is_focused = we->is_focused;
            dyn->is_minimized = we->is_minimized;
            dyn->icon = icon_load(we->title);
            if (!dyn->icon && we->exec_name[0])
                dyn->icon = icon_load(we->exec_name);
        }
    }

    dock_recompute_geometry(dock, screen_w, screen_h);
    if (dock->hovered_idx >= dock->item_count)
        dock->hovered_idx = -1;
    dock->dirty = 1;
}

void dock_handle_mouse_motion(dock_state_t *dock, int32_t cx, int32_t cy)
{
    if (!dock)
        return;

    int32_t dock_w = (dock->win && dock->win->surface.width > 0)
                     ? (int32_t)dock->win->surface.width : dock->width;
    int32_t dock_h = (dock->win && dock->win->surface.height > 0)
                     ? (int32_t)dock->win->surface.height : dock->height;

    int new_hovered = -1;
    if (cx >= 0 && cy >= 0 && cx < dock_w && cy < dock_h) {
        int32_t item_sz = THEME_PX(DOCK_ITEM_SIZE);
        for (int i = 0; i < dock->item_count; i++) {
            if (dock->items[i].type == DOCK_ITEM_SEPARATOR)
                continue;
            if (cx >= dock->items[i].x_offset && cx < dock->items[i].x_offset + item_sz) {
                new_hovered = i;
                break;
            }
        }
    }

    if (dock->hovered_idx != new_hovered) {
        dock->hovered_idx = new_hovered;
        dock->dirty = 1;
    }
}

int dock_handle_mouse_click(dock_state_t *dock, int32_t cx, int32_t cy, int btn, shell_state_t *st)
{
    (void)cy;
    if (!dock || !st)
        return 0;

    int32_t item_sz = THEME_PX(DOCK_ITEM_SIZE);
    int clicked_idx = -1;
    for (int i = 0; i < dock->item_count; i++) {
        if (dock->items[i].type == DOCK_ITEM_SEPARATOR)
            continue;
        if (cx >= dock->items[i].x_offset && cx < dock->items[i].x_offset + item_sz) {
            clicked_idx = i;
            break;
        }
    }

    if (clicked_idx < 0)
        return 0;

    dock_item_t *item = &dock->items[clicked_idx];

    if (btn == BTN_LEFT) {
        if (st->statusbar.menu_open)
            statusbar_close_menu(&st->statusbar);
        if (st->launcher.visible)
            shell_close_launcher(st);
        if (dock->menu_open)
            dock_close_menu(dock);

        if (item->type == DOCK_ITEM_PINNED && item->window_id == 0) {
            /* Unlaunched pinned item: spawn application */
            shell_spawn_app(item->exec_path);
            return 1;
        }

        if (item->window_id != 0) {
            int sfd = vanilla_client_get_fd(st->client);
            vanilla_msg_hdr_t hdr;
            hdr.magic = VANILLA_IPC_MAGIC;

            if (!item->is_mapped || !item->is_focused) {
                /* Raise and focus */
                vanilla_msg_map_window_t m_req;
                hdr.msg_type = MSG_MAP_WINDOW;
                hdr.payload_len = (uint16_t)sizeof(m_req);
                hdr.window_id = item->window_id;
                m_req.window_id = item->window_id;
                if (sfd >= 0) {
                    write(sfd, &hdr, sizeof(hdr));
                    write(sfd, &m_req, sizeof(m_req));
                }
                item->is_mapped = 1;
                item->is_focused = 1;
                item->is_minimized = 0;
            } else {
                /* Already focused: toggle minimize */
                vanilla_msg_unmap_window_t u_req;
                hdr.msg_type = MSG_UNMAP_WINDOW;
                hdr.payload_len = (uint16_t)sizeof(u_req);
                hdr.window_id = item->window_id;
                u_req.window_id = item->window_id;
                if (sfd >= 0) {
                    write(sfd, &hdr, sizeof(hdr));
                    write(sfd, &u_req, sizeof(u_req));
                }
                item->is_mapped = 0;
                item->is_focused = 0;
                item->is_minimized = 1;
            }

            /* Synchronize state with master window list */
            for (int w = 0; w < st->win_list.count; w++) {
                if (st->win_list.windows[w].window_id == item->window_id) {
                    st->win_list.windows[w].is_mapped = item->is_mapped;
                    st->win_list.windows[w].is_focused = item->is_focused;
                    st->win_list.windows[w].is_minimized = item->is_minimized;
                } else if (item->is_focused) {
                    st->win_list.windows[w].is_focused = 0;
                }
            }
            if (item->is_focused) {
                statusbar_set_active_title(&st->statusbar, item->name);
            }

            dock->dirty = 1;
            return 1;
        }
    } else if (btn == BTN_RIGHT) {
        if (st->statusbar.menu_open)
            statusbar_close_menu(&st->statusbar);
        if (st->launcher.visible)
            shell_close_launcher(st);
        dock_open_menu(dock, clicked_idx, st);
        return 1;
    }

    return 0;
}

static int32_t dock_menu_item_y(int idx)
{
    int32_t y = DOCK_MENU_PAD_Y + idx * DOCK_MENU_ITEM_H;
    if (idx >= 2)
        y += DOCK_MENU_SEP_H;
    return y;
}

void dock_open_menu(dock_state_t *dock, int item_idx, shell_state_t *st)
{
    if (!dock || !st || !st->client || item_idx < 0 || item_idx >= dock->item_count)
        return;

    dock->menu_open = 1;
    dock->menu_item_idx = item_idx;

    int32_t mx = dock->x + dock->items[item_idx].x_offset;
    if (mx < THEME_PX(8))
        mx = THEME_PX(8);
    if (mx + DOCK_MENU_WIDTH > st->screen_w - THEME_PX(8))
        mx = st->screen_w - DOCK_MENU_WIDTH - THEME_PX(8);
    int32_t my = dock->y - DOCK_MENU_HEIGHT - THEME_PX(6);
    int32_t min_top = STATUSBAR_HEIGHT + THEME_PX(4);
    if (my < min_top)
        my = min_top;

    if (!dock->menu_win) {
        dock->menu_win = vanilla_create_window(st->client, "Dock Menu", mx, my,
                                               DOCK_MENU_WIDTH, DOCK_MENU_HEIGHT,
                                               WINDOW_FLAG_POPUP | WINDOW_FLAG_ALWAYS_TOP |
                                               WINDOW_FLAG_BORDERLESS | WINDOW_FLAG_TRANSPARENT);
    } else {
        vanilla_move_window(dock->menu_win, mx, my);
    }

    if (dock->menu_win) {
        vanilla_map_window(dock->menu_win);
        dock_render_menu(dock);
        vanilla_present(dock->menu_win, NULL);
        dock->menu_dirty = 0;
    } else {
        dock->menu_dirty = 1;
    }
}

void dock_close_menu(dock_state_t *dock)
{
    if (!dock)
        return;

    dock->menu_open = 0;
    dock->menu_item_idx = -1;
    dock->menu_dirty = 0;
    if (dock->menu_win) {
        vanilla_unmap_window(dock->menu_win);
    }
    dock->dirty = 1;
}

void dock_render_menu(dock_state_t *dock)
{
    if (!dock || !dock->menu_win || !dock->menu_win->surface.pixels)
        return;

    vanilla_surface_t *surf = &dock->menu_win->surface;
    int32_t w = (int32_t)surf->width;
    int32_t h = (int32_t)surf->height;

    /* Sharp 0px menu panel surface contrast (MENU_RADIUS_PX == 0) */
    app_fill_rect(surf, 0, 0, w, h, shell_menu_bg());
    int32_t bw = g_theme->border_width > 0 ? g_theme->border_width : 1;
    uint32_t bcol = shell_menu_border();
    app_fill_rect(surf, 0, 0, w, bw, bcol);
    app_fill_rect(surf, w - bw, 0, bw, h, bcol);
    app_fill_rect(surf, 0, 0, bw, h, bcol);
    app_fill_rect(surf, 0, h - bw, w, bw, bcol);

    dock_item_t *item = (dock->menu_item_idx >= 0 && dock->menu_item_idx < dock->item_count)
                        ? &dock->items[dock->menu_item_idx] : NULL;

    const char *actions[3] = { "New Window", "Maximize", "Close" };
    float font_sz = THEME_F(12.0f);
    int32_t pad_x = THEME_PX(12);
    vanilla_rect_t clip = { pad_x, 0, w - pad_x * 2, h };

    for (int i = 0; i < 3; i++) {
        if (i == 2) {
            int32_t div_y = DOCK_MENU_PAD_Y + 2 * DOCK_MENU_ITEM_H + (DOCK_MENU_SEP_H / 2);
            app_fill_rect(surf, THEME_PX(8), div_y, w - THEME_PX(16), 1, shell_menu_divider());
        }

        int32_t iy = dock_menu_item_y(i);
        int32_t th = 0;
        app_measure_text_ex(actions[i], font_sz, APP_FONT_MEDIUM, NULL, &th);
        int32_t ty = iy + (DOCK_MENU_ITEM_H - th) / 2;
        uint32_t fg = g_theme->fg_primary;
        if (i >= 1 && (!item || item->window_id == 0))
            fg = g_theme->fg_dim;
        app_draw_text_clipped(surf, &clip, pad_x, ty, actions[i], font_sz, APP_FONT_MEDIUM, fg);
    }
}

int dock_handle_menu_click(dock_state_t *dock, int32_t cx, int32_t cy, shell_state_t *st)
{
    (void)cx;
    if (!dock || !st || dock->menu_item_idx < 0 || dock->menu_item_idx >= dock->item_count)
        return 0;

    int item_idx = dock->menu_item_idx;
    dock_item_t *item = &dock->items[item_idx];

    int action_idx = -1;
    for (int i = 0; i < 3; i++) {
        int32_t iy = dock_menu_item_y(i);
        if (cy >= iy && cy < iy + DOCK_MENU_ITEM_H) {
            action_idx = i;
            break;
        }
    }
    if (action_idx < 0 || action_idx >= 3) {
        dock_close_menu(dock);
        return 0;
    }

    if (action_idx == 0) {
        /* New Window */
        if (item->exec_path[0])
            shell_spawn_app(item->exec_path);
    } else if (action_idx == 1) {
        /* Maximize */
        if (item->window_id != 0 && st->client) {
            vanilla_window_action(st->client, item->window_id, WINDOW_ACTION_MAXIMIZE);
            item->is_mapped = 1;
            item->is_focused = 1;
            item->is_minimized = 0;
            for (int w = 0; w < st->win_list.count; w++) {
                if (st->win_list.windows[w].window_id == item->window_id) {
                    st->win_list.windows[w].is_mapped = 1;
                    st->win_list.windows[w].is_focused = 1;
                    st->win_list.windows[w].is_minimized = 0;
                } else {
                    st->win_list.windows[w].is_focused = 0;
                }
            }
            statusbar_set_active_title(&st->statusbar, item->name);
        }
    } else if (action_idx == 2) {
        /* Close */
        if (item->window_id != 0 && st->client) {
            vanilla_window_action(st->client, item->window_id, WINDOW_ACTION_CLOSE);
        }
    }

    dock_close_menu(dock);
    return 1;
}
