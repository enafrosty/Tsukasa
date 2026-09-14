/*
 * Project Tsukasa — userland display-server (wsrv15) wire protocol
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

#ifndef TSUKASA_DISPLAY_PROTO_H
#define TSUKASA_DISPLAY_PROTO_H

#include <stdint.h>

#define WSD_MAGIC   0x54534B44u  /* 'TSKD' little-endian */
#define WSD_VERSION 1

/* The compositor's pathname-bound AF_UNIX rendezvous point. */
#define WSD_SOCKET_PATH "/tmp/wsrv15.sock"

/* Client -> server control messages. */
#define WSD_MSG_CREATE_SURFACE  1
#define WSD_MSG_DAMAGE          2
#define WSD_MSG_SET_TITLE       3
#define WSD_MSG_DESTROY_SURFACE 4
#define WSD_MSG_QUIT            5

/* Server -> client replies / events. */
#define WSD_MSG_CREATE_REPLY    64
#define WSD_EVT_KEY             65
#define WSD_EVT_POINTER         66
#define WSD_EVT_CLOSE           67
#define WSD_EVT_DAMAGE_ACK      68

#define WSD_TITLE_MAX 48

/* Surface creation flags. */
#define WSD_SURF_FLAG_NONE 0x0u

/* Fixed 16-byte frame header, identical layout to Nova's NovaFrameHeader. */
typedef struct {
    uint32_t magic;
    uint16_t version;
    uint16_t flags;
    uint32_t msg_type;
    uint32_t payload_size;
} __attribute__((packed)) wsd_hdr_t;

/* WSD_MSG_CREATE_SURFACE payload (client -> server). */
typedef struct {
    uint32_t w;
    uint32_t h;
    uint32_t flags;
} __attribute__((packed)) wsd_create_req_t;

/* WSD_MSG_CREATE_REPLY payload (server -> client). */
typedef struct {
    uint32_t surface_id;
    int32_t  shm_id;
    uint32_t pitch;
    uint32_t w;
    uint32_t h;
} __attribute__((packed)) wsd_create_reply_t;

/* WSD_MSG_DAMAGE payload (client -> server): recomposite this rect. */
typedef struct {
    uint32_t surface_id;
    int32_t  x;
    int32_t  y;
    int32_t  w;
    int32_t  h;
} __attribute__((packed)) wsd_damage_t;

/* WSD_MSG_SET_TITLE payload (client -> server). */
typedef struct {
    uint32_t surface_id;
    char     title[WSD_TITLE_MAX];
} __attribute__((packed)) wsd_set_title_t;

/* WSD_MSG_DESTROY_SURFACE payload (client -> server). */
typedef struct {
    uint32_t surface_id;
} __attribute__((packed)) wsd_surface_ref_t;

/* WSD_EVT_KEY payload (server -> client). Mirrors struct input_event. */
typedef struct {
    uint32_t surface_id;
    uint32_t keycode;
    int32_t  pressed;
    uint32_t modifiers;
} __attribute__((packed)) wsd_evt_key_t;

/* WSD_EVT_POINTER payload (server -> client). Screen coordinates in v1. */
typedef struct {
    uint32_t surface_id;
    int32_t  x;
    int32_t  y;
    uint32_t buttons;
} __attribute__((packed)) wsd_evt_pointer_t;

/* WSD_EVT_CLOSE payload (server -> client). */
typedef struct {
    uint32_t surface_id;
} __attribute__((packed)) wsd_evt_close_t;

/* WSD_EVT_DAMAGE_ACK payload (server -> client): sent after a DAMAGE has been composited to the framebuffer. */
typedef struct {
    uint32_t surface_id;
    uint32_t verified;
} __attribute__((packed)) wsd_evt_damage_ack_t;

#endif /* TSUKASA_DISPLAY_PROTO_H */
