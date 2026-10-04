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

#define MENU_ITEM_LABEL_MAX  64

#define MENU_ITEM_ENABLED    (1u << 0)
#define MENU_ITEM_SEPARATOR  (1u << 1)
#define MENU_ITEM_CHECKED    (1u << 2)

typedef struct {
    uint32_t item_id;       /* nonzero; 0 reserved for separator markers */
    uint32_t submenu_id;    /* 0 = leaf item; nonzero = opens a submenu */
    uint16_t flags;         /* MENU_ITEM_ENABLED, MENU_ITEM_SEPARATOR, MENU_ITEM_CHECKED */
    uint16_t reserved;
    char     label[MENU_ITEM_LABEL_MAX];  /* UTF-8, NUL-terminated */
} __attribute__((packed)) vanilla_menu_item_t;

_Static_assert(sizeof(vanilla_menu_item_t) == 76,
    "vanilla_menu_item_t size mismatch");

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
    VANILLA_EVENT_CONTEXT_MENU_RESULT = 10,
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
        struct {
            uint32_t menu_id;
            uint32_t item_id;
        } context_menu_result;
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
int  vanilla_client_get_fd(const vanilla_client_t *client);

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

/* Context menu operations */
int vanilla_show_context_menu(vanilla_client_t *client, vanilla_window_t *win,
                              uint32_t menu_id, int32_t x, int32_t y,
                              const vanilla_menu_item_t *items, uint32_t item_count);
int vanilla_show_submenu(vanilla_client_t *client, vanilla_window_t *win,
                         uint32_t menu_id, uint32_t parent_id,
                         const vanilla_menu_item_t *items, uint32_t item_count);

/* Convenience builder */
typedef struct vanilla_menu_builder vanilla_menu_builder_t;
vanilla_menu_builder_t *vanilla_menu_builder_new(void);
void vanilla_menu_builder_add_item(vanilla_menu_builder_t *b, uint32_t id, const char *label);
void vanilla_menu_builder_add_separator(vanilla_menu_builder_t *b);
void vanilla_menu_builder_add_submenu(vanilla_menu_builder_t *b, uint32_t id,
                                     const char *label, uint32_t submenu_id);
int  vanilla_menu_builder_show(vanilla_menu_builder_t *b, vanilla_client_t *c,
                               vanilla_window_t *w, uint32_t menu_id, int32_t x, int32_t y);
int  vanilla_menu_builder_show_sub(vanilla_menu_builder_t *b, vanilla_client_t *c,
                                   vanilla_window_t *w, uint32_t menu_id, uint32_t parent_id);
void vanilla_menu_builder_free(vanilla_menu_builder_t *b);

#endif /* _VANILLA_H */
