/*
 * Project Tsukasa — Notification Daemon Wire Protocol and Definitions
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

#ifndef _VANILLA_SERVICES_NOTIFYD_H
#define _VANILLA_SERVICES_NOTIFYD_H

#include <stdint.h>
#include <stddef.h>

#define VNOTIF_MAGIC          0x564E4F54u  /* "VNOT" */
#define VNOTIF_OP_SEND        1   /* client -> daemon: post a notification */
#define VNOTIF_OP_CLOSE       2   /* client -> daemon: explicitly dismiss */
#define VNOTIF_OP_EVENT       3   /* daemon -> client: action selected or dismissed */

#define VNOTIF_TITLE_MAX      64
#define VNOTIF_BODY_MAX       256
#define VNOTIF_ICON_MAX       32   /* icon name, e.g. "info", "warning", "error" */
#define VNOTIF_ACTION_MAX     4    /* max action buttons per notification */
#define VNOTIF_ACTION_LBL_MAX 24   /* action label text */

#define VNOTIF_SOCKET_PATH    "/tmp/vnotif.sock"

#define NOTIF_MAX_ACTIVE      8    /* max active notifications in queue */
#define NOTIF_MAX_VISIBLE     3    /* max visible toast windows simultaneously */

#define TOAST_WIDTH           320
#define TOAST_BASE_HEIGHT     72
#define TOAST_ACTION_HEIGHT   32   /* extra height when action buttons are present (72+32=104) */
#define TOAST_MARGIN_X        12
#define TOAST_MARGIN_Y        12
#define TOAST_SPACING         8

typedef struct {
    char     label[VNOTIF_ACTION_LBL_MAX];
    uint32_t action_id;
} __attribute__((packed)) vnotif_action_t;

_Static_assert(sizeof(vnotif_action_t) == 28, "vnotif_action_t size mismatch");

typedef struct {
    uint32_t        magic;
    uint8_t         op;            /* VNOTIF_OP_SEND */
    uint8_t         action_count;  /* 0–VNOTIF_ACTION_MAX */
    uint8_t         _pad[2];
    uint32_t        timeout_ms;    /* 0 = persistent */
    char            title[VNOTIF_TITLE_MAX];
    char            body[VNOTIF_BODY_MAX];
    char            icon_name[VNOTIF_ICON_MAX];
    vnotif_action_t actions[VNOTIF_ACTION_MAX];
} __attribute__((packed)) vnotif_send_t;

_Static_assert(sizeof(vnotif_send_t) == 476, "vnotif_send_t size mismatch");

typedef struct {
    uint32_t magic;
    uint8_t  op;       /* VNOTIF_OP_CLOSE */
    uint8_t  _pad[3];
    uint32_t notif_id;
} __attribute__((packed)) vnotif_close_t;

_Static_assert(sizeof(vnotif_close_t) == 12, "vnotif_close_t size mismatch");

typedef struct {
    uint32_t magic;
    int32_t  status;    /* 0 = ok; negative errno on failure */
    uint32_t notif_id;  /* assigned by daemon on SEND; 0 on error */
} __attribute__((packed)) vnotif_resp_t;

_Static_assert(sizeof(vnotif_resp_t) == 12, "vnotif_resp_t size mismatch");

typedef struct {
    uint32_t magic;
    uint8_t  op;           /* VNOTIF_OP_EVENT */
    uint8_t  _pad[3];
    uint32_t notif_id;
    uint32_t action_id;    /* 0 = dismissed (timeout or close button) */
} __attribute__((packed)) vnotif_event_t;

_Static_assert(sizeof(vnotif_event_t) == 16, "vnotif_event_t size mismatch");

int notifyd_run(const char *sock_path, volatile int *stop_flag);

#ifdef VNOTIF_TEST_NO_MAIN
int notifyd_test_get_counts(int *out_active, int *out_visible);
int notifyd_test_trigger_action(uint32_t notif_id, uint32_t action_id);
uint32_t notifyd_test_get_window_id(uint32_t notif_id);
#endif

#endif /* _VANILLA_SERVICES_NOTIFYD_H */
