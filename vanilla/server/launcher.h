/*
 * Project Tsukasa — Vanilla Quick Launcher and Fuzzy Search Header
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

#ifndef _VANILLA_LAUNCHER_H
#define _VANILLA_LAUNCHER_H

#include <stdint.h>
#include <sys/input.h>
#include <sys/types.h>
#include "../include/surface.h"

#if defined(_WIN32)
pid_t spawn(const char *path, char *const argv[], char *const envp[]);
#endif

#define LAUNCHER_WIDTH        440
#define LAUNCHER_HEIGHT       280
#define LAUNCHER_SEARCH_MAX   64
#define LAUNCHER_MAX_APPS     16
#define LAUNCHER_ITEM_HEIGHT  36

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
} vanilla_launcher_t;

struct vanilla_server;

void launcher_init(vanilla_launcher_t *launcher);
void launcher_set_visible(struct vanilla_server *srv, int visible);
void launcher_toggle(struct vanilla_server *srv);
int  launcher_handle_key(struct vanilla_server *srv, uint16_t code, int pressed);
void launcher_render(struct vanilla_server *srv, const vanilla_rect_t *dirty);
int  launcher_handle_click(struct vanilla_server *srv, int32_t x, int32_t y, uint32_t button);
void launcher_invalidate(struct vanilla_server *srv);
int  launcher_fuzzy_match(const char *pattern, const char *target);
char launcher_evdev_to_ascii(uint16_t code, int shift);
void launcher_exec_selected(struct vanilla_server *srv);

#endif /* _VANILLA_LAUNCHER_H */
