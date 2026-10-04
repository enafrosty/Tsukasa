/*
 * Project Tsukasa — Vanilla Desktop Shell and Launcher Client Implementation
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

#include "shell_client.h"
#include "../server/server.h"
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
} start_menu_item_t;

static const start_menu_item_t g_start_menu_items[START_MENU_NUM_ITEMS] = {
    { "Terminal",     "/bin/terminal.elf", ">_" },
    { "File Manager", "/bin/filemgr.elf",  "FM" },
    { "Notepad",      "/bin/notepad.elf",  "NP" },
    { "Calculator",   "/bin/calc.elf",     "+-" },
    { "Task Manager", "/bin/taskmgr.elf",  "TM" },
};

static const vanilla_app_entry_t g_launcher_apps[] = {
    { "Terminal",        "Tsukasa interactive shell",    "/bin/terminal.elf", 0 },
    { "Notepad",         "Simple text editor",           "/bin/notepad.elf",  0 },
    { "Calculator",      "Basic math calculator",        "/bin/calc.elf",     0 },
    { "File Manager",    "Browse directory files",       "/bin/filemgr.elf",  0 },
    { "Task Manager",    "Inspect and manage processes", "/bin/taskmgr.elf",  0 },
    { "System Fetch",    "Display system information",   "/bin/sysfetch",     0 },
    { "Settings",        "Desktop and system config",    "/bin/settings.elf", 0 },
    { "Process Viewer",  "Inspect running processes",    "/bin/ps",           0 },
    { "Network Info",    "Network status and sockets",   "/bin/net",          0 },
};
#define LAUNCHER_NUM_BUILTIN_APPS (int)(sizeof(g_launcher_apps) / sizeof(g_launcher_apps[0]))

static inline uint32_t shell_get_badge_color(int index)
{
    switch (index) {
    case 0: return g_theme->accent_pressed;
    case 1: return g_theme->accent;
    case 2: return g_theme->success;
    case 3: return g_theme->warning;
    case 4: return g_theme->accent_hover;
    default: return g_theme->accent;
    }
}

static inline uint32_t launcher_get_icon_color(int app_index)
{
    switch (app_index) {
    case 0: return g_theme->accent_pressed;
    case 1: return g_theme->success;
    case 2: return g_theme->warning;
    case 3: return g_theme->accent;
    case 4: return g_theme->accent_hover;
    case 5: return g_theme->accent_pressed;
    case 6: return g_theme->accent;
    case 7: return g_theme->accent_hover;
    case 8: return g_theme->accent;
    default: return g_theme->accent;
    }
}

static inline char to_lower(char c)
{
    return (c >= 'A' && c <= 'Z') ? (char)(c + 32) : c;
}

void shell_spawn_app(const char *path)
{
    char current_path[256];
    strncpy(current_path, path, sizeof(current_path) - 1);
    current_path[sizeof(current_path) - 1] = '\0';

    char *argv[] = { current_path, NULL };
    pid_t pid = spawn(current_path, argv, NULL);

    if (pid <= 0 && strncmp(current_path, "/bin/", 5) == 0) {
        char fat_path[320];
        snprintf(fat_path, sizeof(fat_path), "/fat12/%s", current_path + 5);
        char *fargv[] = { fat_path, NULL };
        pid = spawn(fat_path, fargv, NULL);
    }

    if (pid <= 0) {
        char upath[256];
        size_t len = strlen(current_path);
        size_t i;
        for (i = 0; i < len && i < sizeof(upath) - 1; i++) {
            char c = current_path[i];
            upath[i] = (c >= 'a' && c <= 'z') ? (char)(c - 32) : c;
        }
        upath[i] = '\0';
        char *uargv[] = { upath, NULL };
        pid = spawn(upath, uargv, NULL);

        if (pid <= 0 && strncmp(upath, "/BIN/", 5) == 0) {
            char fat_upath[320];
            snprintf(fat_upath, sizeof(fat_upath), "/fat12/%s", upath + 5);
            char *fuargv[] = { fat_upath, NULL };
            pid = spawn(fat_upath, fuargv, NULL);
        }
    }
    (void)pid;
}

static void launcher_exec_selected(vanilla_launcher_t *launcher)
{
    if (!launcher)
        return;

    int sel = launcher->selected_idx;
    if (sel >= 0 && sel < launcher->match_count) {
        int app_idx = launcher->matches[sel].app_index;
        const vanilla_app_entry_t *app = &g_launcher_apps[app_idx];

        char current_path[256];
        strncpy(current_path, app->exec_path, sizeof(current_path) - 1);
        current_path[sizeof(current_path) - 1] = '\0';

        char *argv[] = { current_path, NULL };
        pid_t pid = spawn(current_path, argv, NULL);

        if (pid <= 0) {
            size_t len = strlen(current_path);
            if (len < 4 || strcmp(current_path + len - 4, ".elf") != 0) {
                if (len + 4 < sizeof(current_path)) {
                    strcat(current_path, ".elf");
                    pid = spawn(current_path, argv, NULL);
                }
            }
        }

        if (pid <= 0) {
            char fat12_path[320];
            const char *leaf = current_path;
            if (strncmp(current_path, "/bin/", 5) == 0)
                leaf = current_path + 5;
            else if (current_path[0] == '/')
                leaf = current_path + 1;

            snprintf(fat12_path, sizeof(fat12_path), "/fat12/%s", leaf);
            char *fat12_argv[] = { fat12_path, NULL };
            pid = spawn(fat12_path, fat12_argv, NULL);

            if (pid <= 0 && strncmp(app->exec_path, "/bin/", 5) == 0 &&
                strcmp(app->exec_path, current_path) != 0) {
                snprintf(fat12_path, sizeof(fat12_path), "/fat12/%s", app->exec_path + 5);
                char *fat12_argv2[] = { fat12_path, NULL };
                pid = spawn(fat12_path, fat12_argv2, NULL);
            }
        }
        (void)pid;
    }
}

int launcher_fuzzy_match(const char *pattern, const char *target)
{
    if (!pattern || !target)
        return -1;

    if (pattern[0] == '\0')
        return 100;

    int score = 0;
    int p_idx = 0;
    int consecutive = 0;
    int prev_match_idx = -2;

    for (int t_idx = 0; target[t_idx] != '\0' && pattern[p_idx] != '\0'; t_idx++) {
        char p_char = to_lower(pattern[p_idx]);
        char t_char = to_lower(target[t_idx]);

        if (p_char == t_char) {
            score += 10;

            if (t_idx == 0 || target[t_idx - 1] == ' ' || target[t_idx - 1] == '-' ||
                target[t_idx - 1] == '_' || target[t_idx - 1] == '/') {
                score += 20;
            }

            if (t_idx == prev_match_idx + 1) {
                consecutive++;
                score += 15 * consecutive;
            } else {
                consecutive = 0;
            }

            prev_match_idx = t_idx;
            p_idx++;
        }
    }

    if (pattern[p_idx] != '\0')
        return -1;

    int len_diff = (int)strlen(target) - (int)strlen(pattern);
    if (len_diff > 0)
        score -= len_diff;

    return score > 0 ? score : 1;
}

void launcher_update_matches(vanilla_launcher_t *launcher)
{
    if (!launcher)
        return;

    launcher->match_count = 0;

    for (int i = 0; i < LAUNCHER_NUM_BUILTIN_APPS; i++) {
        int name_score = launcher_fuzzy_match(launcher->query, g_launcher_apps[i].name);
        int desc_score = launcher_fuzzy_match(launcher->query, g_launcher_apps[i].description);
        int best_score = name_score > desc_score ? name_score : desc_score;

        if (best_score > 0) {
            launcher->matches[launcher->match_count].app_index = i;
            launcher->matches[launcher->match_count].score = best_score;
            launcher->match_count++;
        }
    }

    for (int i = 1; i < launcher->match_count; i++) {
        vanilla_match_result_t key = launcher->matches[i];
        int j = i - 1;
        while (j >= 0 && launcher->matches[j].score < key.score) {
            launcher->matches[j + 1] = launcher->matches[j];
            j--;
        }
        launcher->matches[j + 1] = key;
    }

    if (launcher->selected_idx >= launcher->match_count)
        launcher->selected_idx = launcher->match_count > 0 ? launcher->match_count - 1 : 0;
}

void launcher_init(vanilla_launcher_t *launcher)
{
    if (!launcher)
        return;

    launcher->visible = 0;
    launcher->query[0] = '\0';
    launcher->query_len = 0;
    launcher->selected_idx = 0;
    launcher->match_count = 0;
    launcher->win = NULL;
    launcher->dirty = 0;
    launcher_update_matches(launcher);
}

int launcher_handle_key_state(vanilla_launcher_t *launcher, uint16_t code, int pressed, uint16_t mod_state)
{
    if (!launcher)
        return 0;

    if (!pressed)
        return 1;

    if (code == KEY_ESC) {
        launcher->visible = 0;
        launcher->dirty = 1;
        return 1;
    }

    if (code == KEY_ENTER || code == KEY_KPENTER) {
        launcher_exec_selected(launcher);
        launcher->visible = 0;
        launcher->dirty = 1;
        return 1;
    }

    if (code == KEY_UP) {
        if (launcher->selected_idx > 0) {
            launcher->selected_idx--;
            launcher->dirty = 1;
        }
        return 1;
    }

    if (code == KEY_DOWN) {
        if (launcher->selected_idx + 1 < launcher->match_count) {
            launcher->selected_idx++;
            launcher->dirty = 1;
        }
        return 1;
    }

    if (code == KEY_BACKSPACE) {
        if (launcher->query_len > 0) {
            launcher->query[--launcher->query_len] = '\0';
            launcher_update_matches(launcher);
            launcher->dirty = 1;
        }
        return 1;
    }

    char ch = vanilla_evdev_to_ascii(code, (mod_state & MOD_SHIFT) ? 1 : 0);
    if (ch != 0 && launcher->query_len < LAUNCHER_SEARCH_MAX - 1) {
        launcher->query[launcher->query_len++] = ch;
        launcher->query[launcher->query_len] = '\0';
        launcher_update_matches(launcher);
        launcher->dirty = 1;
        return 1;
    }

    return 1;
}

void shell_init(vanilla_shell_t *shell)
{
    if (!shell)
        return;

    shell->start_menu_open = 0;
    shell->selected_idx = 0;
    shell->last_clock_sec = 0;
    shell->start_menu_win = NULL;
    shell->start_menu_dirty = 0;
    strncpy(shell->clock_str, "12:00", sizeof(shell->clock_str) - 1);
    shell->clock_str[sizeof(shell->clock_str) - 1] = '\0';
}

void shell_update_clock(vanilla_shell_t *shell, struct vanilla_server *srv)
{
    (void)srv;
    if (!shell)
        return;

    time_t now = time(NULL);
    if (now != shell->last_clock_sec && now > 0) {
        struct tm tm;
#if defined(_WIN32)
        gmtime_s(&tm, &now);
#else
        gmtime_r(&now, &tm);
#endif
        char buf[16];
        snprintf(buf, sizeof(buf), "%02d:%02d", tm.tm_hour, tm.tm_min);

        if (strncmp(buf, shell->clock_str, sizeof(buf)) != 0) {
            strncpy(shell->clock_str, buf, sizeof(shell->clock_str) - 1);
            shell->clock_str[sizeof(shell->clock_str) - 1] = '\0';
        }
        shell->last_clock_sec = now;
    }
}


static void __attribute__((unused)) shell_render_taskbar(shell_state_t *st)
{
    if (!st || !st->taskbar_win || !st->taskbar_win->surface.pixels)
        return;

    vanilla_surface_t *surf = &st->taskbar_win->surface;
    int32_t w = (int32_t)surf->width;
    int32_t h = (int32_t)surf->height;

    /* Background panel */
    app_fill_rect(surf, 0, 0, w, h, g_theme->taskbar_bg);

    /* Top border line */
    app_fill_rect(surf, 0, 0, w, g_theme->border_width, g_theme->border);

    /* Start button */
    uint32_t btn_color = st->shell.start_menu_open ? g_theme->accent_hover : g_theme->accent_pressed;
    app_fill_rect(surf, TASKBAR_START_X, THEME_PX(4), TASKBAR_START_W, TASKBAR_START_H, btn_color);
    app_draw_text(surf, TASKBAR_START_X + THEME_PX(14), THEME_PX(4) + THEME_PX(7), "Start", g_theme->taskbar_text);

    /* Window pills */
    int pill_idx = 0;
    for (int i = 0; i < st->win_list.count; i++) {
        shell_window_entry_t *we = &st->win_list.windows[i];
        if (!we->window_id || (we->flags & WINDOW_FLAG_BORDERLESS))
            continue;

        int32_t px = TASKBAR_PILL_START_X + pill_idx * (TASKBAR_PILL_W + TASKBAR_PILL_GAP);
        if (px + TASKBAR_PILL_W > w - TASKBAR_CLOCK_W - THEME_PX(8))
            break;

        uint32_t bg = we->is_focused ? g_theme->taskbar_item_open : (we->is_mapped ? g_theme->bg_base : g_theme->taskbar_bg);
        app_fill_rect(surf, px, THEME_PX(4), TASKBAR_PILL_W, TASKBAR_PILL_H, bg);

        if (we->is_focused) {
            app_fill_rect(surf, px + THEME_PX(2), THEME_PX(4) + TASKBAR_PILL_H - THEME_PX(2),
                          TASKBAR_PILL_W - THEME_PX(4), THEME_PX(2), g_theme->taskbar_item_active);
        }

        const char *title = we->title[0] ? we->title : "App";
        uint32_t fg = we->is_focused ? g_theme->fg_primary : (we->is_mapped ? g_theme->fg_muted : g_theme->fg_dim);
        app_draw_text(surf, px + THEME_PX(8), THEME_PX(4) + THEME_PX(7), title, fg);

        pill_idx++;
    }

    /* System tray digital clock */
    int32_t clock_x = w - TASKBAR_CLOCK_W - THEME_PX(4);
    int32_t clock_y = THEME_PX(4);
    app_fill_rect(surf, clock_x, clock_y, TASKBAR_CLOCK_W, TASKBAR_CLOCK_H, g_theme->bg_base);
    app_draw_text(surf, clock_x + THEME_PX(16), clock_y + THEME_PX(7), st->shell.clock_str, g_theme->taskbar_text);
}

