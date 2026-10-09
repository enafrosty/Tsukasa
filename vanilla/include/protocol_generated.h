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
    MSG_BUFFER_RELEASED      = 14,
    MSG_FRAME_BEGIN          = 15,
    MSG_ACK_CONFIGURE        = 16,
    MSG_SET_CURSOR           = 17,
    MSG_CLIPBOARD_OFFER      = 18,
    MSG_CLIPBOARD_REQUEST    = 19,
    MSG_DND_OFFER            = 20,
    MSG_DND_DROP             = 21,
    MSG_THEME_CHANGED        = 22,
    MSG_SET_SIZE_HINTS       = 23,
    MSG_INPUT_EVENT_V2       = 24,
    MSG_SHOW_CONTEXT_MENU    = 25,
    MSG_CONTEXT_MENU_RESULT  = 26,
    MSG_DND_ENTER            = 27,
    MSG_DND_LEAVE            = 28,
    MSG_DND_ACCEPT           = 29,
    MSG_DND_CANCEL           = 30,
    MSG_DEBUG_QUERY          = 31,
    MSG_DEBUG_QUERY_RESP     = 32,
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

/* MSG_BUFFER_RELEASED payload (server to client, v2+) */
typedef struct {
    uint32_t window_id;
    uint32_t serial;
} __attribute__((packed)) vanilla_msg_buffer_released_t;

_Static_assert(sizeof(vanilla_msg_buffer_released_t) == 8,
    "vanilla_msg_buffer_released_t size mismatch; regenerate protocol");

/* MSG_FRAME_BEGIN payload (server to client, v2+) */
typedef struct {
    uint32_t window_id;
    uint32_t frame_serial;
    uint32_t timestamp_ms;
} __attribute__((packed)) vanilla_msg_frame_begin_t;

_Static_assert(sizeof(vanilla_msg_frame_begin_t) == 12,
    "vanilla_msg_frame_begin_t size mismatch; regenerate protocol");

/* MSG_ACK_CONFIGURE payload (client to server, v2+) */
typedef struct {
    uint32_t window_id;
    uint32_t serial;
} __attribute__((packed)) vanilla_msg_ack_configure_t;

_Static_assert(sizeof(vanilla_msg_ack_configure_t) == 8,
    "vanilla_msg_ack_configure_t size mismatch; regenerate protocol");

/* MSG_SET_CURSOR payload (client to server, v2+) */
typedef struct {
    uint32_t window_id;
    uint32_t shape;
} __attribute__((packed)) vanilla_msg_set_cursor_t;

_Static_assert(sizeof(vanilla_msg_set_cursor_t) == 8,
    "vanilla_msg_set_cursor_t size mismatch; regenerate protocol");

/* MSG_CLIPBOARD_OFFER payload (client to server, v2+) */
typedef struct {
    uint32_t window_id;
    uint32_t mime_count;
} __attribute__((packed)) vanilla_msg_clipboard_offer_t;

_Static_assert(sizeof(vanilla_msg_clipboard_offer_t) == 8,
    "vanilla_msg_clipboard_offer_t size mismatch; regenerate protocol");

/* MSG_CLIPBOARD_REQUEST payload (server to client, v2+) */
typedef struct {
    uint32_t window_id;
    uint32_t request_id;
    char     mime_type[32];
} __attribute__((packed)) vanilla_msg_clipboard_request_t;

_Static_assert(sizeof(vanilla_msg_clipboard_request_t) == 40,
    "vanilla_msg_clipboard_request_t size mismatch; regenerate protocol");

/* MSG_DND_OFFER payload (client to server, v2+) */
typedef struct {
    char     mime[64];
    char     data[256];
    uint32_t data_len;
    uint32_t has_ghost;
    uint8_t  ghost[4096];
} __attribute__((packed)) vanilla_msg_dnd_offer_t;

_Static_assert(sizeof(vanilla_msg_dnd_offer_t) == 4424,
    "vanilla_msg_dnd_offer_t size mismatch; regenerate protocol");

/* MSG_DND_DROP payload (server to client, v2+) */
typedef struct {
    int32_t  local_x;
    int32_t  local_y;
    char     mime[64];
    char     data[256];
    uint32_t data_len;
} __attribute__((packed)) vanilla_msg_dnd_drop_t;

