/*
 * Project Tsukasa — Vanilla Display Server Binary IPC Protocol Definitions
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

#define VANILLA_IPC_MAGIC       0x56414E49u
#define VANILLA_IPC_VERSION     1
#define VANILLA_SOCKET_PATH     "/tmp/vanilla.sock"

#define VANILLA_TITLE_MAX       64
#define VANILLA_MAX_WIDTH       4096
#define VANILLA_MAX_HEIGHT      4096

/* Window creation and configuration flags */
#define WINDOW_FLAG_NONE        0x00000000u
#define WINDOW_FLAG_BORDERLESS  (1u << 0)
#define WINDOW_FLAG_RESIZABLE   (1u << 1)
#define WINDOW_FLAG_POPUP       (1u << 2)
#define WINDOW_FLAG_TRANSPARENT (1u << 3)
#define WINDOW_FLAG_MODAL       (1u << 4)
#define WINDOW_FLAG_ALWAYS_TOP  (1u << 5)

/* Binary IPC message IDs */
#define MSG_HELLO               1
#define MSG_HELLO_ACK           2
#define MSG_CREATE_WINDOW       3
#define MSG_CREATE_WINDOW_ACK   4
#define MSG_DESTROY_WINDOW      5
#define MSG_MAP_WINDOW          6
#define MSG_UNMAP_WINDOW        7
#define MSG_MOVE_RESIZE         8
#define MSG_PRESENT             9
#define MSG_INPUT_EVENT         10
#define MSG_WINDOW_FOCUS        11
#define MSG_WINDOW_CLOSE_REQ    12
#define MSG_WINDOW_CONFIGURE    13

/* Namespaced aliases */
#define VANILLA_MSG_HELLO               MSG_HELLO
#define VANILLA_MSG_HELLO_ACK           MSG_HELLO_ACK
#define VANILLA_MSG_CREATE_WINDOW       MSG_CREATE_WINDOW
#define VANILLA_MSG_CREATE_WINDOW_ACK   MSG_CREATE_WINDOW_ACK
#define VANILLA_MSG_DESTROY_WINDOW      MSG_DESTROY_WINDOW
#define VANILLA_MSG_MAP_WINDOW          MSG_MAP_WINDOW
#define VANILLA_MSG_UNMAP_WINDOW        MSG_UNMAP_WINDOW
#define VANILLA_MSG_MOVE_RESIZE         MSG_MOVE_RESIZE
#define VANILLA_MSG_PRESENT             MSG_PRESENT
#define VANILLA_MSG_INPUT_EVENT         MSG_INPUT_EVENT
#define VANILLA_MSG_WINDOW_FOCUS        MSG_WINDOW_FOCUS
#define VANILLA_MSG_WINDOW_CLOSE_REQ    MSG_WINDOW_CLOSE_REQ
#define VANILLA_MSG_WINDOW_CONFIGURE    MSG_WINDOW_CONFIGURE

/* Fixed 12-byte header envelope for all IPC messages */
typedef struct {
    uint32_t magic;
    uint16_t msg_type;
    uint16_t payload_len;
    uint32_t window_id;
} __attribute__((packed)) vanilla_msg_hdr_t;

/* MSG_HELLO payload (client -> server) */
typedef struct {
    uint32_t client_version;
    char     client_name[32];
} __attribute__((packed)) vanilla_msg_hello_t;

/* MSG_HELLO_ACK payload (server -> client) */
typedef struct {
    uint32_t server_version;
    int32_t  status;
} __attribute__((packed)) vanilla_msg_hello_ack_t;

/* MSG_CREATE_WINDOW payload (client -> server) */
typedef struct {
    char     title[VANILLA_TITLE_MAX];
    int32_t  x;
    int32_t  y;
    uint32_t width;
    uint32_t height;
    uint32_t flags;
} __attribute__((packed)) vanilla_msg_create_window_t;

/* MSG_CREATE_WINDOW_ACK payload (server -> client) */
typedef struct {
    uint32_t window_id;
    int32_t  shm_id;
    uint32_t buffer_size;
    uint32_t pitch;
    int32_t  status;
} __attribute__((packed)) vanilla_msg_create_window_ack_t;

/* MSG_DESTROY_WINDOW payload (client -> server) */
typedef struct {
    uint32_t window_id;
} __attribute__((packed)) vanilla_msg_destroy_window_t;

/* MSG_MAP_WINDOW / MSG_UNMAP_WINDOW payload (client -> server) */
typedef struct {
    uint32_t window_id;
} __attribute__((packed)) vanilla_msg_map_window_t;

/* MSG_MOVE_RESIZE payload (client -> server) */
typedef struct {
    int32_t  x;
    int32_t  y;
    uint32_t width;
    uint32_t height;
} __attribute__((packed)) vanilla_msg_move_resize_t;

/* MSG_PRESENT payload (client -> server) */
typedef struct {
    int32_t x;
    int32_t y;
    int32_t w;
    int32_t h;
} __attribute__((packed)) vanilla_msg_present_t;

/* MSG_INPUT_EVENT payload (server -> client) */
typedef struct {
    struct input_event event;
} __attribute__((packed)) vanilla_msg_input_event_t;

/* MSG_WINDOW_FOCUS payload (server -> client) */
typedef struct {
    uint32_t focused;
} __attribute__((packed)) vanilla_msg_window_focus_t;

/* MSG_WINDOW_CLOSE_REQ payload (server -> client) */
typedef struct {
    uint32_t reserved;
} __attribute__((packed)) vanilla_msg_window_close_req_t;

/* MSG_WINDOW_CONFIGURE payload (server -> client) */
typedef struct {
    int32_t  x;
    int32_t  y;
    uint32_t width;
    uint32_t height;
} __attribute__((packed)) vanilla_msg_window_configure_t;

#endif /* _VANILLA_PROTOCOL_H */
