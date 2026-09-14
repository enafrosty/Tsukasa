/*
 * Project Tsukasa — Vanilla Quick Launcher and Fuzzy Search Implementation
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

#include "launcher.h"
#include "server.h"
#include "blitter.h"
#include "font.h"
#include "shell.h"

#include <unistd.h>
#include <string.h>
#include <stdio.h>

static const vanilla_app_entry_t g_launcher_apps[] = {
    { "Terminal",        "Tsukasa interactive shell",  "/bin/terminal.elf", 0xFF5E81AC },
    { "Notepad",         "Simple text editor",         "/bin/notepad.elf",  0xFFA3BE8C },
    { "Calculator",      "Basic math calculator",      "/bin/calc.elf",     0xFFEBCB8B },
    { "File Manager",    "Browse directory files",     "/bin/filemgr.elf",  0xFFD08770 },
    { "Task Manager",    "Inspect and manage processes", "/bin/taskmgr.elf", 0xFF81A1C1 },
    { "System Fetch",    "Display system information", "/bin/sysfetch",     0xFFB48EAD },
    { "Settings",        "Desktop and system config",  "/bin/settings.elf", 0xFF88C0D0 },
    { "Process Viewer",  "Inspect running processes",  "/bin/ps",           0xFF81A1C1 },
    { "Network Info",    "Network status and sockets", "/bin/net",          0xFF8FBCBB },
};
#define LAUNCHER_NUM_BUILTIN_APPS (int)(sizeof(g_launcher_apps) / sizeof(g_launcher_apps[0]))

static inline char to_lower(char c)
{
    return (c >= 'A' && c <= 'Z') ? (c + 32) : c;
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

            /* Word boundary bonus: start of string or preceded by delimiter */
            if (t_idx == 0 || target[t_idx - 1] == ' ' || target[t_idx - 1] == '-' ||
                target[t_idx - 1] == '_' || target[t_idx - 1] == '/') {
                score += 20;
            }

            /* Consecutive match bonus */
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

