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
#include "statusbar.h"
#include "dock.h"
#include "../server/server.h"
#include "../apps/app_common.h"
#include "../libvanilla/icon.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <unistd.h>
#include <time.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/ioctl.h>
#ifndef TEST_SHELL_RUNNER
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/poll.h>
#endif

#ifndef SHELL_SEPARATE_UNITS
#include "statusbar.c"
#include "dock.c"
#endif

typedef struct {
    const char *title;
    const char *path;
    const char *badge;
    icon_t     *icon;
} start_menu_item_t;

static start_menu_item_t g_start_menu_items[START_MENU_NUM_ITEMS] = {
    { "Terminal",     "/bin/terminal.elf", ">_", NULL },
    { "File Manager", "/bin/filemgr.elf",  "FM", NULL },
    { "Notepad",      "/bin/notepad.elf",  "NP", NULL },
    { "Calculator",   "/bin/calc.elf",     "+-", NULL },
    { "Task Manager", "/bin/taskmgr.elf",  "TM", NULL },
};

static const vanilla_app_entry_t g_launcher_apps[] = {
    { "Terminal",        "Tsukasa interactive shell",    "/bin/terminal.elf", 0 },
    { "Notepad",         "Simple text editor",           "/bin/notepad.elf",  0 },
    { "Calculator",      "Basic math calculator",        "/bin/calc.elf",     0 },
    { "File Manager",    "Browse directory files",       "/bin/filemgr.elf",  0 },
    { "Task Manager",    "Inspect and manage processes", "/bin/taskmgr.elf",  0 },
    { "Settings",        "Desktop and system config",    "/bin/settings.elf", 0 },
};
#define LAUNCHER_NUM_BUILTIN_APPS (int)(sizeof(g_launcher_apps) / sizeof(g_launcher_apps[0]))

static icon_t *g_launcher_icons[LAUNCHER_NUM_BUILTIN_APPS];
static int g_icons_loaded = 0;

#ifndef TEST_SHELL_RUNNER
static int shell_dbg_io_exact(int fd, void *buf, size_t count, int is_write)
{
    uint8_t *p = (uint8_t *)buf;
    size_t done = 0;
    int retries = 0;

    while (done < count) {
        ssize_t n = is_write ? write(fd, p + done, count - done)
                             : read(fd, p + done, count - done);
        if (n > 0) {
            done += (size_t)n;
        } else if (n == 0) {
            return -1;
        } else if (errno == EAGAIN || errno == EINTR) {
            struct pollfd pfd;
            pfd.fd = fd;
            pfd.events = is_write ? POLLOUT : POLLIN;
            pfd.revents = 0;
            if (++retries > 20 || poll(&pfd, 1, 50) <= 0)
                return -1;
        } else {
            return -1;
        }
    }
    return 0;
}

static int shell_dbg_discard_exact(int fd, size_t count)
{
    uint8_t buf[128];
    while (count > 0) {
        size_t chunk = count < sizeof(buf) ? count : sizeof(buf);
        if (shell_dbg_io_exact(fd, buf, chunk, 0) < 0)
            return -1;
        count -= chunk;
    }
    return 0;
}

static int shell_query_window_title(uint32_t win_id, char *out_title, size_t out_cap)
{
    if (!out_title || out_cap == 0 || win_id == 0)
        return -1;

    int fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (fd < 0)
        return -1;

    struct sockaddr_un addr;
    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, VANILLA_SOCKET_PATH, sizeof(addr.sun_path) - 1);

    if (connect(fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        close(fd);
        return -1;
    }

    vanilla_msg_hdr_t hdr;
    vanilla_msg_hello_t hello;
    memset(&hdr, 0, sizeof(hdr));
    memset(&hello, 0, sizeof(hello));
    hdr.magic = VANILLA_IPC_MAGIC;
    hdr.msg_type = MSG_HELLO;
    hdr.payload_len = (uint16_t)sizeof(hello);
    hello.client_version = VANILLA_IPC_VERSION;
    strncpy(hello.client_name, "shell-title", sizeof(hello.client_name) - 1);

    if (shell_dbg_io_exact(fd, &hdr, sizeof(hdr), 1) < 0 ||
        shell_dbg_io_exact(fd, &hello, sizeof(hello), 1) < 0) {
        close(fd);
        return -1;
    }

    vanilla_msg_hdr_t ack_hdr;
    vanilla_msg_hello_ack_t ack_body;
    int hello_skipped = 0;
    for (;;) {
        if (shell_dbg_io_exact(fd, &ack_hdr, sizeof(ack_hdr), 0) < 0 ||
            ack_hdr.magic != VANILLA_IPC_MAGIC) {
            close(fd);
            return -1;
        }
        if (ack_hdr.msg_type == MSG_HELLO_ACK)
            break;
        if (ack_hdr.payload_len > 0 && shell_dbg_discard_exact(fd, ack_hdr.payload_len) < 0) {
            close(fd);
            return -1;
        }
        if (++hello_skipped > 64) {
            close(fd);
            return -1;
        }
    }
    if (ack_hdr.payload_len < sizeof(ack_body) ||
        shell_dbg_io_exact(fd, &ack_body, sizeof(ack_body), 0) < 0) {
        close(fd);
        return -1;
    }
    if (ack_hdr.payload_len > sizeof(ack_body) &&
        shell_dbg_discard_exact(fd, ack_hdr.payload_len - sizeof(ack_body)) < 0) {
        close(fd);
        return -1;
    }

    vanilla_msg_debug_query_t qpayload;
    memset(&hdr, 0, sizeof(hdr));
    hdr.magic = VANILLA_IPC_MAGIC;
    hdr.msg_type = MSG_DEBUG_QUERY;
    hdr.payload_len = (uint16_t)sizeof(qpayload);
    qpayload.query_type = DBGQ_WINDOWS;

    if (shell_dbg_io_exact(fd, &hdr, sizeof(hdr), 1) < 0 ||
        shell_dbg_io_exact(fd, &qpayload, sizeof(qpayload), 1) < 0) {
        close(fd);
        return -1;
    }

    int skipped = 0;
    for (;;) {
        vanilla_msg_hdr_t rhdr;
        if (shell_dbg_io_exact(fd, &rhdr, sizeof(rhdr), 0) < 0 ||
            rhdr.magic != VANILLA_IPC_MAGIC) {
            close(fd);
            return -1;
        }
        if (rhdr.msg_type != MSG_DEBUG_QUERY_RESP) {
            if (rhdr.payload_len > 0 && shell_dbg_discard_exact(fd, rhdr.payload_len) < 0) {
                close(fd);
                return -1;
            }
            if (++skipped > 64) {
                close(fd);
                return -1;
            }
            continue;
        }

        vanilla_msg_debug_query_resp_t rpayload;
        if (rhdr.payload_len < sizeof(rpayload) ||
            shell_dbg_io_exact(fd, &rpayload, sizeof(rpayload), 0) < 0) {
            close(fd);
            return -1;
        }

        size_t rem = rhdr.payload_len - sizeof(rpayload);
        int found = -1;
        for (uint32_t i = 0; i < rpayload.window_count && rem >= sizeof(vanilla_dbg_window_t); i++) {
            vanilla_dbg_window_t rec;
            if (shell_dbg_io_exact(fd, &rec, sizeof(rec), 0) < 0) {
                close(fd);
                return -1;
            }
            rem -= sizeof(rec);
            if (found != 0 && rec.window_id == win_id && rec.title[0] != '\0') {
                size_t len = strlen(rec.title);
                if (len >= out_cap)
                    len = out_cap - 1;
                memcpy(out_title, rec.title, len);
                out_title[len] = '\0';
                found = 0;
            }
        }
        if (rem > 0)
            (void)shell_dbg_discard_exact(fd, rem);
        close(fd);
        return found;
    }
}
#else
static int shell_query_window_title(uint32_t win_id, char *out_title, size_t out_cap)
{
    (void)win_id;
    (void)out_title;
    (void)out_cap;
    return -1;
}
#endif