_Static_assert(sizeof(vanilla_msg_dnd_drop_t) == 332,
    "vanilla_msg_dnd_drop_t size mismatch; regenerate protocol");

/* MSG_THEME_CHANGED payload (server to client, v2+) */
typedef struct {
    uint32_t reserved;
} __attribute__((packed)) vanilla_msg_theme_changed_t;

_Static_assert(sizeof(vanilla_msg_theme_changed_t) == 4,
    "vanilla_msg_theme_changed_t size mismatch; regenerate protocol");

/* MSG_SET_SIZE_HINTS payload (client to server, v2+) */
typedef struct {
    uint32_t window_id;
    uint32_t min_width;
    uint32_t min_height;
    uint32_t max_width;
    uint32_t max_height;
    uint32_t aspect_num;
    uint32_t aspect_den;
} __attribute__((packed)) vanilla_msg_set_size_hints_t;

_Static_assert(sizeof(vanilla_msg_set_size_hints_t) == 28,
    "vanilla_msg_set_size_hints_t size mismatch; regenerate protocol");

/* MSG_INPUT_EVENT_V2 payload (server to client, v2+) */
typedef struct {
    struct input_event event;
    uint16_t mod_state;
} __attribute__((packed)) vanilla_msg_input_event_v2_t;

_Static_assert(sizeof(vanilla_msg_input_event_v2_t) == 26,
    "vanilla_msg_input_event_v2_t size mismatch; regenerate protocol");

/* MSG_SHOW_CONTEXT_MENU payload (client to server, v2+) */
typedef struct {
    uint32_t window_id;
    uint32_t menu_id;
    uint32_t parent_id;
    int32_t  x;
    int32_t  y;
    uint32_t item_count;
} __attribute__((packed)) vanilla_msg_show_context_menu_t;

_Static_assert(sizeof(vanilla_msg_show_context_menu_t) == 24,
    "vanilla_msg_show_context_menu_t size mismatch; regenerate protocol");

/* MSG_CONTEXT_MENU_RESULT payload (server to client, v2+) */
typedef struct {
    uint32_t window_id;
    uint32_t menu_id;
    uint32_t item_id;
} __attribute__((packed)) vanilla_msg_context_menu_result_t;

_Static_assert(sizeof(vanilla_msg_context_menu_result_t) == 12,
    "vanilla_msg_context_menu_result_t size mismatch; regenerate protocol");

/* MSG_DND_ENTER payload (server to client, v2+) */
typedef struct {
    int32_t  local_x;
    int32_t  local_y;
    char     mime[64];
} __attribute__((packed)) vanilla_msg_dnd_enter_t;

_Static_assert(sizeof(vanilla_msg_dnd_enter_t) == 72,
    "vanilla_msg_dnd_enter_t size mismatch; regenerate protocol");

/* MSG_DND_LEAVE payload (server to client, v2+) */
typedef struct {
    uint32_t reserved;
} __attribute__((packed)) vanilla_msg_dnd_leave_t;

_Static_assert(sizeof(vanilla_msg_dnd_leave_t) == 4,
    "vanilla_msg_dnd_leave_t size mismatch; regenerate protocol");

/* MSG_DND_ACCEPT payload (client to server, v2+) */
typedef struct {
    uint32_t accepted;
} __attribute__((packed)) vanilla_msg_dnd_accept_t;

_Static_assert(sizeof(vanilla_msg_dnd_accept_t) == 4,
    "vanilla_msg_dnd_accept_t size mismatch; regenerate protocol");

/* MSG_DND_CANCEL payload (server to client, v2+) */
typedef struct {
    uint32_t reserved;
} __attribute__((packed)) vanilla_msg_dnd_cancel_t;

_Static_assert(sizeof(vanilla_msg_dnd_cancel_t) == 4,
    "vanilla_msg_dnd_cancel_t size mismatch; regenerate protocol");

/* MSG_DEBUG_QUERY payload (client to server, v2+) */
typedef struct {
    uint32_t query_type;
} __attribute__((packed)) vanilla_msg_debug_query_t;

