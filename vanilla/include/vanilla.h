/*
 * Project Tsukasa — Vanilla Display Server Client SDK (libvanilla)
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

#ifndef _VANILLA_H
#define _VANILLA_H

#include <stdint.h>
#include <stddef.h>
#include "protocol.h"
#include "surface.h"

typedef enum {
    VANILLA_EVENT_NONE = 0,
    VANILLA_EVENT_INPUT = 1,
    VANILLA_EVENT_FOCUS = 2,
    VANILLA_EVENT_CLOSE_REQ = 3,
    VANILLA_EVENT_CONFIGURE = 4,
    VANILLA_EVENT_FRAME_BEGIN = 5,
    VANILLA_EVENT_BUFFER_RELEASED = 6,
    VANILLA_EVENT_DOUBLE_CLICK = 7,
    VANILLA_EVENT_TRIPLE_CLICK = 8,
    VANILLA_EVENT_LONG_PRESS = 9,
} vanilla_event_type_t;

typedef struct {
    vanilla_event_type_t type;
    uint32_t             window_id;
    uint16_t             mod_state;
    union {
        struct input_event input;
        struct {
            int focused;
        } focus;
        struct {
            int32_t  x;
            int32_t  y;
            uint32_t width;
            uint32_t height;
            uint32_t serial;
            uint32_t flags;
        } configure;
        struct {
            uint32_t frame_serial;
            uint32_t timestamp_ms;
        } frame_begin;
        struct {
            uint32_t serial;
        } buffer_released;
    };
} vanilla_event_t;

typedef struct vanilla_client vanilla_client_t;

typedef void (*vanilla_frame_callback_t)(vanilla_client_t *client, uint32_t window_id,
                                         uint32_t frame_serial, uint32_t timestamp_ms);

typedef struct vanilla_window {
    vanilla_client_t      *client;
    uint32_t               window_id;
    vanilla_surface_t      surface;
    int32_t                x;
    int32_t                y;
    uint32_t               width;
    uint32_t               height;
    uint32_t               flags;
    uint32_t               last_frame_serial;
    struct vanilla_window *next;
} vanilla_window_t;

/* Client lifecycle management */
vanilla_client_t *vanilla_connect(const char *socket_path);
void vanilla_disconnect(vanilla_client_t *client);
void vanilla_set_frame_callback(vanilla_client_t *client, vanilla_frame_callback_t fn);

/* Window operations */
vanilla_window_t *vanilla_create_window(vanilla_client_t *client, const char *title,
                                        int x, int y, int w, int h, uint32_t flags);
void vanilla_destroy_window(vanilla_window_t *win);
int  vanilla_map_window(vanilla_window_t *win);
int  vanilla_unmap_window(vanilla_window_t *win);
int  vanilla_move_window(vanilla_window_t *win, int32_t x, int32_t y);
void vanilla_present(vanilla_window_t *win, const vanilla_rect_t *damage);
int  vanilla_ack_configure(vanilla_client_t *client, uint32_t window_id, uint32_t serial);
int  vanilla_set_size_hints(vanilla_client_t *client, uint32_t window_id,
                            uint32_t min_w, uint32_t min_h,
                            uint32_t max_w, uint32_t max_h,
                            uint32_t asp_n, uint32_t asp_d);

/* Event processing */
int  vanilla_poll_event(vanilla_client_t *client, vanilla_event_t *out_ev);
int  vanilla_wait_event(vanilla_client_t *client, vanilla_event_t *out_ev);

/* Service discovery */
#include "../libvanilla/service.h"

#endif /* _VANILLA_H */
