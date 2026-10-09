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
    uint32_t sa = (src >> 24) & 0xFF;
    if (sa == 0)
        return;

    if (sa == 255) {
        *dst = src;
        return;
    }

    uint32_t dp = *dst;
    uint32_t da = (dp >> 24) & 0xFF;
    uint32_t inv = 255 - sa;

    uint32_t r = (((src >> 16) & 0xFF) * sa + ((dp >> 16) & 0xFF) * inv + 127) / 255;
    uint32_t g = (((src >> 8) & 0xFF) * sa + ((dp >> 8) & 0xFF) * inv + 127) / 255;
    uint32_t b = ((src & 0xFF) * sa + (dp & 0xFF) * inv + 127) / 255;
    uint32_t a = sa + (da * inv + 127) / 255;
    if (a > 255)
        a = 255;

    *dst = (a << 24) | (r << 16) | (g << 8) | b;
}

static void dock_fill_rounded_rect(vanilla_surface_t *surf, int32_t rx, int32_t ry,
                                   int32_t rw, int32_t rh, int32_t radius, uint32_t color)
{
    if (!surf || !surf->pixels || rw <= 0 || rh <= 0)
        return;

    int32_t surf_w = (int32_t)surf->width;
    int32_t surf_h = (int32_t)surf->height;
    uint32_t pitch_px = surf->pitch / sizeof(uint32_t);

    if (radius * 2 > rw) radius = rw / 2;
    if (radius * 2 > rh) radius = rh / 2;
    if (radius < 1) radius = 1;
    int32_t r_sq = radius * radius;

    for (int32_t cy = 0; cy < rh; cy++) {
        int32_t py = ry + cy;
        if (py < 0 || py >= surf_h)
            continue;

        uint32_t *row = surf->pixels + (size_t)py * pitch_px;
        for (int32_t cx = 0; cx < rw; cx++) {
            int32_t px = rx + cx;
            if (px < 0 || px >= surf_w)
                continue;

            int is_inside = 1;
            if (cx < radius && cy < radius) {
                int32_t dx = radius - 1 - cx;
                int32_t dy = radius - 1 - cy;
                if (dx * dx + dy * dy > r_sq) is_inside = 0;
            } else if (cx >= rw - radius && cy < radius) {
                int32_t dx = cx - (rw - radius);
                int32_t dy = radius - 1 - cy;
                if (dx * dx + dy * dy > r_sq) is_inside = 0;
            } else if (cx < radius && cy >= rh - radius) {
                int32_t dx = radius - 1 - cx;
                int32_t dy = cy - (rh - radius);
                if (dx * dx + dy * dy > r_sq) is_inside = 0;
            } else if (cx >= rw - radius && cy >= rh - radius) {
                int32_t dx = cx - (rw - radius);
                int32_t dy = cy - (rh - radius);
                if (dx * dx + dy * dy > r_sq) is_inside = 0;
            }

            if (is_inside) {
                dock_blend_pixel(&row[px], color);
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

    int32_t content_w = 0;
    for (int i = 0; i < dock->item_count; i++) {
        dock->items[i].x_offset = DOCK_PAD_X + content_w;
        if (dock->items[i].type == DOCK_ITEM_SEPARATOR)
            content_w += THEME_PX(6) + DOCK_ITEM_GAP;
        else
            content_w += THEME_PX(DOCK_ITEM_SIZE) + THEME_PX(DOCK_ITEM_GAP);
    }
    if (dock->item_count > 0)
        content_w -= THEME_PX(DOCK_ITEM_GAP);

    dock->width = content_w + (DOCK_PAD_X * 2);
    dock->height = THEME_PX(DOCK_ITEM_SIZE) + (DOCK_PAD_Y * 2);
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

    /* Render frosted glass capsule with 16px corner radius */
    int32_t radius = DOCK_RADIUS_PX;
    if (radius * 2 > h) radius = h / 2;
    if (radius * 2 > w) radius = w / 2;
    int32_t r_sq = radius * radius;
    int32_t r_inner_sq = (radius - 1) * (radius - 1);

    for (int32_t y = 0; y < h; y++) {
        uint32_t *row = surf->pixels + (size_t)y * pitch_px;
        for (int32_t x = 0; x < w; x++) {
            int is_border = 0;
            int is_inside = 1;

            if (x < radius && y < radius) {
                int32_t dx = radius - 1 - x;
                int32_t dy = radius - 1 - y;
                int32_t d2 = dx * dx + dy * dy;
                if (d2 > r_sq) is_inside = 0;
                else if (d2 >= r_inner_sq) is_border = 1;
            } else if (x >= w - radius && y < radius) {
                int32_t dx = x - (w - radius);
                int32_t dy = radius - 1 - y;
                int32_t d2 = dx * dx + dy * dy;
                if (d2 > r_sq) is_inside = 0;
                else if (d2 >= r_inner_sq) is_border = 1;
            } else if (x < radius && y >= h - radius) {
                int32_t dx = radius - 1 - x;
                int32_t dy = y - (h - radius);
                int32_t d2 = dx * dx + dy * dy;
                if (d2 > r_sq) is_inside = 0;
                else if (d2 >= r_inner_sq) is_border = 1;
            } else if (x >= w - radius && y >= h - radius) {
                int32_t dx = x - (w - radius);
                int32_t dy = y - (h - radius);
                int32_t d2 = dx * dx + dy * dy;
                if (d2 > r_sq) is_inside = 0;
                else if (d2 >= r_inner_sq) is_border = 1;
            } else {
                if (x == 0 || x == w - 1 || y == 0 || y == h - 1)
                    is_border = 1;
            }

            if (is_inside) {
                row[x] = is_border ? THEME_COLOR_DOCK_BORDER : THEME_COLOR_DOCK_BG;
            }
        }
    }

    /* Render dock items */
    for (int i = 0; i < dock->item_count; i++) {
        dock_item_t *item = &dock->items[i];

        if (item->type == DOCK_ITEM_SEPARATOR) {
            app_fill_rect(surf, item->x_offset + 3, DOCK_PAD_Y + 4, 1, DOCK_ITEM_SIZE - 8, THEME_COLOR_DOCK_BORDER);
            continue;
        }

        /* Focus highlight pill behind active window icon */
        if (item->is_focused) {
            dock_fill_rounded_rect(surf, item->x_offset - 2, DOCK_PAD_Y - 2,
                                   DOCK_ITEM_SIZE + 4, DOCK_ITEM_SIZE + 4, 8, THEME_COLOR_DOCK_PILL);
        }

        /* Vector or fallback icon render */
        int rendered = 0;
        if (item->icon) {
            if (icon_render_to_surface(item->icon, surf, item->x_offset, DOCK_PAD_Y, DOCK_ITEM_SIZE) == 0)
                rendered = 1;
        }

        if (!rendered) {
            /* Fallback badge for unpinned dynamic apps or missing icons */
            uint32_t bg = item->is_focused ? g_theme->accent : g_theme->bg_elevated;
            dock_fill_rounded_rect(surf, item->x_offset, DOCK_PAD_Y, DOCK_ITEM_SIZE, DOCK_ITEM_SIZE, 8, bg);
            char badge[3];
            badge[0] = item->name[0] ? item->name[0] : 'A';
            badge[1] = item->name[1] ? item->name[1] : 'P';
            badge[2] = '\0';
            app_draw_text(surf, item->x_offset + 10, DOCK_PAD_Y + 14, badge, g_theme->fg_primary);
        }

        /* Running state indicator dot centered 3px below the icon */
        if (item->is_mapped || item->window_id != 0) {
            uint32_t dot_color = item->is_minimized ? THEME_COLOR_SLATE : THEME_COLOR_FROST_CYAN;
            int32_t dot_x = item->x_offset + (DOCK_ITEM_SIZE / 2) - 2;
            int32_t dot_y = DOCK_PAD_Y + DOCK_ITEM_SIZE + 3;
            if (dot_y + 4 <= h) {
                app_fill_rect(surf, dot_x, dot_y, 4, 4, dot_color);
            }
        }
    }
}

static int match_app_name(const char *win_title, const char *app_name)
{
    if (!win_title || !app_name)
        return 0;

    size_t alen = strlen(app_name);
    if (strncasecmp(win_title, app_name, alen) == 0)
        return 1;

    if (strcasestr(win_title, app_name) != NULL)
        return 1;

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

            if (match_app_name(we->title, pinned->name) || (pinned->window_id != 0 && pinned->window_id == we->window_id)) {
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
        for (int w = 0; w < wl->count; w++) {
            if (wl->windows[w].window_id == dock->items[idx].window_id) {
                dock->items[idx].is_mapped = wl->windows[w].is_mapped;
                dock->items[idx].is_focused = wl->windows[w].is_focused;
                dock->items[idx].is_minimized = wl->windows[w].is_minimized;
                exists = 1;
                break;
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
            dyn->window_id = we->window_id;
            dyn->is_mapped = we->is_mapped;
            dyn->is_focused = we->is_focused;
            dyn->is_minimized = we->is_minimized;
            dyn->icon = icon_load(we->title);
        }
    }

    dock_recompute_geometry(dock, screen_w, screen_h);
    dock->dirty = 1;
}

int dock_handle_mouse_click(dock_state_t *dock, int32_t cx, int32_t cy, int btn, shell_state_t *st)
{
    (void)cy;
    if (!dock || !st)
        return 0;

    int clicked_idx = -1;
    for (int i = 0; i < dock->item_count; i++) {
        if (dock->items[i].type == DOCK_ITEM_SEPARATOR)
            continue;
        if (cx >= dock->items[i].x_offset && cx < dock->items[i].x_offset + DOCK_ITEM_SIZE) {
            clicked_idx = i;
            break;
        }
    }

    if (clicked_idx < 0)
        return 0;

    dock_item_t *item = &dock->items[clicked_idx];

    if (btn == BTN_LEFT) {
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
            dock->dirty = 1;
            return 1;
        }
    } else if (btn == BTN_RIGHT) {
        dock_open_menu(dock, clicked_idx, st);
        return 1;
    }

    return 0;
}

void dock_open_menu(dock_state_t *dock, int item_idx, shell_state_t *st)
{
    if (!dock || !st || !st->client || item_idx < 0 || item_idx >= dock->item_count)
        return;

    dock->menu_open = 1;
    dock->menu_item_idx = item_idx;

    int32_t mx = dock->x + dock->items[item_idx].x_offset;
    if (mx + DOCK_MENU_WIDTH > st->screen_w)
        mx = st->screen_w - DOCK_MENU_WIDTH - 8;
    int32_t my = dock->y - DOCK_MENU_HEIGHT - 6;
    if (my < 32)
        my = 32;

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
    }
}

void dock_close_menu(dock_state_t *dock)
{
    if (!dock)
        return;

    dock->menu_open = 0;
    dock->menu_item_idx = -1;
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

    app_fill_rect(surf, 0, 0, w, h, g_theme->bg_base);
    int32_t bw = g_theme->border_width > 0 ? g_theme->border_width : 1;
    app_fill_rect(surf, 0, 0, w, bw, g_theme->border);
    app_fill_rect(surf, w - bw, 0, bw, h, g_theme->border);
    app_fill_rect(surf, 0, 0, bw, h, g_theme->border);
    app_fill_rect(surf, 0, h - bw, w, bw, g_theme->border);

    const char *actions[3] = { "New Window", "Maximize", "Close" };
    for (int i = 0; i < 3; i++) {
        int32_t iy = THEME_PX(4) + i * DOCK_MENU_ITEM_H;
        app_draw_text(surf, THEME_PX(12), iy + THEME_PX(6), actions[i], g_theme->fg_primary);
    }
}

int dock_handle_menu_click(dock_state_t *dock, int32_t cx, int32_t cy, shell_state_t *st)
{
    (void)cx;
    if (!dock || !st || dock->menu_item_idx < 0 || dock->menu_item_idx >= dock->item_count)
        return 0;

    int item_idx = dock->menu_item_idx;
    dock_item_t *item = &dock->items[item_idx];
    int action_idx = (cy - THEME_PX(4)) / DOCK_MENU_ITEM_H;

    if (action_idx == 0) {
        /* New Window */
        if (item->exec_path[0])
            shell_spawn_app(item->exec_path);
    } else if (action_idx == 1) {
        /* Maximize */
        if (item->window_id != 0) {
            int sfd = vanilla_client_get_fd(st->client);
            vanilla_msg_hdr_t hdr;
            hdr.magic = VANILLA_IPC_MAGIC;
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
        }
    } else if (action_idx == 2) {
        /* Close */
        if (item->window_id != 0) {
            int sfd = vanilla_client_get_fd(st->client);
            vanilla_msg_hdr_t hdr;
            hdr.magic = VANILLA_IPC_MAGIC;
            vanilla_msg_destroy_window_t d_req;
            hdr.msg_type = MSG_DESTROY_WINDOW;
            hdr.payload_len = (uint16_t)sizeof(d_req);
            hdr.window_id = item->window_id;
            d_req.window_id = item->window_id;
            if (sfd >= 0) {
                write(sfd, &hdr, sizeof(hdr));
                write(sfd, &d_req, sizeof(d_req));
            }
        }
    }

    dock_close_menu(dock);
    return 1;
}
