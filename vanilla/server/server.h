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
#include <sys/types.h>
#include <sys/input.h>
#include "../include/protocol.h"
#include "../include/surface.h"
#include "../include/vanilla.h"
#include "compositor.h"
#include "cursor.h"

pid_t spawn(const char *path, char *const argv[], char *const envp[]);

#define TASKBAR_HEIGHT          THEME_PX(36)

#define CONTEXT_MENU_MAX_DEPTH  4
#define CONTEXT_MENU_MAX_ITEMS  64
#define CONTEXT_MENU_POOL_SIZE  8

#define SERVER_MENU_DESKTOP     0xDE5C0001u
#define SERVER_MENU_TASKBAR     0x7A5B0002u

typedef struct {
    int                 active;
    uint32_t            owning_window_id;
    int                 client_idx;
    uint32_t            menu_id;
    uint32_t            parent_id;
    int32_t             x;
    int32_t             y;
    int32_t             width;
    int32_t             height;
    int                 item_count;
    vanilla_menu_item_t items[CONTEXT_MENU_MAX_ITEMS];
    int                 highlighted;
    uint64_t            open_ticks;
    float               anim_alpha;
    uint64_t            hover_timer_start;
    int                 hover_item_idx;
    int                 open_submenu_idx;
} vanilla_context_menu_t;

#if defined(_WIN32)
#ifndef O_NONBLOCK
#define O_NONBLOCK 0x4000
#endif
int sched_yield(void);
#endif

#ifndef VANILLA_MAX_CLIENTS
#define VANILLA_MAX_CLIENTS 16
#endif
#ifndef VANILLA_MAX_WINDOWS
#define VANILLA_MAX_WINDOWS 64
#endif

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

typedef enum {
    RESIZE_EDGE_NONE      = 0,
    RESIZE_EDGE_TOP       = 1,
    RESIZE_EDGE_BOTTOM    = 2,
    RESIZE_EDGE_LEFT      = 3,
    RESIZE_EDGE_RIGHT     = 4,
    RESIZE_EDGE_TOP_LEFT  = 5,
    RESIZE_EDGE_TOP_RIGHT = 6,
    RESIZE_EDGE_BOT_LEFT  = 7,
    RESIZE_EDGE_BOT_RIGHT = 8,
} vanilla_resize_edge_t;

typedef enum {
    ANIM_IDLE       = 0,
    ANIM_OPENING    = 1,
    ANIM_CLOSING    = 2,
    ANIM_MINIMIZING = 3,
    ANIM_RESTORING  = 4,
    ANIM_SNAP_PREV  = 5,
    ANIM_FOCUS      = 6,
} vanilla_anim_state_t;

#define DRAG_MODE_NONE     0
#define DRAG_MODE_TITLEBAR 1
#define DRAG_MODE_RESIZE   2

typedef struct vanilla_server_window {
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

    /* Protocol v2 configure/ack */
    uint32_t          configure_serial;
    int               configure_pending;
    int32_t           pending_x;
    int32_t           pending_y;
    uint32_t          pending_w;
    uint32_t          pending_h;
    uint32_t          configure_timestamp_ms;

    /* Protocol v2 frame pacing */
    uint32_t          frame_serial;
    uint32_t          present_serial;
    int               buffer_in_use;
    int               frame_begin_in_flight;
    uint32_t          frame_begin_time_ms;

    /* Protocol v2 cursor shape */
    uint32_t          cursor_shape;

    /* Protocol v2 size hints */
    uint32_t          min_width;
    uint32_t          min_height;
    uint32_t          max_width;
    uint32_t          max_height;
    uint32_t          aspect_num;
    uint32_t          aspect_den;

    /* Transactional live resize target buffering */
    int               resize_has_target;
    int32_t           target_x;
    int32_t           target_y;
    uint32_t          target_w;
    uint32_t          target_h;

    /* Keyboard focus ring state */
    int               has_keyboard_focus;

    /* Compositor animation state */
    int               anim_state;
    uint64_t          anim_start_ticks;
    uint32_t          anim_dur_ticks;
    float             anim_scale_start;
    float             anim_scale_end;
    float             anim_alpha_start;
    float             anim_alpha_end;
    int32_t           anim_origin_x;
    int32_t           anim_origin_y;
    int               anim_destroy_on_done;
    int               anim_unmap_on_done;
    float             anim_current_scale;
    float             anim_current_alpha;

    /* Debug & diagnostics */
    uint32_t          presented_frames;
} vanilla_server_window_t;

