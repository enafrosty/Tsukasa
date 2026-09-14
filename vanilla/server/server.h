/*
 * Project Tsukasa — Vanilla Display Server State and Window Manager Header
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

#ifndef _VANILLA_SERVER_H
#define _VANILLA_SERVER_H

#include <stdint.h>
#include <stddef.h>
#include <sys/input.h>
#include "../include/protocol.h"
#include "../include/surface.h"
#include "compositor.h"
#include "shell.h"
#include "launcher.h"

#if defined(_WIN32)
#ifndef O_NONBLOCK
#define O_NONBLOCK 0x4000
#endif
int sched_yield(void);
#endif

#define VANILLA_MAX_CLIENTS 16
#define VANILLA_MAX_WINDOWS 64

typedef enum {
    LAYER_BACKGROUND = 0,
    LAYER_NORMAL     = 1,
    LAYER_TOPMOST    = 2,
    LAYER_OVERLAY    = 3,
} vanilla_layer_t;

typedef enum {
    SNAP_NONE     = 0,
    SNAP_MAXIMIZE = 1,
    SNAP_LEFT     = 2,
    SNAP_RIGHT    = 3,
} vanilla_snap_t;

typedef struct {
    int               in_use;
    uint32_t          window_id;
    int               client_fd;
    int               shm_id;
    vanilla_surface_t surface;
    int32_t           x;
    int32_t           y;
    uint32_t          width;
    uint32_t          height;
    uint32_t          flags;
    int32_t           z_index;
    int               layer;
    int               is_mapped;
    int               is_focused;
    char              title[VANILLA_TITLE_MAX];
    vanilla_rect_t    damage;

    /* Aero-snap floating state preservation */
    int               is_snapped;
    int32_t           restore_x;
    int32_t           restore_y;
    uint32_t          restore_w;
    uint32_t          restore_h;
} vanilla_server_window_t;

typedef struct {
    int in_use;
    int fd;
} vanilla_client_conn_t;

typedef struct vanilla_server {
    int                     listen_fd;
    char                    socket_path[108];
    vanilla_client_conn_t   clients[VANILLA_MAX_CLIENTS];
    vanilla_server_window_t windows[VANILLA_MAX_WINDOWS];
    uint32_t                next_window_id;
    uint32_t                focused_window_id;
    int32_t                 next_z_index;
    vanilla_compositor_t    compositor;
    int                     running;

    /* Input subsystem & cursor overlay state */
    int                     input_fd;
    int32_t                 cursor_x;
    int32_t                 cursor_y;
    uint32_t                mouse_buttons;
    int                     shift_pressed;
    int                     alt_pressed;
    int                     ctrl_pressed;

    /* Window interactive dragging */
    uint32_t                drag_window_id;
    int                     is_dragging;
    int32_t                 drag_offset_x;
    int32_t                 drag_offset_y;

    /* Desktop shell taskbar and quick launcher */
    vanilla_shell_t         shell;
    vanilla_launcher_t      launcher;
} vanilla_server_t;

/* Server lifecycle */
int  vanilla_server_init(vanilla_server_t *srv, const char *socket_path);
void vanilla_server_close(vanilla_server_t *srv);

/* Connection and client dispatch */
int  vanilla_server_accept(vanilla_server_t *srv);
int  vanilla_server_poll(vanilla_server_t *srv, int timeout_ms);
int  vanilla_server_dispatch_client(vanilla_server_t *srv, int client_idx);
void vanilla_server_remove_client(vanilla_server_t *srv, int client_idx);

/* Window lookups, layout, and notifications */
vanilla_server_window_t *vanilla_server_find_window(vanilla_server_t *srv, uint32_t window_id);
int  vanilla_server_send_input(vanilla_server_t *srv, uint32_t window_id, const struct input_event *ev);
int  vanilla_server_focus_window(vanilla_server_t *srv, uint32_t window_id);
int  vanilla_server_close_request(vanilla_server_t *srv, uint32_t window_id);
void wm_raise_window(vanilla_server_t *srv, uint32_t window_id);
void wm_lower_window(vanilla_server_t *srv, uint32_t window_id);
void wm_get_frame_rect(const vanilla_server_window_t *win, vanilla_rect_t *out_frame);
void wm_invalidate_window(vanilla_server_t *srv, const vanilla_server_window_t *win);

/* Window tiling, input dispatch, and cursor rendering */
void wm_snap_window(vanilla_server_t *srv, uint32_t window_id, int snap_type);
void wm_unsnap_window(vanilla_server_t *srv, uint32_t window_id);
int  wm_handle_input_event(vanilla_server_t *srv, const struct input_event *ev);
void wm_render_cursor(vanilla_server_t *srv, const vanilla_rect_t *dirty);

#endif /* _VANILLA_SERVER_H */