static void shell_load_icons(void)
{
    if (g_icons_loaded)
        return;

    for (int i = 0; i < START_MENU_NUM_ITEMS; i++) {
        g_start_menu_items[i].icon = icon_load(g_start_menu_items[i].title);
    }

    for (int i = 0; i < LAUNCHER_NUM_BUILTIN_APPS; i++) {
        g_launcher_icons[i] = icon_load(g_launcher_apps[i].name);
    }

    g_icons_loaded = 1;
}

void shell_free_icons(void)
{
    if (!g_icons_loaded)
        return;

    for (int i = 0; i < START_MENU_NUM_ITEMS; i++) {
        if (g_start_menu_items[i].icon) {
            icon_free(g_start_menu_items[i].icon);
            g_start_menu_items[i].icon = NULL;
        }
    }

    for (int i = 0; i < LAUNCHER_NUM_BUILTIN_APPS; i++) {
        if (g_launcher_icons[i]) {
            icon_free(g_launcher_icons[i]);
            g_launcher_icons[i] = NULL;
        }
    }

    g_icons_loaded = 0;
}

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

typedef struct {
    char   app_name[32];
    char   exec_name[32];
    pid_t  pid;
    time_t spawn_time;
} shell_pending_spawn_t;

static shell_pending_spawn_t g_pending_spawn;

static void shell_record_pending_spawn(const char *path)
{
    memset(&g_pending_spawn, 0, sizeof(g_pending_spawn));
    if (!path || !path[0])
        return;

    const char *leaf = path;
    for (const char *p = path; *p; p++) {
        if (*p == '/' || *p == '\\')
            leaf = p + 1;
    }
    strncpy(g_pending_spawn.exec_name, leaf, sizeof(g_pending_spawn.exec_name) - 1);

    if (shell_strcasestr(leaf, "terminal") || shell_strcasestr(leaf, "term"))
        strncpy(g_pending_spawn.app_name, "Terminal", sizeof(g_pending_spawn.app_name) - 1);
    else if (shell_strcasestr(leaf, "filemgr") || shell_strcasestr(leaf, "file"))
        strncpy(g_pending_spawn.app_name, "File Manager", sizeof(g_pending_spawn.app_name) - 1);
    else if (shell_strcasestr(leaf, "notepad"))
        strncpy(g_pending_spawn.app_name, "Notepad", sizeof(g_pending_spawn.app_name) - 1);
    else if (shell_strcasestr(leaf, "calc"))
        strncpy(g_pending_spawn.app_name, "Calculator", sizeof(g_pending_spawn.app_name) - 1);
    else if (shell_strcasestr(leaf, "settings"))
        strncpy(g_pending_spawn.app_name, "Settings", sizeof(g_pending_spawn.app_name) - 1);
    else if (shell_strcasestr(leaf, "taskmgr"))
        strncpy(g_pending_spawn.app_name, "Task Manager", sizeof(g_pending_spawn.app_name) - 1);
    else if (shell_strcasestr(leaf, "sysfetch"))
        strncpy(g_pending_spawn.app_name, "System Fetch", sizeof(g_pending_spawn.app_name) - 1);
    else if (shell_strcasestr(leaf, "ps"))
        strncpy(g_pending_spawn.app_name, "Process Viewer", sizeof(g_pending_spawn.app_name) - 1);
    else if (shell_strcasestr(leaf, "net"))
        strncpy(g_pending_spawn.app_name, "Network Info", sizeof(g_pending_spawn.app_name) - 1);
    else {
        strncpy(g_pending_spawn.app_name, leaf, sizeof(g_pending_spawn.app_name) - 1);
        char *dot = strrchr(g_pending_spawn.app_name, '.');
        if (dot)
            *dot = '\0';
        if (g_pending_spawn.app_name[0] >= 'a' && g_pending_spawn.app_name[0] <= 'z')
            g_pending_spawn.app_name[0] = (char)(g_pending_spawn.app_name[0] - 32);
    }
    g_pending_spawn.spawn_time = time(NULL);
}

pid_t shell_spawn_app(const char *path)
{
    if (!path || !path[0])
        return -1;

    shell_record_pending_spawn(path);

    char current_path[256];
    strncpy(current_path, path, sizeof(current_path) - 1);
    current_path[sizeof(current_path) - 1] = '\0';

    char *argv[] = { current_path, NULL };
    pid_t pid = spawn(current_path, argv, NULL);

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
    }
    if (pid > 0)
        g_pending_spawn.pid = pid;
    return pid;
}