static void start_menu_render_client(shell_state_t *st)
{
    if (!st || !st->shell.start_menu_win || !st->shell.start_menu_win->surface.pixels)
        return;

    vanilla_surface_t *surf = &st->shell.start_menu_win->surface;
    int32_t w = (int32_t)surf->width;
    int32_t h = (int32_t)surf->height;

    /* Panel background */
    app_fill_rect(surf, 0, 0, w, h, g_theme->bg_base);

    /* Borders */
    int32_t bw = g_theme->border_width;
    app_fill_rect(surf, 0, 0, w, bw, g_theme->border);
    app_fill_rect(surf, w - bw, 0, bw, h, g_theme->border);
    app_fill_rect(surf, 0, 0, bw, h, g_theme->border);
    app_fill_rect(surf, 0, h - bw, w, bw, g_theme->border);

    /* Start Menu items */
    for (int i = 0; i < START_MENU_NUM_ITEMS; i++) {
        const start_menu_item_t *item = &g_start_menu_items[i];
        int32_t item_y = THEME_PX(12) + i * START_MENU_ITEM_H;
        int32_t item_w = w - THEME_PX(12);
        int32_t item_h = START_MENU_ITEM_H - THEME_PX(4);

        if (i == st->shell.selected_idx)
            app_fill_rect(surf, THEME_PX(6), item_y, item_w, item_h, g_theme->bg_elevated);

        /* Item badge / icon */
        int32_t badge_x = THEME_PX(6) + THEME_PX(4);
        int32_t badge_y = item_y + THEME_PX(3);
        app_fill_rect(surf, badge_x, badge_y, THEME_PX(18), THEME_PX(18), shell_get_badge_color(i));
        app_draw_text(surf, badge_x + THEME_PX(2), badge_y + THEME_PX(4), item->badge, g_theme->fg_primary);

        /* Item title */
        app_draw_text(surf, THEME_PX(6) + THEME_PX(28), item_y + THEME_PX(6), item->title, g_theme->fg_primary);
    }
}

