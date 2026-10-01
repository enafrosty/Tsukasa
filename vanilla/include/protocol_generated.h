/*
 * Project Tsukasa — Auto-Generated Vanilla Display Server IPC Protocol
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
/* Auto-generated - do not edit. Regenerate with: make -C vanilla gen-protocol */

#ifndef _VANILLA_PROTOCOL_GENERATED_H
#define _VANILLA_PROTOCOL_GENERATED_H

#include <stdint.h>
#include <stddef.h>
#include <sys/input.h>

typedef struct {
    uint32_t magic;
    uint16_t msg_type;
    uint16_t payload_len;
    uint32_t window_id;
} __attribute__((packed)) vanilla_msg_hdr_t;

_Static_assert(sizeof(vanilla_msg_hdr_t) == 12, "vanilla_msg_hdr_t size mismatch");

typedef enum {
    MSG_HELLO                = 1,
    MSG_HELLO_ACK            = 2,
    MSG_CREATE_WINDOW        = 3,
    MSG_CREATE_WINDOW_ACK    = 4,
    MSG_DESTROY_WINDOW       = 5,
    MSG_MAP_WINDOW           = 6,
    MSG_UNMAP_WINDOW         = 7,
    MSG_MOVE_RESIZE          = 8,
    MSG_PRESENT              = 9,
    MSG_INPUT_EVENT          = 10,
    MSG_WINDOW_FOCUS         = 11,
    MSG_WINDOW_CLOSE_REQ     = 12,
    MSG_WINDOW_CONFIGURE     = 13,
} vanilla_msg_type_t;

/* MSG_HELLO payload (client to server, v1+) */
typedef struct {
    uint32_t client_version;
    char     client_name[32];
} __attribute__((packed)) vanilla_msg_hello_t;

_Static_assert(sizeof(vanilla_msg_hello_t) == 36,
    "vanilla_msg_hello_t size mismatch; regenerate protocol");

/* MSG_HELLO_ACK payload (server to client, v1+) */
typedef struct {
    uint32_t server_version;
    int32_t  status;
} __attribute__((packed)) vanilla_msg_hello_ack_t;

_Static_assert(sizeof(vanilla_msg_hello_ack_t) == 8,
    "vanilla_msg_hello_ack_t size mismatch; regenerate protocol");

/* MSG_CREATE_WINDOW payload (client to server, v1+) */
typedef struct {
    char     title[64];
    int32_t  x;
    int32_t  y;
    uint32_t width;
    uint32_t height;
    uint32_t flags;
} __attribute__((packed)) vanilla_msg_create_window_t;

_Static_assert(sizeof(vanilla_msg_create_window_t) == 84,
    "vanilla_msg_create_window_t size mismatch; regenerate protocol");

/* MSG_CREATE_WINDOW_ACK payload (server to client, v1+) */
typedef struct {
    uint32_t window_id;
    int32_t  shm_id;
    uint32_t buffer_size;
    uint32_t pitch;
    int32_t  status;
} __attribute__((packed)) vanilla_msg_create_window_ack_t;

_Static_assert(sizeof(vanilla_msg_create_window_ack_t) == 20,
    "vanilla_msg_create_window_ack_t size mismatch; regenerate protocol");

/* MSG_DESTROY_WINDOW payload (client to server, v1+) */
typedef struct {
    uint32_t window_id;
} __attribute__((packed)) vanilla_msg_destroy_window_t;

_Static_assert(sizeof(vanilla_msg_destroy_window_t) == 4,
    "vanilla_msg_destroy_window_t size mismatch; regenerate protocol");

/* MSG_MAP_WINDOW payload (client to server, v1+) */
typedef struct {
    uint32_t window_id;
} __attribute__((packed)) vanilla_msg_map_window_t;

_Static_assert(sizeof(vanilla_msg_map_window_t) == 4,
    "vanilla_msg_map_window_t size mismatch; regenerate protocol");

/* MSG_UNMAP_WINDOW payload (client to server, v1+) */
typedef struct {
    uint32_t window_id;
} __attribute__((packed)) vanilla_msg_unmap_window_t;

_Static_assert(sizeof(vanilla_msg_unmap_window_t) == 4,
    "vanilla_msg_unmap_window_t size mismatch; regenerate protocol");

/* MSG_MOVE_RESIZE payload (client to server, v1+) */
typedef struct {
    int32_t  x;
    int32_t  y;
    uint32_t width;
    uint32_t height;
} __attribute__((packed)) vanilla_msg_move_resize_t;

_Static_assert(sizeof(vanilla_msg_move_resize_t) == 16,
    "vanilla_msg_move_resize_t size mismatch; regenerate protocol");