static void launcher_exec_selected(vanilla_launcher_t *launcher)
{
    if (!launcher)
        return;

    int sel = launcher->selected_idx;
    if (sel >= 0 && sel < launcher->match_count) {
        int app_idx = launcher->matches[sel].app_index;
        const vanilla_app_entry_t *app = &g_launcher_apps[app_idx];

        pid_t pid = shell_spawn_app(app->exec_path);
        if (pid <= 0) {
            size_t len = strlen(app->exec_path);
            if (len < 4 || strcmp(app->exec_path + len - 4, ".elf") != 0) {
                char elf_path[256];
                snprintf(elf_path, sizeof(elf_path), "%s.elf", app->exec_path);
                shell_spawn_app(elf_path);
            }
        }
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

    shell_load_icons();

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
        struct tm tm_val;
#if defined(_WIN32)
        gmtime_s(&tm_val, &now);
#else
        gmtime_r(&now, &tm_val);
#endif
        char buf[16];
        snprintf(buf, sizeof(buf), "%02d:%02d", tm_val.tm_hour, tm_val.tm_min);

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
    float btn_sz = THEME_F(12.0f);
    int32_t stw = 0;
    int32_t sth = 0;
    app_measure_text_ex("Start", btn_sz, APP_FONT_BOLD, &stw, &sth);
    app_draw_text_ex(surf, TASKBAR_START_X + (TASKBAR_START_W - stw) / 2,
                     THEME_PX(4) + (TASKBAR_START_H - sth) / 2,
                     "Start", btn_sz, APP_FONT_BOLD, g_theme->taskbar_text);

    /* Window pills */
    int pill_idx = 0;
    float pill_sz = THEME_F(12.0f);
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
        int32_t pth = 0;
        app_measure_text_ex(title, pill_sz, APP_FONT_MEDIUM, NULL, &pth);
        vanilla_rect_t pclip = { px + THEME_PX(6), THEME_PX(4), TASKBAR_PILL_W - THEME_PX(12), TASKBAR_PILL_H };
        app_draw_text_clipped(surf, &pclip, px + THEME_PX(8),
                              THEME_PX(4) + (TASKBAR_PILL_H - pth) / 2,
                              title, pill_sz, APP_FONT_MEDIUM, fg);

        pill_idx++;
    }

    /* System tray digital clock */
    int32_t clock_x = w - TASKBAR_CLOCK_W - THEME_PX(4);
    int32_t clock_y = THEME_PX(4);
    app_fill_rect(surf, clock_x, clock_y, TASKBAR_CLOCK_W, TASKBAR_CLOCK_H, g_theme->bg_base);
    float clk_sz = THEME_F(12.0f);
    int32_t ctw = 0;
    int32_t cth = 0;
    app_measure_text_ex(st->shell.clock_str, clk_sz, APP_FONT_MEDIUM, &ctw, &cth);
    app_draw_text_ex(surf, clock_x + (TASKBAR_CLOCK_W - ctw) / 2,
                     clock_y + (TASKBAR_CLOCK_H - cth) / 2,
                     st->shell.clock_str, clk_sz, APP_FONT_MEDIUM, g_theme->taskbar_text);
}

static void start_menu_render_client(shell_state_t *st)
{
    if (!st || !st->shell.start_menu_win || !st->shell.start_menu_win->surface.pixels)
        return;

    vanilla_surface_t *surf = &st->shell.start_menu_win->surface;
    int32_t w = (int32_t)surf->width;
    int32_t h = (int32_t)surf->height;

    /* Sharp 0px menu panel surface contrast (MENU_RADIUS_PX == 0) */
    uint32_t menu_bg = shell_menu_bg();
    uint32_t border_col = shell_menu_border();
    app_fill_rect(surf, 0, 0, w, h, menu_bg);

    int32_t bw = g_theme->border_width > 0 ? g_theme->border_width : 1;
    app_fill_rect(surf, 0, 0, w, bw, border_col);
    app_fill_rect(surf, w - bw, 0, bw, h, border_col);
    app_fill_rect(surf, 0, 0, bw, h, border_col);
    app_fill_rect(surf, 0, h - bw, w, bw, border_col);

    /* Section header and 1px divider */
    float hdr_sz = THEME_F(11.0f);
    int32_t hth = 0;
    app_measure_text_ex("Applications", hdr_sz, APP_FONT_BOLD, NULL, &hth);
    app_draw_text_ex(surf, THEME_PX(12), THEME_PX(6) + (START_MENU_HEADER_H - hth) / 2,
                     "Applications", hdr_sz, APP_FONT_BOLD, g_theme->fg_muted);

    int32_t div_y = THEME_PX(6) + START_MENU_HEADER_H + (START_MENU_SEP_H / 2);
    app_fill_rect(surf, THEME_PX(8), div_y, w - THEME_PX(16), 1, shell_menu_divider());

    /* Start Menu items */
    float title_sz = THEME_F(13.0f);
    float badge_sz = THEME_F(10.0f);
    uint32_t sel_bg = shell_menu_selection_bg(menu_bg);
    int32_t icon_sz = THEME_PX(18);

    for (int i = 0; i < START_MENU_NUM_ITEMS; i++) {
        const start_menu_item_t *item = &g_start_menu_items[i];
        int32_t item_y = START_MENU_ITEMS_Y + i * START_MENU_ITEM_H;
        int32_t item_w = w - THEME_PX(12);
        int32_t item_h = START_MENU_ITEM_H - THEME_PX(2);

        if (i == st->shell.selected_idx) {
            app_fill_rect(surf, THEME_PX(6), item_y, item_w, item_h, sel_bg);
            app_fill_rect(surf, THEME_PX(6), item_y, THEME_PX(3), item_h, g_theme->accent);
        }

        /* Item badge / icon */
        int32_t badge_x = THEME_PX(12);
        int32_t badge_y = item_y + (item_h - icon_sz) / 2;
        if (item->icon && icon_render_to_surface(item->icon, surf, badge_x, badge_y, icon_sz) == 0) {
            /* Vector or bitmap icon rendered */
        } else {
            app_fill_rect(surf, badge_x, badge_y, icon_sz, icon_sz, shell_get_badge_color(i));
            if (item->badge) {
                int32_t btw = 0;
                int32_t bth = 0;
                app_measure_text_ex(item->badge, badge_sz, APP_FONT_BOLD, &btw, &bth);
                app_draw_text_ex(surf, badge_x + (icon_sz - btw) / 2,
                                 badge_y + (icon_sz - bth) / 2,
                                 item->badge, badge_sz, APP_FONT_BOLD, g_theme->fg_primary);
            }
        }

        /* Item title */
        int32_t text_x = badge_x + icon_sz + THEME_PX(8);
        int32_t tth = 0;
        app_measure_text_ex(item->title, title_sz, APP_FONT_MEDIUM, NULL, &tth);
        vanilla_rect_t clip = { text_x, item_y, w - THEME_PX(10) - text_x, item_h };
        app_draw_text_clipped(surf, &clip, text_x, item_y + (item_h - tth) / 2,
                              item->title, title_sz, APP_FONT_MEDIUM, g_theme->fg_primary);
    }
}

