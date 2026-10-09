/*
 * Project Tsukasa — Vanilla Display Server Protocol Definitions
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

#ifndef _VANILLA_PROTOCOL_H
#define _VANILLA_PROTOCOL_H

#include <stdint.h>
#include <stddef.h>
#include <sys/input.h>
#include "protocol_generated.h"

/* Modifier key bitmasks (MOD_*) */
#define MOD_LSHIFT    (1u << 0)
#define MOD_RSHIFT    (1u << 1)
#define MOD_LCTRL     (1u << 2)
#define MOD_RCTRL     (1u << 3)
#define MOD_LALT      (1u << 4)
#define MOD_RALT      (1u << 5)
#define MOD_CAPS_LOCK (1u << 6)   /* toggle */
#define MOD_NUM_LOCK  (1u << 7)   /* toggle */
#define MOD_SUPER     (1u << 8)   /* left Super / Windows key */

/* Convenience aggregates */
#define MOD_SHIFT     (MOD_LSHIFT | MOD_RSHIFT)
#define MOD_CTRL      (MOD_LCTRL  | MOD_RCTRL)
#define MOD_ALT       (MOD_LALT   | MOD_RALT)

/* Extended evdev keycodes if not provided by libc sys/input.h */
#ifndef KEY_RIGHTCTRL
#define KEY_RIGHTCTRL 97
#endif
#ifndef KEY_RIGHTALT
#define KEY_RIGHTALT 100
#endif
#ifndef KEY_NUMLOCK
#define KEY_NUMLOCK 69
#endif
#ifndef KEY_LEFTMETA
#define KEY_LEFTMETA 125
#endif
#ifndef KEY_RIGHTMETA
#define KEY_RIGHTMETA 126
#endif

#define VANILLA_IPC_MAGIC       0x56414E49u
#define VANILLA_IPC_VERSION     2
#define VANILLA_SOCKET_PATH     "/tmp/vanilla.sock"

#define VANILLA_TITLE_MAX       64
#define VANILLA_MAX_WIDTH       4096
#define VANILLA_MAX_HEIGHT      4096

#ifndef VANILLA_MAX_WINDOWS
#define VANILLA_MAX_WINDOWS     64
#endif

#ifndef VANILLA_MAX_CLIENTS
#define VANILLA_MAX_CLIENTS     16
#endif

/* Window creation and configuration flags */
#define WINDOW_FLAG_NONE        0x00000000u
#define WINDOW_FLAG_BORDERLESS  (1u << 0)
#define WINDOW_FLAG_RESIZABLE   (1u << 1)
#define WINDOW_FLAG_POPUP       (1u << 2)
#define WINDOW_FLAG_TRANSPARENT (1u << 3)
#define WINDOW_FLAG_MODAL       (1u << 4)
#define WINDOW_FLAG_ALWAYS_TOP  (1u << 5)

/* MSG_PRESENT - extended for v2 (client -> server) */
typedef struct {
    int32_t  x;
    int32_t  y;
    int32_t  w;
    int32_t  h;
    uint32_t frame_serial;
} __attribute__((packed)) vanilla_msg_present_v2_t;

_Static_assert(sizeof(vanilla_msg_present_v2_t) == 20,
    "vanilla_msg_present_v2_t size mismatch");

/* MSG_WINDOW_CONFIGURE - extended for v2 (server -> client) */
typedef struct {
    int32_t  x;
    int32_t  y;
    uint32_t width;
    uint32_t height;
    uint32_t serial;
    uint32_t flags;
} __attribute__((packed)) vanilla_msg_window_configure_v2_t;

_Static_assert(sizeof(vanilla_msg_window_configure_v2_t) == 24,
    "vanilla_msg_window_configure_v2_t size mismatch");

#ifndef KEY_F12
#define KEY_F12 88
#endif

/* Debug query types */
#define DBGQ_WINDOWS       1
#define DBGQ_COMPOSITOR    2
#define DBGQ_THEME         3
#define DBGQ_SET_THEME     4
#define DBGQ_THEME_RELOAD  5

/* Record returned for DBGQ_WINDOWS */
typedef struct {
    uint32_t window_id;
    int32_t  pid;          /* process ID of the owning client (-1 if not tracked) */
    char     title[VANILLA_TITLE_MAX];
    int32_t  x, y;
    uint32_t w, h;
    int32_t  z_index;
    uint8_t  layer;
    uint8_t  is_mapped;
    uint8_t  is_focused;
    uint8_t  _pad;
    int32_t  damage_x, damage_y, damage_w, damage_h;
    uint32_t frame_count;   /* frames presented since last query */
} __attribute__((packed)) vanilla_dbg_window_t;

_Static_assert(sizeof(vanilla_dbg_window_t) == 116,
    "vanilla_dbg_window_t size mismatch");

/* Record returned for DBGQ_COMPOSITOR */
typedef struct {
    uint32_t frame_time_us;    /* microseconds for last composite pass */
    uint32_t dirty_rect_count; /* dirty_rects[] entries in last pass */
    uint32_t damage_area_px;   /* total pixels in dirty rects */
    uint32_t client_count;
    uint32_t window_count;
} __attribute__((packed)) vanilla_dbg_compositor_t;

_Static_assert(sizeof(vanilla_dbg_compositor_t) == 20,
    "vanilla_dbg_compositor_t size mismatch");

#endif /* _VANILLA_PROTOCOL_H */