/* MSG_PRESENT payload (client to server, v1+) */
typedef struct {
    int32_t  x;
    int32_t  y;
    int32_t  w;
    int32_t  h;
} __attribute__((packed)) vanilla_msg_present_t;

_Static_assert(sizeof(vanilla_msg_present_t) == 16,
    "vanilla_msg_present_t size mismatch; regenerate protocol");

/* MSG_INPUT_EVENT payload (server to client, v1+) */
typedef struct {
    struct input_event event;
} __attribute__((packed)) vanilla_msg_input_event_t;

_Static_assert(sizeof(vanilla_msg_input_event_t) == 24,
    "vanilla_msg_input_event_t size mismatch; regenerate protocol");

/* MSG_WINDOW_FOCUS payload (server to client, v1+) */
typedef struct {
    uint32_t focused;
} __attribute__((packed)) vanilla_msg_window_focus_t;

_Static_assert(sizeof(vanilla_msg_window_focus_t) == 4,
    "vanilla_msg_window_focus_t size mismatch; regenerate protocol");

/* MSG_WINDOW_CLOSE_REQ payload (server to client, v1+) */
typedef struct {
    uint32_t reserved;
} __attribute__((packed)) vanilla_msg_window_close_req_t;

_Static_assert(sizeof(vanilla_msg_window_close_req_t) == 4,
    "vanilla_msg_window_close_req_t size mismatch; regenerate protocol");

/* MSG_WINDOW_CONFIGURE payload (server to client, v1+) */
typedef struct {
    int32_t  x;
    int32_t  y;
    uint32_t width;
    uint32_t height;
} __attribute__((packed)) vanilla_msg_window_configure_t;

_Static_assert(sizeof(vanilla_msg_window_configure_t) == 16,
    "vanilla_msg_window_configure_t size mismatch; regenerate protocol");

typedef struct vanilla_server vanilla_server_t;

typedef int (*vanilla_dispatch_fn_t)(vanilla_server_t *srv, int client_idx,
                                     const vanilla_msg_hdr_t *hdr,
                                     const uint8_t *payload);

typedef struct {
    uint16_t              msg_type;
    uint16_t              min_version;
    size_t                payload_size;
    vanilla_dispatch_fn_t handler;
} vanilla_dispatch_entry_t;

int handle_msg_hello(vanilla_server_t *srv, int client_idx, const vanilla_msg_hdr_t *hdr, const uint8_t *payload);
int handle_msg_create_window(vanilla_server_t *srv, int client_idx, const vanilla_msg_hdr_t *hdr, const uint8_t *payload);
int handle_msg_destroy_window(vanilla_server_t *srv, int client_idx, const vanilla_msg_hdr_t *hdr, const uint8_t *payload);
int handle_msg_map_window(vanilla_server_t *srv, int client_idx, const vanilla_msg_hdr_t *hdr, const uint8_t *payload);
int handle_msg_unmap_window(vanilla_server_t *srv, int client_idx, const vanilla_msg_hdr_t *hdr, const uint8_t *payload);
int handle_msg_move_resize(vanilla_server_t *srv, int client_idx, const vanilla_msg_hdr_t *hdr, const uint8_t *payload);
int handle_msg_present(vanilla_server_t *srv, int client_idx, const vanilla_msg_hdr_t *hdr, const uint8_t *payload);

#ifdef VANILLA_DISPATCH_TABLE_IMPL
const vanilla_dispatch_entry_t g_vanilla_dispatch_table[] = {
    { MSG_HELLO, 1, sizeof(vanilla_msg_hello_t), handle_msg_hello },
    { MSG_CREATE_WINDOW, 1, sizeof(vanilla_msg_create_window_t), handle_msg_create_window },
    { MSG_DESTROY_WINDOW, 1, sizeof(vanilla_msg_destroy_window_t), handle_msg_destroy_window },
    { MSG_MAP_WINDOW, 1, sizeof(vanilla_msg_map_window_t), handle_msg_map_window },
    { MSG_UNMAP_WINDOW, 1, sizeof(vanilla_msg_unmap_window_t), handle_msg_unmap_window },
    { MSG_MOVE_RESIZE, 1, sizeof(vanilla_msg_move_resize_t), handle_msg_move_resize },
    { MSG_PRESENT, 1, sizeof(vanilla_msg_present_t), handle_msg_present },
};
const size_t g_vanilla_dispatch_table_len = sizeof(g_vanilla_dispatch_table) / sizeof(g_vanilla_dispatch_table[0]);
#else
extern const vanilla_dispatch_entry_t g_vanilla_dispatch_table[];
extern const size_t                   g_vanilla_dispatch_table_len;
#endif

#endif /* _VANILLA_PROTOCOL_GENERATED_H */