static void launcher_render_client(shell_state_t *st)
{
    if (!st || !st->launcher.win || !st->launcher.win->surface.pixels)
        return;

    vanilla_surface_t *surf = &st->launcher.win->surface;
    int32_t w = (int32_t)surf->width;
    int32_t h = (int32_t)surf->height;

    /* Sharp 0px launcher popup contrast surface (MENU_RADIUS_PX == 0) */
    uint32_t menu_bg = shell_menu_bg();
    app_fill_rect(surf, 0, 0, w, h, menu_bg);

    /* Outer 1px border */
    app_draw_rect(surf, 0, 0, w, h, shell_menu_border());

    /* Search input box */
    int32_t sb_x = THEME_PX(12);
    int32_t sb_y = THEME_PX(12);
    int32_t sb_w = w - THEME_PX(24);
    int32_t sb_h = THEME_PX(32);
    app_fill_rect(surf, sb_x, sb_y, sb_w, sb_h, g_theme->bg_base);
    app_draw_rect(surf, sb_x, sb_y, sb_w, sb_h, g_theme->border_focus);

    float q_sz = THEME_F(13.0f);
    int32_t text_pad = THEME_PX(10);
    int32_t qw = 0;
    int32_t qh = 0;
    vanilla_rect_t qclip = { sb_x + text_pad, sb_y, sb_w - text_pad * 2, sb_h };

    if (st->launcher.query_len > 0) {
        app_measure_text_ex(st->launcher.query, q_sz, APP_FONT_MEDIUM, &qw, &qh);
        int32_t qy = sb_y + (sb_h - qh) / 2;
        app_draw_text_clipped(surf, &qclip, sb_x + text_pad, qy,
                              st->launcher.query, q_sz, APP_FONT_MEDIUM, g_theme->fg_primary);
        int32_t caret_x = sb_x + text_pad + qw + THEME_PX(2);
        if (caret_x + THEME_PX(2) < sb_x + sb_w - text_pad)
            app_fill_rect(surf, caret_x, sb_y + THEME_PX(7), THEME_PX(2), sb_h - THEME_PX(14), g_theme->accent);
    } else {
        const char *placeholder = "Type to search apps...";
        app_measure_text_ex(placeholder, q_sz, APP_FONT_REGULAR, NULL, &qh);
        int32_t py = sb_y + (sb_h - qh) / 2;
        app_draw_text_clipped(surf, &qclip, sb_x + text_pad, py,
                              placeholder, q_sz, APP_FONT_REGULAR, g_theme->fg_muted);
        app_fill_rect(surf, sb_x + text_pad, sb_y + THEME_PX(7), THEME_PX(2), sb_h - THEME_PX(14), g_theme->accent);
    }

    /* 1px divider separating search input from results */
    app_fill_rect(surf, sb_x, sb_y + sb_h + THEME_PX(5), sb_w, 1, shell_menu_divider());

    /* Result list (up to 5 items) */
    int32_t item_start_y = THEME_PX(54);
    float name_sz = THEME_F(13.0f);
    float desc_sz = THEME_F(12.0f);
    uint32_t sel_bg = shell_menu_selection_bg(menu_bg);

    if (st->launcher.match_count == 0) {
        const char *empty_msg = "No matching applications";
        int32_t ew = 0;
        int32_t eh = 0;
        app_measure_text_ex(empty_msg, name_sz, APP_FONT_REGULAR, &ew, &eh);
        app_draw_text_ex(surf, (w - ew) / 2, item_start_y + THEME_PX(28),
                         empty_msg, name_sz, APP_FONT_REGULAR, g_theme->fg_muted);
        return;
    }

    for (int i = 0; i < st->launcher.match_count && i < 5; i++) {
        int app_idx = st->launcher.matches[i].app_index;
        const vanilla_app_entry_t *app = &g_launcher_apps[app_idx];
        int32_t iy = item_start_y + i * LAUNCHER_ITEM_HEIGHT;
        int32_t iw = w - THEME_PX(24);
        int32_t ih = LAUNCHER_ITEM_HEIGHT - THEME_PX(2);

        uint32_t bg = (i == st->launcher.selected_idx) ? sel_bg : menu_bg;
        app_fill_rect(surf, sb_x, iy, iw, ih, bg);

        if (i == st->launcher.selected_idx)
            app_fill_rect(surf, sb_x, iy, THEME_PX(3), ih, g_theme->accent);

        /* Icon badge */
        int32_t ic_sz = THEME_PX(16);
        int32_t ic_x = sb_x + THEME_PX(8);
        int32_t ic_y = iy + (ih - ic_sz) / 2;
        icon_t *app_icon = (app_idx >= 0 && app_idx < LAUNCHER_NUM_BUILTIN_APPS) ? g_launcher_icons[app_idx] : NULL;
        if (app_icon && icon_render_to_surface(app_icon, surf, ic_x, ic_y, ic_sz) == 0) {
            /* Vector or bitmap icon rendered */
        } else {
            app_fill_rect(surf, ic_x, ic_y, ic_sz, ic_sz, launcher_get_icon_color(app_idx));
        }

        /* Non-overlapping clipped app name and description */
        int32_t name_x = sb_x + THEME_PX(32);
        int32_t name_w = THEME_PX(116);
        int32_t desc_x = name_x + name_w + THEME_PX(8);
        int32_t desc_w = (sb_x + iw - THEME_PX(8)) - desc_x;

        int32_t nh = 0;
        int32_t dh = 0;
        app_measure_text_ex(app->name, name_sz, APP_FONT_BOLD, NULL, &nh);
        app_measure_text_ex(app->description, desc_sz, APP_FONT_REGULAR, NULL, &dh);

        vanilla_rect_t nclip = { name_x, iy, name_w, ih };
        app_draw_text_clipped(surf, &nclip, name_x, iy + (ih - nh) / 2,
                              app->name, name_sz, APP_FONT_BOLD, g_theme->fg_primary);

        if (desc_w > THEME_PX(24)) {
            vanilla_rect_t dclip = { desc_x, iy, desc_w, ih };
            app_draw_text_clipped(surf, &dclip, desc_x, iy + (ih - dh) / 2,
                                  app->description, desc_sz, APP_FONT_REGULAR, g_theme->fg_muted);
        }
    }
}