static void launcher_update_matches(vanilla_launcher_t *launcher)
{
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

    /* Insertion sort descending by score */
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

char launcher_evdev_to_ascii(uint16_t code, int shift)
{
    if (code >= KEY_1 && code <= KEY_9) {
        static const char num_normal[] = "123456789";
        static const char num_shift[]  = "!@#$%^&*(";
        return shift ? num_shift[code - KEY_1] : num_normal[code - KEY_1];
    }
    if (code == KEY_0)
        return shift ? ')' : '0';

    if (code >= KEY_Q && code <= KEY_P) {
        static const char row1[] = "qwertyuiop";
        char c = row1[code - KEY_Q];
        return shift ? (c - 32) : c;
    }
    if (code >= KEY_A && code <= KEY_L) {
        static const char row2[] = "asdfghjkl";
        char c = row2[code - KEY_A];
        return shift ? (c - 32) : c;
    }
    if (code >= KEY_Z && code <= KEY_M) {
        static const char row3[] = "zxcvbnm";
        char c = row3[code - KEY_Z];
        return shift ? (c - 32) : c;
    }

    switch (code) {
    case KEY_SPACE:      return ' ';
    case KEY_MINUS:      return shift ? '_' : '-';
    case KEY_EQUAL:      return shift ? '+' : '=';
    case KEY_LEFTBRACE:  return shift ? '{' : '[';
    case KEY_RIGHTBRACE: return shift ? '}' : ']';
    case KEY_SEMICOLON:  return shift ? ':' : ';';
    case KEY_APOSTROPHE: return shift ? '"' : '\'';
    case KEY_GRAVE:      return shift ? '~' : '`';
    case KEY_BACKSLASH:  return shift ? '|' : '\\';
    case KEY_COMMA:      return shift ? '<' : ',';
    case KEY_DOT:        return shift ? '>' : '.';
    case KEY_SLASH:      return shift ? '?' : '/';
    default:             return 0;
    }
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
    launcher_update_matches(launcher);
}

void launcher_invalidate(vanilla_server_t *srv)
{
    if (!srv)
        return;

    int32_t screen_w = (int32_t)srv->compositor.width;
    int32_t screen_h = (int32_t)srv->compositor.height;
    int32_t lx = (screen_w - LAUNCHER_WIDTH) / 2;
    int32_t ly = (screen_h - TASKBAR_HEIGHT - LAUNCHER_HEIGHT) / 2;
    if (ly < 20)
        ly = 20;

    vanilla_rect_t r = {
        lx - 16,
        ly - 16,
        LAUNCHER_WIDTH + 32,
        LAUNCHER_HEIGHT + 32
    };
    compositor_add_damage(&srv->compositor, &r);
}

void launcher_set_visible(vanilla_server_t *srv, int visible)
{
    if (!srv || srv->launcher.visible == visible)
        return;

    launcher_invalidate(srv);
    srv->launcher.visible = visible;

    if (visible) {
        srv->launcher.query[0] = '\0';
        srv->launcher.query_len = 0;
        srv->launcher.selected_idx = 0;
        launcher_update_matches(&srv->launcher);
    }

    launcher_invalidate(srv);
    shell_invalidate(srv);
}

void launcher_toggle(vanilla_server_t *srv)
{
    if (!srv)
        return;
    launcher_set_visible(srv, !srv->launcher.visible);
}

int launcher_handle_key(vanilla_server_t *srv, uint16_t code, int pressed)
{
    if (!srv)
        return 0;

    if (!srv->launcher.visible) {
        if (pressed && srv->alt_pressed && code == KEY_SPACE) {
            launcher_set_visible(srv, 1);
            return 1;
        }
        return 0;
    }

    if (!pressed)
        return 1;

    if (code == KEY_ESC) {
        launcher_set_visible(srv, 0);
        return 1;
    }

    if (code == KEY_ENTER) {
        launcher_exec_selected(srv);
        return 1;
    }

    if (code == KEY_UP) {
        if (srv->launcher.selected_idx > 0) {
            srv->launcher.selected_idx--;
            launcher_invalidate(srv);
        }
        return 1;
    }

    if (code == KEY_DOWN) {
        if (srv->launcher.selected_idx + 1 < srv->launcher.match_count) {
            srv->launcher.selected_idx++;
            launcher_invalidate(srv);
        }
        return 1;
    }

    if (code == KEY_BACKSPACE) {
        if (srv->launcher.query_len > 0) {
            srv->launcher.query[--srv->launcher.query_len] = '\0';
            launcher_update_matches(&srv->launcher);
            launcher_invalidate(srv);
        }
        return 1;
    }

    char ch = launcher_evdev_to_ascii(code, srv->shift_pressed);
    if (ch != 0 && srv->launcher.query_len < LAUNCHER_SEARCH_MAX - 1) {
        srv->launcher.query[srv->launcher.query_len++] = ch;
        srv->launcher.query[srv->launcher.query_len] = '\0';
        launcher_update_matches(&srv->launcher);
        launcher_invalidate(srv);
        return 1;
    }

    return 1;
}

void launcher_exec_selected(vanilla_server_t *srv)
{
    if (!srv || !srv->launcher.visible)
        return;

    int sel = srv->launcher.selected_idx;
    if (sel >= 0 && sel < srv->launcher.match_count) {
        int app_idx = srv->launcher.matches[sel].app_index;
        const vanilla_app_entry_t *app = &g_launcher_apps[app_idx];

        launcher_set_visible(srv, 0);

        char current_path[256];
        strncpy(current_path, app->exec_path, sizeof(current_path) - 1);
        current_path[sizeof(current_path) - 1] = '\0';

        char *argv[] = { current_path, NULL };
        pid_t pid = spawn(current_path, argv, NULL);

        /* Fallback 1: if spawn fails and path does not end with .elf, append .elf */
        if (pid <= 0) {
            size_t len = strlen(current_path);
            if (len < 4 || strcmp(current_path + len - 4, ".elf") != 0) {
                if (len + 4 < sizeof(current_path)) {
                    strcat(current_path, ".elf");
                    pid = spawn(current_path, argv, NULL);
                }
            }
        }

        /* Fallback 2: if still failing, try prepending /fat12/ instead of /bin/ */
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
    } else {
        launcher_set_visible(srv, 0);
    }
}

int launcher_handle_click(vanilla_server_t *srv, int32_t x, int32_t y, uint32_t button)
{
    if (!srv || !srv->launcher.visible || button != BTN_LEFT)
        return 0;

    int32_t screen_w = (int32_t)srv->compositor.width;
    int32_t screen_h = (int32_t)srv->compositor.height;
    int32_t lx = (screen_w - LAUNCHER_WIDTH) / 2;
    int32_t ly = (screen_h - TASKBAR_HEIGHT - LAUNCHER_HEIGHT) / 2;
    if (ly < 20)
        ly = 20;

    if (x < lx || x >= lx + LAUNCHER_WIDTH || y < ly || y >= ly + LAUNCHER_HEIGHT) {
        launcher_set_visible(srv, 0);
        return 1;
    }

    int32_t item_start_y = ly + 56;
    for (int i = 0; i < srv->launcher.match_count && i < 5; i++) {
        int32_t iy = item_start_y + i * LAUNCHER_ITEM_HEIGHT;
        if (x >= lx + 12 && x < lx + LAUNCHER_WIDTH - 12 &&
            y >= iy && y < iy + LAUNCHER_ITEM_HEIGHT) {
            srv->launcher.selected_idx = i;
            launcher_exec_selected(srv);
            return 1;
        }
    }

    return 1;
}

void launcher_render(vanilla_server_t *srv, const vanilla_rect_t *dirty)
{
    if (!srv || !srv->launcher.visible || !dirty)
        return;

    vanilla_compositor_t *comp = &srv->compositor;
    int32_t screen_w = (int32_t)comp->width;
    int32_t screen_h = (int32_t)comp->height;
    int32_t lx = (screen_w - LAUNCHER_WIDTH) / 2;
    int32_t ly = (screen_h - TASKBAR_HEIGHT - LAUNCHER_HEIGHT) / 2;
    if (ly < 20)
        ly = 20;

    vanilla_rect_t modal_rect = { lx, ly, LAUNCHER_WIDTH, LAUNCHER_HEIGHT };
    vanilla_rect_t vis_modal;
    if (!vanilla_rect_intersect(&modal_rect, dirty, &vis_modal))
        return;

    /* Drop shadow behind modal */
    blt_drop_shadow(comp->backbuffer, comp->pitch_px, comp->width, comp->height, &modal_rect, dirty, 12, 160);

    /* Background panel: Nord Dark */
    blt_fill_rect(comp->backbuffer, comp->pitch_px, &vis_modal, 0xFF2E3440);

    /* 2px cyan border */
    vanilla_rect_t b_top = { lx, ly, LAUNCHER_WIDTH, 2 };
    vanilla_rect_t vis_b;
    if (vanilla_rect_intersect(&b_top, dirty, &vis_b))
        blt_fill_rect(comp->backbuffer, comp->pitch_px, &vis_b, 0xFF88C0D0);
    vanilla_rect_t b_bot = { lx, ly + LAUNCHER_HEIGHT - 2, LAUNCHER_WIDTH, 2 };
    if (vanilla_rect_intersect(&b_bot, dirty, &vis_b))
        blt_fill_rect(comp->backbuffer, comp->pitch_px, &vis_b, 0xFF88C0D0);
    vanilla_rect_t b_left = { lx, ly, 2, LAUNCHER_HEIGHT };
    if (vanilla_rect_intersect(&b_left, dirty, &vis_b))
        blt_fill_rect(comp->backbuffer, comp->pitch_px, &vis_b, 0xFF88C0D0);
    vanilla_rect_t b_right = { lx + LAUNCHER_WIDTH - 2, ly, 2, LAUNCHER_HEIGHT };
    if (vanilla_rect_intersect(&b_right, dirty, &vis_b))
        blt_fill_rect(comp->backbuffer, comp->pitch_px, &vis_b, 0xFF88C0D0);

    /* Search input box */
    vanilla_rect_t sbox = { lx + 12, ly + 12, LAUNCHER_WIDTH - 24, 32 };
    vanilla_rect_t vis_sbox;
    if (vanilla_rect_intersect(&sbox, dirty, &vis_sbox)) {
        blt_fill_rect(comp->backbuffer, comp->pitch_px, &vis_sbox, 0xFF3B4252);
        if (comp->font.info) {
            if (srv->launcher.query_len > 0) {
                font_draw_text(comp->backbuffer, comp->pitch_px, &vis_sbox, &comp->font,
                               srv->launcher.query, sbox.x + 8, sbox.y + 9, 13, 0xFFECEFF4);
            } else {
                font_draw_text(comp->backbuffer, comp->pitch_px, &vis_sbox, &comp->font,
                               "Type to search apps...", sbox.x + 8, sbox.y + 9, 13, 0xFF4C566A);
            }
        }
    }

    /* Result list (max 5 items shown) */
    int32_t item_start_y = ly + 54;
    for (int i = 0; i < srv->launcher.match_count && i < 5; i++) {
        int app_idx = srv->launcher.matches[i].app_index;
        const vanilla_app_entry_t *app = &g_launcher_apps[app_idx];
        int32_t iy = item_start_y + i * LAUNCHER_ITEM_HEIGHT;

        vanilla_rect_t item_rect = { lx + 12, iy, LAUNCHER_WIDTH - 24, LAUNCHER_ITEM_HEIGHT - 2 };
        vanilla_rect_t vis_item;
        if (!vanilla_rect_intersect(&item_rect, dirty, &vis_item))
            continue;

        uint32_t bg = (i == srv->launcher.selected_idx) ? 0xFF434C5E : 0xFF2E3440;
        blt_fill_rect(comp->backbuffer, comp->pitch_px, &vis_item, bg);

        /* Selection left accent line */
        if (i == srv->launcher.selected_idx) {
            vanilla_rect_t sel_acc = { item_rect.x, item_rect.y, 3, item_rect.h };
            vanilla_rect_t vis_acc;
            if (vanilla_rect_intersect(&sel_acc, dirty, &vis_acc))
                blt_fill_rect(comp->backbuffer, comp->pitch_px, &vis_acc, 0xFF88C0D0);
        }

        /* App icon badge */
        vanilla_rect_t icon_rect = { item_rect.x + 8, item_rect.y + 8, 16, 16 };
        vanilla_rect_t vis_icon;
        if (vanilla_rect_intersect(&icon_rect, dirty, &vis_icon))
            blt_fill_rect(comp->backbuffer, comp->pitch_px, &vis_icon, app->icon_color);

        if (comp->font.info) {
            /* App Name */
            font_draw_text(comp->backbuffer, comp->pitch_px, &vis_item, &comp->font,
                           app->name, item_rect.x + 32, item_rect.y + 9, 13, 0xFFECEFF4);

            /* App Description */
            font_draw_text(comp->backbuffer, comp->pitch_px, &vis_item, &comp->font,
                           app->description, item_rect.x + 140, item_rect.y + 9, 11, 0xFF98A2B3);
        }
    }
}
