/*
 * Project Tsukasa — Clipboard Daemon Wire Protocol and Definitions
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

#ifndef _VANILLA_SERVICES_CLIPBOARDD_H
#define _VANILLA_SERVICES_CLIPBOARDD_H

#include <stdint.h>
#include <stddef.h>

#define VCLIP_MAGIC         0x56434C50u   /* "VCLP" */
#define VCLIP_OP_SET        1             /* client -> daemon: set UTF-8 clipboard content */
#define VCLIP_OP_GET        2             /* client -> daemon: get current clipboard content */
#define VCLIP_OP_CLEAR      3             /* client -> daemon: clear clipboard */

#define VCLIP_TEXT_MAX      65536         /* 64 KiB: maximum UTF-8 buffer capacity */
#define VCLIP_SOCKET_PATH   "/tmp/vclip.sock"

#ifndef EMSGSIZE
#define EMSGSIZE            90
#endif

typedef struct {
    uint32_t magic;
    uint8_t  op;
    uint8_t  _pad[3];
    uint32_t text_len;  /* for SET: payload byte count; for GET/CLEAR: 0 */
} __attribute__((packed)) vclip_req_hdr_t;

_Static_assert(sizeof(vclip_req_hdr_t) == 12, "vclip_req_hdr_t size mismatch");

typedef struct {
    uint32_t magic;
    int32_t  status;    /* 0 on success; negative errno on error */
    uint32_t text_len;  /* for GET response: payload byte count */
} __attribute__((packed)) vclip_resp_hdr_t;

_Static_assert(sizeof(vclip_resp_hdr_t) == 12, "vclip_resp_hdr_t size mismatch");

/* Phase B wire protocol definitions: on-demand MIME transfer */
#define VCLIP_OP_OFFER      10  /* source -> daemon: announce MIME types */
#define VCLIP_OP_SUBSCRIBE  11  /* destination -> daemon: register interest */
#define VCLIP_OP_FETCH      12  /* destination -> daemon: request a MIME type */
#define VCLIP_OP_DATA       13  /* daemon -> source: request data for this type */

#define VCLIP_MIME_MAX      64
#define VCLIP_MIME_COUNT    8

typedef struct {
    uint32_t magic;
    uint8_t  op;
    uint8_t  mime_count;
    uint8_t  _pad[2];
    char     mimes[VCLIP_MIME_COUNT][VCLIP_MIME_MAX];
} __attribute__((packed)) vclip_offer_t;

typedef struct {
    uint32_t magic;
    uint8_t  op;
    uint8_t  _pad[3];
    char     mime[VCLIP_MIME_MAX];
    char     tmp_path[108];
} __attribute__((packed)) vclip_fetch_t;

#endif /* _VANILLA_SERVICES_CLIPBOARDD_H */