void shell_close_launcher(shell_state_t *st)
{
    st->launcher.visible = 0;
    st->launcher.dirty = 0;
    if (st->launcher.win) {
        vanilla_unmap_window(st->launcher.win);
    }
}

void shell_close_start_menu(shell_state_t *st)
{
    st->shell.start_menu_open = 0;
    st->shell.start_menu_dirty = 0;
    if (st->shell.start_menu_win) {
        vanilla_unmap_window(st->shell.start_menu_win);
    }
    st->taskbar_dirty = 1;
}

static void shell_open_launcher(shell_state_t *st)
{
    if (st->launcher.visible && st->launcher.win)
        return;

    if (st->shell.start_menu_open)
        shell_close_start_menu(st);

    st->launcher.visible = 1;
    st->launcher.query[0] = '\0';
    st->launcher.query_len = 0;
    st->launcher.selected_idx = 0;
    launcher_update_matches(&st->launcher);

    if (st->client) {
        if (!st->launcher.win) {
            int32_t lx = (st->screen_w - LAUNCHER_WIDTH) / 2;
            int32_t ly = (st->screen_h - TASKBAR_HEIGHT - LAUNCHER_HEIGHT) / 2;
            if (ly < THEME_PX(20))
                ly = THEME_PX(20);

            st->launcher.win = vanilla_create_window(st->client, "Quick Launcher", lx, ly,
                                                     LAUNCHER_WIDTH, LAUNCHER_HEIGHT,
                                                     WINDOW_FLAG_POPUP | WINDOW_FLAG_ALWAYS_TOP | WINDOW_FLAG_BORDERLESS | WINDOW_FLAG_TRANSPARENT);
        }
        if (st->launcher.win) {
            vanilla_map_window(st->launcher.win);
            launcher_render_client(st);
            vanilla_present(st->launcher.win, NULL);
            st->launcher.dirty = 0;
        } else {
            st->launcher.dirty = 1;
        }
    }
}

static void shell_toggle_launcher(shell_state_t *st)
{
    if (st->launcher.visible) {
        shell_close_launcher(st);
    } else {
        shell_open_launcher(st);
    }
}

static void shell_open_start_menu(shell_state_t *st)
{
    if (st->shell.start_menu_open && st->shell.start_menu_win)
        return;

    if (st->launcher.visible)
        shell_close_launcher(st);

    st->shell.start_menu_open = 1;
    st->shell.selected_idx = 0;

    if (st->client) {
        if (!st->shell.start_menu_win) {
            int32_t sm_x = 0;
            int32_t sm_y = st->screen_h - TASKBAR_HEIGHT - START_MENU_HEIGHT;

            st->shell.start_menu_win = vanilla_create_window(st->client, "Start Menu", sm_x, sm_y,
                                                             START_MENU_WIDTH, START_MENU_HEIGHT,
                                                             WINDOW_FLAG_POPUP | WINDOW_FLAG_ALWAYS_TOP | WINDOW_FLAG_BORDERLESS | WINDOW_FLAG_TRANSPARENT);
        }
        if (st->shell.start_menu_win) {
            vanilla_map_window(st->shell.start_menu_win);
            start_menu_render_client(st);
            vanilla_present(st->shell.start_menu_win, NULL);
            st->shell.start_menu_dirty = 0;
        } else {
            st->shell.start_menu_dirty = 1;
        }
    }
    st->taskbar_dirty = 1;
}

static void shell_toggle_start_menu(shell_state_t *st)
{
    if (st->shell.start_menu_open) {
        shell_close_start_menu(st);
    } else {
        shell_open_start_menu(st);
    }
}