_Static_assert(sizeof(vanilla_msg_debug_query_t) == 4,
    "vanilla_msg_debug_query_t size mismatch; regenerate protocol");

/* MSG_DEBUG_QUERY_RESP payload (server to client, v2+) */
typedef struct {
    uint32_t window_count;
} __attribute__((packed)) vanilla_msg_debug_query_resp_t;

_Static_assert(sizeof(vanilla_msg_debug_query_resp_t) == 4,
    "vanilla_msg_debug_query_resp_t size mismatch; regenerate protocol");

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
int handle_msg_ack_configure(vanilla_server_t *srv, int client_idx, const vanilla_msg_hdr_t *hdr, const uint8_t *payload);
int handle_msg_set_cursor(vanilla_server_t *srv, int client_idx, const vanilla_msg_hdr_t *hdr, const uint8_t *payload);
int handle_msg_clipboard_offer(vanilla_server_t *srv, int client_idx, const vanilla_msg_hdr_t *hdr, const uint8_t *payload);
int handle_msg_dnd_offer(vanilla_server_t *srv, int client_idx, const vanilla_msg_hdr_t *hdr, const uint8_t *payload);
int handle_msg_set_size_hints(vanilla_server_t *srv, int client_idx, const vanilla_msg_hdr_t *hdr, const uint8_t *payload);
int handle_msg_show_context_menu(vanilla_server_t *srv, int client_idx, const vanilla_msg_hdr_t *hdr, const uint8_t *payload);
int handle_msg_dnd_accept(vanilla_server_t *srv, int client_idx, const vanilla_msg_hdr_t *hdr, const uint8_t *payload);
int handle_msg_debug_query(vanilla_server_t *srv, int client_idx, const vanilla_msg_hdr_t *hdr, const uint8_t *payload);

#ifdef VANILLA_DISPATCH_TABLE_IMPL
const vanilla_dispatch_entry_t g_vanilla_dispatch_table[] = {
    { MSG_HELLO, 1, sizeof(vanilla_msg_hello_t), handle_msg_hello },
    { MSG_CREATE_WINDOW, 1, sizeof(vanilla_msg_create_window_t), handle_msg_create_window },
    { MSG_DESTROY_WINDOW, 1, sizeof(vanilla_msg_destroy_window_t), handle_msg_destroy_window },
    { MSG_MAP_WINDOW, 1, sizeof(vanilla_msg_map_window_t), handle_msg_map_window },
    { MSG_UNMAP_WINDOW, 1, sizeof(vanilla_msg_unmap_window_t), handle_msg_unmap_window },
    { MSG_MOVE_RESIZE, 1, sizeof(vanilla_msg_move_resize_t), handle_msg_move_resize },
    { MSG_PRESENT, 1, sizeof(vanilla_msg_present_t), handle_msg_present },
    { MSG_ACK_CONFIGURE, 2, sizeof(vanilla_msg_ack_configure_t), handle_msg_ack_configure },
    { MSG_SET_CURSOR, 2, sizeof(vanilla_msg_set_cursor_t), handle_msg_set_cursor },
    { MSG_CLIPBOARD_OFFER, 2, sizeof(vanilla_msg_clipboard_offer_t), handle_msg_clipboard_offer },
    { MSG_DND_OFFER, 2, sizeof(vanilla_msg_dnd_offer_t), handle_msg_dnd_offer },
    { MSG_SET_SIZE_HINTS, 2, sizeof(vanilla_msg_set_size_hints_t), handle_msg_set_size_hints },
    { MSG_SHOW_CONTEXT_MENU, 2, 0, handle_msg_show_context_menu },
    { MSG_DND_ACCEPT, 2, sizeof(vanilla_msg_dnd_accept_t), handle_msg_dnd_accept },
    { MSG_DEBUG_QUERY, 2, 0, handle_msg_debug_query },
};
const size_t g_vanilla_dispatch_table_len = sizeof(g_vanilla_dispatch_table) / sizeof(g_vanilla_dispatch_table[0]);
#else
extern const vanilla_dispatch_entry_t g_vanilla_dispatch_table[];
extern const size_t                   g_vanilla_dispatch_table_len;
#endif

#endif /* _VANILLA_PROTOCOL_GENERATED_H */
