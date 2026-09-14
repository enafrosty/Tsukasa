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
} vanilla_event_type_t;

typedef struct {
    vanilla_event_type_t type;
    uint32_t             window_id;
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
        } configure;
    };
} vanilla_event_t;

typedef struct vanilla_client vanilla_client_t;

typedef struct vanilla_window {
    vanilla_client_t      *client;
    uint32_t               window_id;
    vanilla_surface_t      surface;
    int32_t                x;
    int32_t                y;
    uint32_t               width;
    uint32_t               height;
    uint32_t               flags;
    struct vanilla_window *next;
} vanilla_window_t;

/* Client lifecycle management */
vanilla_client_t *vanilla_connect(const char *socket_path);
void vanilla_disconnect(vanilla_client_t *client);

/* Window operations */
vanilla_window_t *vanilla_create_window(vanilla_client_t *client, const char *title,
                                        int x, int y, int w, int h, uint32_t flags);
void vanilla_destroy_window(vanilla_window_t *win);
int  vanilla_map_window(vanilla_window_t *win);
int  vanilla_unmap_window(vanilla_window_t *win);
int  vanilla_move_window(vanilla_window_t *win, int32_t x, int32_t y);
void vanilla_present(vanilla_window_t *win, const vanilla_rect_t *damage);

/* Event processing */
int  vanilla_poll_event(vanilla_client_t *client, vanilla_event_t *out_ev);
int  vanilla_wait_event(vanilla_client_t *client, vanilla_event_t *out_ev);

#endif /* _VANILLA_H */