void shell_handle_event(shell_state_t *st, const vanilla_event_t *ev)
{
    if (!st || !ev)
        return;

    if (ev->type == VANILLA_EVENT_INPUT ||
        ev->type == VANILLA_EVENT_DOUBLE_CLICK ||
        ev->type == VANILLA_EVENT_TRIPLE_CLICK ||
        ev->type == VANILLA_EVENT_LONG_PRESS) {
        st->mod_state = ev->mod_state;
    }

    if (ev->type == VANILLA_EVENT_THEME_CHANGED) {
        if (theme_load("/tmp/vanilla_theme.ini") < 0)
            theme_reload();
        st->statusbar.dirty = 1;
        st->dock.dirty = 1;
        st->taskbar_dirty = 1;
        if (st->launcher.visible)
            st->launcher.dirty = 1;
        if (st->statusbar.menu_open)
            st->statusbar.menu_dirty = 1;
        if (st->dock.menu_open)
            st->dock.menu_dirty = 1;
        if (st->shell.start_menu_open)
            st->shell.start_menu_dirty = 1;
        return;
    }

    if (ev->type == VANILLA_EVENT_CLOSE_REQ) {
        if ((st->statusbar_win && ev->window_id == st->statusbar_win->window_id) ||
            (st->dock_win && ev->window_id == st->dock_win->window_id)) {
            st->running = 0;
        } else if (st->taskbar_win && ev->window_id == st->taskbar_win->window_id) {
            st->running = 0;
        } else if (st->launcher.win && ev->window_id == st->launcher.win->window_id) {
            shell_close_launcher(st);
        } else if (st->statusbar.menu_win && ev->window_id == st->statusbar.menu_win->window_id) {
            statusbar_close_menu(&st->statusbar);
        } else if (st->dock.menu_win && ev->window_id == st->dock.menu_win->window_id) {
            dock_close_menu(&st->dock);
        } else if (st->shell.start_menu_win && ev->window_id == st->shell.start_menu_win->window_id) {
            shell_close_start_menu(st);
        }
        return;
    }

    if (ev->type == VANILLA_EVENT_CONFIGURE) {
        if (st->statusbar_win && ev->window_id == st->statusbar_win->window_id) {
            if (ev->configure.width > 0 && ev->configure.height > 0) {
                if ((int32_t)ev->configure.width != st->screen_w) {
                    st->screen_w = (int32_t)ev->configure.width;
                    st->statusbar.dirty = 1;
                    int old_w = st->dock.width;
                    dock_recompute_geometry(&st->dock, st->screen_w, st->screen_h);
                    if (st->dock_win) {
                        if (st->dock.width != old_w) {
                            vanilla_destroy_window(st->dock_win);
                            st->dock_win = vanilla_create_window(st->client, "Dock", st->dock.x, st->dock.y,
                                                                 st->dock.width, st->dock.height,
                                                                 WINDOW_FLAG_ALWAYS_TOP | WINDOW_FLAG_BORDERLESS | WINDOW_FLAG_TRANSPARENT);
                            if (st->dock_win) {
                                st->dock.win = st->dock_win;
                                vanilla_map_window(st->dock_win);
                                st->dock.dirty = 1;
                            }
                        } else {
                            vanilla_move_window(st->dock_win, st->dock.x, st->dock.y);
                            st->dock.dirty = 1;
                        }
                    }
                }
            }
            return;
        }

        if (st->dock_win && ev->window_id == st->dock_win->window_id) {
            st->dock.dirty = 1;
            return;
        }

        if (st->taskbar_win && ev->window_id == st->taskbar_win->window_id) {
            if (ev->configure.width > 0 && ev->configure.height > 0) {
                if ((int32_t)ev->configure.width != st->screen_w) {
                    st->screen_w = (int32_t)ev->configure.width;
                    st->taskbar_dirty = 1;
                }
            }
            return;
        }

        if ((!st->launcher.win || ev->window_id != st->launcher.win->window_id) &&
            (!st->statusbar.menu_win || ev->window_id != st->statusbar.menu_win->window_id) &&
            (!st->dock.menu_win || ev->window_id != st->dock.menu_win->window_id) &&
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
                int existing_idx = -1;
                for (int i = 0; i < st->win_list.count; i++) {
                    if (st->win_list.windows[i].window_id == ev->window_id) {
                        st->win_list.windows[i].flags = ev->configure.flags;
                        found = 1;
                        existing_idx = i;
                        break;
                    }
                }
                if (found && existing_idx >= 0) {
                    shell_window_entry_t *we = &st->win_list.windows[existing_idx];
                    if (we->title[0] == '\0' || strcmp(we->title, "App") == 0) {
                        char queried[VANILLA_TITLE_MAX] = "";
                        if (shell_query_window_title(ev->window_id, queried, sizeof(queried)) == 0 &&
                            queried[0] != '\0') {
                            strncpy(we->title, queried, sizeof(we->title) - 1);
                            we->title[sizeof(we->title) - 1] = '\0';
                            if (we->is_focused)
                                statusbar_set_active_title(&st->statusbar, we->title);
                        }
                    }
                }
                if (!found && st->win_list.count < SHELL_MAX_WINDOWS) {
                    shell_window_entry_t *we = &st->win_list.windows[st->win_list.count++];
                    we->window_id = ev->window_id;
                    we->is_mapped = 1;
                    we->is_focused = 0;
                    we->is_minimized = 0;
                    we->flags = ev->configure.flags;

                    char app_title[VANILLA_TITLE_MAX] = "App";
                    char exec_name[32] = "";
                    int have_title = (shell_query_window_title(ev->window_id, app_title, sizeof(app_title)) == 0 &&
                                      app_title[0] != '\0');

                    time_t now = time(NULL);
                    if (!have_title && g_pending_spawn.app_name[0] && (now - g_pending_spawn.spawn_time) < 10) {
                        strncpy(app_title, g_pending_spawn.app_name, sizeof(app_title) - 1);
                        strncpy(exec_name, g_pending_spawn.exec_name, sizeof(exec_name) - 1);
                        memset(&g_pending_spawn, 0, sizeof(g_pending_spawn));
                    }

                    strncpy(we->title, app_title, sizeof(we->title) - 1);
                    we->title[sizeof(we->title) - 1] = '\0';
                    strncpy(we->exec_name, exec_name, sizeof(we->exec_name) - 1);
                    we->exec_name[sizeof(we->exec_name) - 1] = '\0';
                }
                st->taskbar_dirty = 1;
            }

            int old_dock_w = st->dock.width;
            int old_dock_x = st->dock.x;
            dock_sync_windows(&st->dock, &st->win_list, st->screen_w, st->screen_h);
            if (st->dock_win) {
                if (st->dock.width != old_dock_w) {
                    vanilla_destroy_window(st->dock_win);
                    st->dock_win = vanilla_create_window(st->client, "Dock", st->dock.x, st->dock.y,
                                                         st->dock.width, st->dock.height,
                                                         WINDOW_FLAG_ALWAYS_TOP | WINDOW_FLAG_BORDERLESS | WINDOW_FLAG_TRANSPARENT);
                    if (st->dock_win) {
                        st->dock.win = st->dock_win;
                        vanilla_map_window(st->dock_win);
                        st->dock.dirty = 1;
                    }
                } else if (st->dock.x != old_dock_x) {
                    vanilla_move_window(st->dock_win, st->dock.x, st->dock.y);
                }
            }
        }
        return;
    }

    if (ev->type == VANILLA_EVENT_FOCUS) {
        const char *focused_title = NULL;
        for (int i = 0; i < st->win_list.count; i++) {
            if (st->win_list.windows[i].window_id == ev->window_id) {
                st->win_list.windows[i].is_focused = ev->focus.focused;
                if (ev->focus.focused) {
                    st->win_list.windows[i].is_mapped = 1;
                    st->win_list.windows[i].is_minimized = 0;
                    if (st->win_list.windows[i].title[0] == '\0' ||
                        strcmp(st->win_list.windows[i].title, "App") == 0) {
                        char queried[VANILLA_TITLE_MAX] = "";
                        if (shell_query_window_title(ev->window_id, queried, sizeof(queried)) == 0 &&
                            queried[0] != '\0') {
                            strncpy(st->win_list.windows[i].title, queried,
                                    sizeof(st->win_list.windows[i].title) - 1);
                            st->win_list.windows[i].title[sizeof(st->win_list.windows[i].title) - 1] = '\0';
                        }
                    }
                    focused_title = st->win_list.windows[i].title;
                }
            } else if (ev->focus.focused) {
                st->win_list.windows[i].is_focused = 0;
            }
        }
        st->taskbar_dirty = 1;

        if (ev->focus.focused && focused_title) {
            statusbar_set_active_title(&st->statusbar, focused_title);
        }

        dock_sync_windows(&st->dock, &st->win_list, st->screen_w, st->screen_h);

        /* Dismiss popups when focus changes */
        if (ev->focus.focused) {
            if (st->launcher.visible && st->launcher.win && ev->window_id != st->launcher.win->window_id)
                shell_close_launcher(st);
            if (st->statusbar.menu_open && st->statusbar.menu_win && ev->window_id != st->statusbar.menu_win->window_id)
                statusbar_close_menu(&st->statusbar);
            if (st->dock.menu_open && st->dock.menu_win && ev->window_id != st->dock.menu_win->window_id)
                dock_close_menu(&st->dock);
            if (st->shell.start_menu_open && st->shell.start_menu_win && ev->window_id != st->shell.start_menu_win->window_id)
                shell_close_start_menu(st);
        } else {
            if (st->launcher.visible && st->launcher.win && ev->window_id == st->launcher.win->window_id)
                shell_close_launcher(st);
            if (st->statusbar.menu_open && st->statusbar.menu_win && ev->window_id == st->statusbar.menu_win->window_id)
                statusbar_close_menu(&st->statusbar);
            if (st->dock.menu_open && st->dock.menu_win && ev->window_id == st->dock.menu_win->window_id)
                dock_close_menu(&st->dock);
            if (st->shell.start_menu_open && st->shell.start_menu_win && ev->window_id == st->shell.start_menu_win->window_id)
                shell_close_start_menu(st);
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

        /* Super / Windows key Start / System Menu toggle */
        if (iev->type == EV_KEY && (iev->code == KEY_LEFTMETA || iev->code == KEY_RIGHTMETA) && iev->value == 1) {
            if (st->statusbar.win)
                statusbar_toggle_menu(&st->statusbar, st);
            else
                shell_toggle_start_menu(st);
            return;
        }

        /* Top status bar events */
        if (st->statusbar.win && ev->window_id == st->statusbar.win->window_id) {
            if (iev->type == EV_KEY && iev->code == BTN_LEFT && iev->value == 1) {
                int32_t cx = (int32_t)iev->pad1;
                int32_t cy = (int32_t)iev->pad2;
                statusbar_handle_mouse_click(&st->statusbar, cx, cy, st);
            }
            return;
        }

        /* Status bar System Menu events */
        if (st->statusbar.menu_win && ev->window_id == st->statusbar.menu_win->window_id) {
            if (iev->type == EV_KEY && iev->code == BTN_LEFT && iev->value == 1) {
                int32_t cx = (int32_t)iev->pad1;
                int32_t cy = (int32_t)iev->pad2;
                statusbar_handle_menu_click(&st->statusbar, cx, cy, st);
            } else if (iev->type == EV_KEY && iev->value == 1 && iev->code == KEY_ESC) {
                statusbar_close_menu(&st->statusbar);
            }
            return;
        }

        /* Floating Aurora Dock events */
        if (st->dock.win && ev->window_id == st->dock.win->window_id) {
            if (iev->type == EV_REL && iev->code != REL_WHEEL) {
                int32_t cx = (int32_t)iev->pad1;
                int32_t cy = (int32_t)iev->pad2;
                dock_handle_mouse_motion(&st->dock, cx, cy);
            } else if (iev->type == EV_KEY && (iev->code == BTN_LEFT || iev->code == BTN_RIGHT) && iev->value == 1) {
                int32_t cx = (int32_t)iev->pad1;
                int32_t cy = (int32_t)iev->pad2;
                dock_handle_mouse_click(&st->dock, cx, cy, iev->code, st);
            }
            return;
        }

        /* Dock context menu events */
        if (st->dock.menu_win && ev->window_id == st->dock.menu_win->window_id) {
            if (iev->type == EV_KEY && iev->code == BTN_LEFT && iev->value == 1) {
                int32_t cx = (int32_t)iev->pad1;
                int32_t cy = (int32_t)iev->pad2;
                dock_handle_menu_click(&st->dock, cx, cy, st);
            } else if (iev->type == EV_KEY && iev->value == 1 && iev->code == KEY_ESC) {
                dock_close_menu(&st->dock);
            }
            return;
        }

        /* Taskbar window events (preserved for test runner) */
        if (st->taskbar_win && ev->window_id == st->taskbar_win->window_id &&
            (!st->statusbar.win || ev->window_id != st->statusbar.win->window_id)) {
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
                            shell_close_start_menu(st);
                        if (st->launcher.visible)
                            shell_close_launcher(st);

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
                    shell_close_start_menu(st);
            }
            return;
        }

        /* Start Menu popup window events */
        if (st->shell.start_menu_win && ev->window_id == st->shell.start_menu_win->window_id) {
            if (iev->type == EV_KEY && iev->code == BTN_LEFT && iev->value == 1) {
                int32_t cy = (int32_t)iev->pad2;
                int rel_y = cy - START_MENU_ITEMS_Y;
                if (rel_y >= 0) {
                    int item_idx = rel_y / START_MENU_ITEM_H;
                    if (item_idx >= 0 && item_idx < START_MENU_NUM_ITEMS) {
                        shell_spawn_app(g_start_menu_items[item_idx].path);
                        shell_close_start_menu(st);
                        return;
                    }
                }
            } else if (iev->type == EV_KEY && iev->value == 1) {
                if (iev->code == KEY_ESC) {
                    shell_close_start_menu(st);
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
                    shell_close_start_menu(st);
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
                        shell_close_launcher(st);
                        return;
                    }
                }
            } else if (iev->type == EV_KEY) {
                launcher_handle_key_state(&st->launcher, iev->code, iev->value, st->mod_state);
                if (!st->launcher.visible) {
                    shell_close_launcher(st);
                }
            }
            return;
        }
    }
}