static void launcher_render_client(shell_state_t *st)
{
    if (!st || !st->launcher.win || !st->launcher.win->surface.pixels)
        return;

    vanilla_surface_t *surf = &st->launcher.win->surface;
    int32_t w = (int32_t)surf->width;
    int32_t h = (int32_t)surf->height;

    /* Background panel */
    app_fill_rect(surf, 0, 0, w, h, g_theme->bg_base);

    /* Border */
    app_draw_rect(surf, 0, 0, w, h, g_theme->border_focus);

    /* Search input box */
    int32_t sb_x = THEME_PX(12);
    int32_t sb_y = THEME_PX(12);
    int32_t sb_w = w - THEME_PX(24);
    int32_t sb_h = THEME_PX(32);
    app_fill_rect(surf, sb_x, sb_y, sb_w, sb_h, g_theme->bg_elevated);

    if (st->launcher.query_len > 0) {
        app_draw_text(surf, sb_x + THEME_PX(8), sb_y + THEME_PX(9), st->launcher.query, g_theme->fg_primary);
    } else {
        app_draw_text(surf, sb_x + THEME_PX(8), sb_y + THEME_PX(9), "Type to search apps...", g_theme->fg_muted);
    }

    /* Result list (up to 5 items) */
    int32_t item_start_y = THEME_PX(54);
    for (int i = 0; i < st->launcher.match_count && i < 5; i++) {
        int app_idx = st->launcher.matches[i].app_index;
        const vanilla_app_entry_t *app = &g_launcher_apps[app_idx];
        int32_t iy = item_start_y + i * LAUNCHER_ITEM_HEIGHT;
        int32_t iw = w - THEME_PX(24);
        int32_t ih = LAUNCHER_ITEM_HEIGHT - THEME_PX(2);

        uint32_t bg = (i == st->launcher.selected_idx) ? g_theme->bg_elevated : g_theme->bg_base;
        app_fill_rect(surf, sb_x, iy, iw, ih, bg);

        if (i == st->launcher.selected_idx)
            app_fill_rect(surf, sb_x, iy, THEME_PX(3), ih, g_theme->accent);

        /* Icon badge */
        int32_t ic_x = sb_x + THEME_PX(8);
        int32_t ic_y = iy + THEME_PX(8);
        app_fill_rect(surf, ic_x, ic_y, THEME_PX(16), THEME_PX(16), launcher_get_icon_color(app_idx));

        /* App name and description */
        app_draw_text(surf, sb_x + THEME_PX(32), iy + THEME_PX(9), app->name, g_theme->fg_primary);
        app_draw_text(surf, sb_x + THEME_PX(140), iy + THEME_PX(9), app->description, g_theme->fg_dim);
    }
}