typedef struct {
    int      in_use;
    int      fd;
    pid_t    client_pid;
    uint32_t version;
    uint32_t negotiated_version;
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
    uint16_t                mod_state;

    /* Click gesture detection */
    uint64_t                last_click_ticks[3];
    int32_t                 last_click_x[3];
    int32_t                 last_click_y[3];
    int                     click_count[3];
    uint64_t                btn_down_ticks[3];
    int32_t                 btn_down_x[3];
    int32_t                 btn_down_y[3];
    int                     long_press_fired[3];

    /* Alt+Tab window switcher */
    int                     alttab_visible;
    int                     alttab_selection;
    int                     alttab_window_count;
    uint32_t                alttab_window_ids[VANILLA_MAX_WINDOWS];

    /* Window interactive dragging */
    uint32_t                drag_window_id;
    int                     is_dragging;
    int32_t                 drag_offset_x;
    int32_t                 drag_offset_y;

    /* Interactive resize state */
    int                     is_resizing;
    uint32_t                resize_window_id;
    int                     resize_edge;
    int32_t                 resize_origin_x;
    int32_t                 resize_origin_y;
    int32_t                 resize_start_x;
    int32_t                 resize_start_y;
    uint32_t                resize_start_w;
    uint32_t                resize_start_h;

    /* Drag threshold */
    int                     drag_threshold_pending;
    int32_t                 drag_threshold_start_x;
    int32_t                 drag_threshold_start_y;
    int                     drag_threshold_mode;
    int                     drag_threshold_edge;
    uint32_t                drag_threshold_window_id;

    /* Server-managed context menus */
    vanilla_context_menu_t  context_menu_stack[CONTEXT_MENU_MAX_DEPTH];
    int                     context_menu_depth;
    vanilla_context_menu_t  context_menu_pool[CONTEXT_MENU_POOL_SIZE];
    int                     context_menu_pool_count;
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
void vanilla_server_broadcast_theme_changed(vanilla_server_t *srv);
void vanilla_server_send_frame_begin(vanilla_server_t *srv);
void vanilla_server_release_buffers(vanilla_server_t *srv);
int  vanilla_server_send_clipboard_request(vanilla_server_t *srv, uint32_t window_id, uint32_t request_id, const char *mime_type);
int  vanilla_server_send_dnd_drop(vanilla_server_t *srv, uint32_t target_window_id, int32_t x, int32_t y);

/* Window tiling, input dispatch, and hit testing */
void wm_snap_window(vanilla_server_t *srv, uint32_t window_id, int snap_type);
void wm_unsnap_window(vanilla_server_t *srv, uint32_t window_id);
int  wm_handle_input_event(vanilla_server_t *srv, const struct input_event *ev);
vanilla_server_window_t *wm_window_at(vanilla_server_t *srv, int32_t x, int32_t y);
vanilla_resize_edge_t wm_hit_test_resize_edge(const vanilla_server_window_t *w, int32_t px, int32_t py);
int  wm_run_resize_selftests(void);
int  wm_run_input_selftests(void);
int  wm_run_animation_selftests(void);
int  dnd_run_selftests(void);
void dnd_check_timeout(vanilla_server_t *srv);

void vanilla_server_destroy_window_record(vanilla_server_t *srv, vanilla_server_window_t *w);

/* Alt+Tab switcher overlay */
void alttab_get_rect(vanilla_server_t *srv, vanilla_rect_t *out_rect);
void alttab_render(vanilla_server_t *srv, const vanilla_rect_t *dirty);

/* Context menu operations */
void compositor_paint_context_menus(vanilla_server_t *srv, const vanilla_rect_t *dirty);
void wm_context_menu_close_stack(vanilla_server_t *srv, uint32_t result_item_id);
void wm_context_menu_pop(vanilla_server_t *srv);
int  wm_context_menu_handle_click(vanilla_server_t *srv, int32_t x, int32_t y, uint32_t button);
int  wm_context_menu_handle_motion(vanilla_server_t *srv, int32_t x, int32_t y);
int  wm_context_menu_handle_key(vanilla_server_t *srv, uint16_t code, int pressed);
void wm_context_menu_check_timers(vanilla_server_t *srv);
int  wm_context_menu_open_desktop(vanilla_server_t *srv, int32_t x, int32_t y);
int  wm_context_menu_open_taskbar(vanilla_server_t *srv, int32_t x, int32_t y, uint32_t window_id);
int  wm_run_context_menu_selftests(void);

/* Time and timer primitives */
uint64_t pit_ticks(void);
uint32_t pit_frequency(void);

#endif /* _VANILLA_SERVER_H */