#ifndef TEST_SHELL_RUNNER
static int shell_compute_poll_timeout(const shell_state_t *st)
{
    if (!st || !st->running)
        return 0;

    if (vanilla_client_pending(st->client) > 0)
        return 0;

    if ((st->statusbar.dirty && st->statusbar_win) ||
        (st->dock.dirty && st->dock_win) ||
        (st->launcher.visible && st->launcher.dirty && st->launcher.win) ||
        (st->statusbar.menu_open && st->statusbar.menu_dirty && st->statusbar.menu_win) ||
        (st->dock.menu_open && st->dock.menu_dirty && st->dock.menu_win) ||
        (st->shell.start_menu_open && st->shell.start_menu_dirty && st->shell.start_menu_win)) {
        return 0;
    }

    /* Sleep up to 1000 ms for 1 Hz statusbar telemetry and clock refresh;
     * compositor events or input wake poll() immediately via POLLIN. */
    return 1000;
}

int main(int argc, char **argv)
{
    (void)argc;
    (void)argv;

    printf("[shell] Desktop modern shell client started\n");

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

#if !defined(_WIN32)
    int fb_fd = open("/dev/fb0", O_RDONLY);
    if (fb_fd >= 0) {
        struct fb_var_screeninfo_query {
            uint32_t xres, yres, xres_virtual, yres_virtual;
            uint32_t xoffset, yoffset;
            uint32_t bits_per_pixel;
        } vinfo;
        if (ioctl(fb_fd, 0x4600, &vinfo) == 0 && vinfo.xres > 0 && vinfo.yres > 0) {
            state.screen_w = (int32_t)vinfo.xres;
            state.screen_h = (int32_t)vinfo.yres;
        }
        close(fb_fd);
    }
#endif

    shell_init(&state.shell);
    launcher_init(&state.launcher);
    statusbar_init(&state.statusbar);
    dock_init(&state.dock);

    /* 1. Top status bar window: (0, 0, screen_w, 28) */
    state.statusbar_win = vanilla_create_window(client, "Statusbar", 0, 0,
                                               state.screen_w, STATUSBAR_HEIGHT,
                                               WINDOW_FLAG_ALWAYS_TOP | WINDOW_FLAG_BORDERLESS | WINDOW_FLAG_TRANSPARENT);
    if (!state.statusbar_win) {
        printf("[shell] Fatal: Failed to create status bar window\n");
        vanilla_disconnect(client);
        return 1;
    }
    state.statusbar.win = state.statusbar_win;

    /* 2. Floating dock window: centered horizontally, floating 12px above bottom edge */
    dock_recompute_geometry(&state.dock, state.screen_w, state.screen_h);
    state.dock_win = vanilla_create_window(client, "Dock", state.dock.x, state.dock.y,
                                          state.dock.width, state.dock.height,
                                          WINDOW_FLAG_ALWAYS_TOP | WINDOW_FLAG_BORDERLESS | WINDOW_FLAG_TRANSPARENT);
    if (!state.dock_win) {
        printf("[shell] Fatal: Failed to create dock window\n");
        vanilla_destroy_window(state.statusbar_win);
        vanilla_disconnect(client);
        return 1;
    }
    state.dock.win = state.dock_win;

    /* Backward compatibility alias pointer */
    state.taskbar_win = state.statusbar_win;

    vanilla_map_window(state.statusbar_win);
    statusbar_render(&state.statusbar, state.screen_w);
    vanilla_present(state.statusbar_win, NULL);
    state.statusbar.dirty = 0;

    vanilla_map_window(state.dock_win);
    dock_render(&state.dock);
    vanilla_present(state.dock_win, NULL);
    state.dock.dirty = 0;

    while (state.running) {
        vanilla_event_t ev;
        while (vanilla_poll_event(client, &ev) > 0) {
            shell_handle_event(&state, &ev);
        }

        /* Throttled system telemetry (strictly once per second) */
        statusbar_update_telemetry(&state.statusbar);
        shell_update_clock(&state.shell, NULL);

        if (state.statusbar.dirty && state.statusbar_win) {
            statusbar_render(&state.statusbar, state.screen_w);
            vanilla_present(state.statusbar_win, NULL);
            state.statusbar.dirty = 0;
        }

        if (state.dock.dirty && state.dock_win) {
            dock_render(&state.dock);
            vanilla_present(state.dock_win, NULL);
            state.dock.dirty = 0;
        }

        if (state.launcher.visible && state.launcher.dirty && state.launcher.win) {
            launcher_render_client(&state);
            vanilla_present(state.launcher.win, NULL);
            state.launcher.dirty = 0;
        }

        if (state.statusbar.menu_open && state.statusbar.menu_dirty && state.statusbar.menu_win) {
            statusbar_render_menu(&state.statusbar);
            vanilla_present(state.statusbar.menu_win, NULL);
            state.statusbar.menu_dirty = 0;
        }

        if (state.dock.menu_open && state.dock.menu_dirty && state.dock.menu_win) {
            dock_render_menu(&state.dock);
            vanilla_present(state.dock.menu_win, NULL);
            state.dock.menu_dirty = 0;
        }

        if (state.shell.start_menu_open && state.shell.start_menu_dirty && state.shell.start_menu_win) {
            start_menu_render_client(&state);
            vanilla_present(state.shell.start_menu_win, NULL);
            state.shell.start_menu_dirty = 0;
        }

        state.taskbar_dirty = 0;

        int timeout_ms = shell_compute_poll_timeout(&state);
        if (timeout_ms == 0)
            continue;

        int sfd = vanilla_client_get_fd(client);
        if (sfd < 0) {
            state.running = 0;
            break;
        }

        struct pollfd pfd;
        pfd.fd = sfd;
        pfd.events = POLLIN;
        pfd.revents = 0;

        int pr = poll(&pfd, 1, timeout_ms);
        if (pr < 0) {
            if (errno == EINTR)
                continue;
            state.running = 0;
            break;
        }
        if (pr > 0 && (pfd.revents & (POLLERR | POLLHUP)) && !(pfd.revents & POLLIN)) {
            state.running = 0;
            break;
        }
    }

    if (state.dock.menu_win)
        vanilla_destroy_window(state.dock.menu_win);
    if (state.statusbar.menu_win)
        vanilla_destroy_window(state.statusbar.menu_win);
    if (state.launcher.win)
        vanilla_destroy_window(state.launcher.win);
    if (state.shell.start_menu_win)
        vanilla_destroy_window(state.shell.start_menu_win);
    if (state.dock_win)
        vanilla_destroy_window(state.dock_win);
    if (state.statusbar_win)
        vanilla_destroy_window(state.statusbar_win);

    statusbar_free_icons(&state.statusbar);
    dock_free_icons(&state.dock);
    shell_free_icons();

    vanilla_disconnect(client);
    return 0;
}
#endif