static void shell_toggle_launcher(shell_state_t *st)
{
    if (st->launcher.visible) {
        st->launcher.visible = 0;
        if (st->launcher.win) {
            vanilla_destroy_window(st->launcher.win);
            st->launcher.win = NULL;
        }
    } else {
        int32_t lx = (st->screen_w - LAUNCHER_WIDTH) / 2;
        int32_t ly = (st->screen_h - TASKBAR_HEIGHT - LAUNCHER_HEIGHT) / 2;
        if (ly < THEME_PX(20))
            ly = THEME_PX(20);

        st->launcher.win = vanilla_create_window(st->client, "Quick Launcher", lx, ly,
                                                 LAUNCHER_WIDTH, LAUNCHER_HEIGHT,
                                                 WINDOW_FLAG_POPUP | WINDOW_FLAG_ALWAYS_TOP | WINDOW_FLAG_TRANSPARENT);
        if (st->launcher.win) {
            vanilla_map_window(st->launcher.win);
            st->launcher.visible = 1;
            st->launcher.query[0] = '\0';
            st->launcher.query_len = 0;
            st->launcher.selected_idx = 0;
            launcher_update_matches(&st->launcher);
            launcher_render_client(st);
            vanilla_present(st->launcher.win, NULL);
        }
    }
}

static void shell_toggle_start_menu(shell_state_t *st)
{
    if (st->shell.start_menu_open) {
        st->shell.start_menu_open = 0;
        if (st->shell.start_menu_win) {
            vanilla_destroy_window(st->shell.start_menu_win);
            st->shell.start_menu_win = NULL;
        }
        st->taskbar_dirty = 1;
    } else {
        int32_t sm_x = 0;
        int32_t sm_y = st->screen_h - TASKBAR_HEIGHT - START_MENU_HEIGHT;

        st->shell.start_menu_win = vanilla_create_window(st->client, "Start Menu", sm_x, sm_y,
                                                         START_MENU_WIDTH, START_MENU_HEIGHT,
                                                         WINDOW_FLAG_POPUP | WINDOW_FLAG_ALWAYS_TOP | WINDOW_FLAG_BORDERLESS | WINDOW_FLAG_TRANSPARENT);
        if (st->shell.start_menu_win) {
            vanilla_map_window(st->shell.start_menu_win);
            st->shell.start_menu_open = 1;
            st->shell.selected_idx = 0;
            start_menu_render_client(st);
            vanilla_present(st->shell.start_menu_win, NULL);
        }
        st->taskbar_dirty = 1;
    }
}

static void __attribute__((unused)) shell_handle_event(shell_state_t *st, const vanilla_event_t *ev)
{
    if (!st || !ev)
        return;

    st->mod_state = ev->mod_state;

    if (ev->type == VANILLA_EVENT_CLOSE_REQ) {
        if (st->taskbar_win && ev->window_id == st->taskbar_win->window_id) {
            st->running = 0;
        } else if (st->launcher.win && ev->window_id == st->launcher.win->window_id) {
            shell_toggle_launcher(st);
        } else if (st->shell.start_menu_win && ev->window_id == st->shell.start_menu_win->window_id) {
            shell_toggle_start_menu(st);
        }
        return;
    }

    if (ev->type == VANILLA_EVENT_CONFIGURE) {
        if (st->taskbar_win && ev->window_id == st->taskbar_win->window_id) {
            if (ev->configure.width > 0 && ev->configure.height > 0) {
                if ((int32_t)ev->configure.width != st->screen_w) {
                    st->screen_w = (int32_t)ev->configure.width;
                    st->taskbar_dirty = 1;
                }
            }
        } else if ((!st->launcher.win || ev->window_id != st->launcher.win->window_id) &&
                   (!st->shell.start_menu_win || ev->window_id != st->shell.start_menu_win->window_id)) {
            if (ev->configure.width == 0 || ev->configure.height == 0) {
                /* Window unmapped or destroyed */
                for (int i = 0; i < st->win_list.count; i++) {
                    if (st->win_list.windows[i].window_id == ev->window_id) {
                        for (int j = i; j < st->win_list.count - 1; j++)
                            st->win_list.windows[j] = st->win_list.windows[j + 1];
                        st->win_list.count--;
                        st->taskbar_dirty = 1;
                        break;
                    }
                }
            } else if (!(ev->configure.flags & WINDOW_FLAG_BORDERLESS)) {
                /* Normal top-level window */
                int found = 0;
                for (int i = 0; i < st->win_list.count; i++) {
                    if (st->win_list.windows[i].window_id == ev->window_id) {
                        st->win_list.windows[i].is_mapped = 1;
                        st->win_list.windows[i].flags = ev->configure.flags;
                        found = 1;
                        break;
                    }
                }
                if (!found && st->win_list.count < SHELL_MAX_WINDOWS) {
                    shell_window_entry_t *we = &st->win_list.windows[st->win_list.count++];
                    we->window_id = ev->window_id;
                    we->is_mapped = 1;
                    we->is_focused = 0;
                    we->is_minimized = 0;
                    we->flags = ev->configure.flags;
                    snprintf(we->title, sizeof(we->title), "App");
                }
                st->taskbar_dirty = 1;
            }
        }
        return;
    }

    if (ev->type == VANILLA_EVENT_FOCUS) {
        for (int i = 0; i < st->win_list.count; i++) {
            if (st->win_list.windows[i].window_id == ev->window_id) {
                st->win_list.windows[i].is_focused = ev->focus.focused;
            } else if (ev->focus.focused) {
                st->win_list.windows[i].is_focused = 0;
            }
        }
        st->taskbar_dirty = 1;

        /* Dismiss popups if focus shifted to a different window */
        if (ev->focus.focused) {
            if (st->launcher.visible && st->launcher.win && ev->window_id != st->launcher.win->window_id)
                shell_toggle_launcher(st);
            if (st->shell.start_menu_open && st->shell.start_menu_win && ev->window_id != st->shell.start_menu_win->window_id)
                shell_toggle_start_menu(st);
        }
        return;
    }

    if (ev->type == VANILLA_EVENT_INPUT) {
        const struct input_event *iev = &ev->input;

        /* Global Alt+Space launcher toggle */
        if (iev->type == EV_KEY && iev->code == KEY_SPACE && iev->value == 1 && (st->mod_state & MOD_ALT)) {
            shell_toggle_launcher(st);
            return;
        }

        /* Taskbar window events */
        if (st->taskbar_win && ev->window_id == st->taskbar_win->window_id) {
            if (iev->type == EV_KEY && iev->code == BTN_LEFT && iev->value == 1) {
                int32_t cx = (int32_t)iev->pad1;
                int32_t cy = (int32_t)iev->pad2;

                /* Click Start button */
                if (cx >= TASKBAR_START_X && cx < TASKBAR_START_X + TASKBAR_START_W &&
                    cy >= THEME_PX(4) && cy < THEME_PX(4) + TASKBAR_START_H) {
                    shell_toggle_start_menu(st);
                    return;
                }

                /* Click window pill */
                int pill_idx = 0;
                for (int i = 0; i < st->win_list.count; i++) {
                    shell_window_entry_t *we = &st->win_list.windows[i];
                    if (!we->window_id || (we->flags & WINDOW_FLAG_BORDERLESS))
                        continue;

                    int32_t px = TASKBAR_PILL_START_X + pill_idx * (TASKBAR_PILL_W + TASKBAR_PILL_GAP);
                    if (px + TASKBAR_PILL_W > st->screen_w - TASKBAR_CLOCK_W - THEME_PX(8))
                        break;

                    if (cx >= px && cx < px + TASKBAR_PILL_W &&
                        cy >= THEME_PX(4) && cy < THEME_PX(4) + TASKBAR_PILL_H) {
                        if (st->shell.start_menu_open)
                            shell_toggle_start_menu(st);

                        int sfd = vanilla_client_get_fd(st->client);
                        vanilla_msg_hdr_t hdr;
                        hdr.magic = VANILLA_IPC_MAGIC;
                        if (!we->is_mapped) {
                            vanilla_msg_map_window_t m_req;
                            hdr.msg_type = MSG_MAP_WINDOW;
                            hdr.payload_len = (uint16_t)sizeof(m_req);
                            hdr.window_id = we->window_id;
                            m_req.window_id = we->window_id;
                            if (sfd >= 0) {
                                write(sfd, &hdr, sizeof(hdr));
                                write(sfd, &m_req, sizeof(m_req));
                            }
                            we->is_mapped = 1;
                            we->is_focused = 1;
                        } else if (we->is_focused) {
                            vanilla_msg_unmap_window_t u_req;
                            hdr.msg_type = MSG_UNMAP_WINDOW;
                            hdr.payload_len = (uint16_t)sizeof(u_req);
                            hdr.window_id = we->window_id;
                            u_req.window_id = we->window_id;
                            if (sfd >= 0) {
                                write(sfd, &hdr, sizeof(hdr));
                                write(sfd, &u_req, sizeof(u_req));
                            }
                            we->is_mapped = 0;
                            we->is_focused = 0;
                        } else {
                            vanilla_msg_map_window_t m_req;
                            hdr.msg_type = MSG_MAP_WINDOW;
                            hdr.payload_len = (uint16_t)sizeof(m_req);
                            hdr.window_id = we->window_id;
                            m_req.window_id = we->window_id;
                            if (sfd >= 0) {
                                write(sfd, &hdr, sizeof(hdr));
                                write(sfd, &m_req, sizeof(m_req));
                            }
                            we->is_focused = 1;
                        }
                        st->taskbar_dirty = 1;
                        return;
                    }
                    pill_idx++;
                }

                if (st->shell.start_menu_open)
                    shell_toggle_start_menu(st);
            }
            return;
        }

        /* Start Menu popup window events */
        if (st->shell.start_menu_win && ev->window_id == st->shell.start_menu_win->window_id) {
            if (iev->type == EV_KEY && iev->code == BTN_LEFT && iev->value == 1) {
                int32_t cy = (int32_t)iev->pad2;
                int rel_y = cy - THEME_PX(12);
                if (rel_y >= 0) {
                    int item_idx = rel_y / START_MENU_ITEM_H;
                    if (item_idx >= 0 && item_idx < START_MENU_NUM_ITEMS) {
                        shell_spawn_app(g_start_menu_items[item_idx].path);
                        shell_toggle_start_menu(st);
                        return;
                    }
                }
            } else if (iev->type == EV_KEY && iev->value == 1) {
                if (iev->code == KEY_ESC) {
                    shell_toggle_start_menu(st);
                } else if (iev->code == KEY_UP) {
                    if (st->shell.selected_idx > 0) {
                        st->shell.selected_idx--;
                        st->shell.start_menu_dirty = 1;
                    }
                } else if (iev->code == KEY_DOWN) {
                    if (st->shell.selected_idx + 1 < START_MENU_NUM_ITEMS) {
                        st->shell.selected_idx++;
                        st->shell.start_menu_dirty = 1;
                    }
                } else if (iev->code == KEY_ENTER || iev->code == KEY_KPENTER || iev->code == KEY_SPACE) {
                    int idx = st->shell.selected_idx;
                    if (idx >= 0 && idx < START_MENU_NUM_ITEMS)
                        shell_spawn_app(g_start_menu_items[idx].path);
                    shell_toggle_start_menu(st);
                }
            }
            return;
        }

        /* Quick Launcher popup window events */
        if (st->launcher.win && ev->window_id == st->launcher.win->window_id) {
            if (iev->type == EV_KEY && iev->code == BTN_LEFT && iev->value == 1) {
                int32_t cx = (int32_t)iev->pad1;
                int32_t cy = (int32_t)iev->pad2;

                int32_t item_start_y = THEME_PX(54);
                for (int i = 0; i < st->launcher.match_count && i < 5; i++) {
                    int32_t iy = item_start_y + i * LAUNCHER_ITEM_HEIGHT;
                    if (cx >= THEME_PX(12) && cx < LAUNCHER_WIDTH - THEME_PX(12) &&
                        cy >= iy && cy < iy + LAUNCHER_ITEM_HEIGHT) {
                        st->launcher.selected_idx = i;
                        launcher_exec_selected(&st->launcher);
                        shell_toggle_launcher(st);
                        return;
                    }
                }
            } else if (iev->type == EV_KEY) {
                launcher_handle_key_state(&st->launcher, iev->code, iev->value, st->mod_state);
                if (!st->launcher.visible) {
                    shell_toggle_launcher(st);
                }
            }
            return;
        }
    }
}

#ifndef TEST_SHELL_RUNNER
int main(int argc, char **argv)
{
    (void)argc;
    (void)argv;

    printf("[shell] Desktop shell client started\n");

    const char *sock_path = VANILLA_SOCKET_PATH;
    vanilla_client_t *client = NULL;

    for (int retry = 0; retry < 50; retry++) {
        client = vanilla_connect(sock_path);
        if (client)
            break;
        struct timespec ts = { 0, 10000000 };
        nanosleep(&ts, NULL);
    }

    if (!client) {
        printf("[shell] Fatal: Failed to connect to display server at %s\n", sock_path);
        return 1;
    }

    shell_state_t state;
    memset(&state, 0, sizeof(state));
    state.client = client;
    state.screen_w = 1024;
    state.screen_h = 768;
    state.running = 1;
    state.taskbar_dirty = 1;

    shell_init(&state.shell);
    launcher_init(&state.launcher);

    state.taskbar_win = vanilla_create_window(client, "Taskbar", 0, state.screen_h - TASKBAR_HEIGHT,
                                              state.screen_w, TASKBAR_HEIGHT,
                                              WINDOW_FLAG_ALWAYS_TOP | WINDOW_FLAG_BORDERLESS | WINDOW_FLAG_TRANSPARENT);
    if (!state.taskbar_win) {
        printf("[shell] Fatal: Failed to create taskbar window\n");
        vanilla_disconnect(client);
        return 1;
    }

    vanilla_map_window(state.taskbar_win);
    shell_render_taskbar(&state);
    vanilla_present(state.taskbar_win, NULL);

    while (state.running) {
        vanilla_event_t ev;
        while (vanilla_poll_event(client, &ev) > 0) {
            shell_handle_event(&state, &ev);
        }

        shell_update_clock(&state.shell, NULL);

        if (state.taskbar_dirty && state.taskbar_win) {
            shell_render_taskbar(&state);
            vanilla_present(state.taskbar_win, NULL);
            state.taskbar_dirty = 0;
        }

        if (state.launcher.visible && state.launcher.dirty && state.launcher.win) {
            launcher_render_client(&state);
            vanilla_present(state.launcher.win, NULL);
            state.launcher.dirty = 0;
        }

        if (state.shell.start_menu_open && state.shell.start_menu_dirty && state.shell.start_menu_win) {
            start_menu_render_client(&state);
            vanilla_present(state.shell.start_menu_win, NULL);
            state.shell.start_menu_dirty = 0;
        }

        struct timespec ts = { 0, 5000000 };
        nanosleep(&ts, NULL);
    }

    if (state.launcher.win)
        vanilla_destroy_window(state.launcher.win);
    if (state.shell.start_menu_win)
        vanilla_destroy_window(state.shell.start_menu_win);
    if (state.taskbar_win)
        vanilla_destroy_window(state.taskbar_win);

    vanilla_disconnect(client);
    return 0;
}
#endif
