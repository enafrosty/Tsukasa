/*
 * Project Tsukasa — Service Supervisor Header
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

#ifndef _VANILLA_SUPERVISOR_H
#define _VANILLA_SUPERVISOR_H

#include <stdint.h>
#include <sys/types.h>

#define SUPERVISOR_MAX_SERVICES    16
#define SUPERVISOR_NAME_MAX        32
#define SUPERVISOR_PATH_MAX        256
#define SUPERVISOR_BACKOFF_INIT_MS 1000
#define SUPERVISOR_BACKOFF_MAX_MS  60000
#define SUPERVISOR_BACKOFF_RESET_MS 30000
#define SUPERVISOR_POLL_INTERVAL_MS 100

#define VSUP_SOCKET_PATH "/tmp/vsup.sock"
#define VSUP_MAGIC       0x56535550u /* "VSUP" */
#define VSUP_OP_LIST     1

typedef enum {
    SVC_STATE_STARTING = 0,
    SVC_STATE_RUNNING  = 1,
    SVC_STATE_BACKOFF  = 2,
    SVC_STATE_STOPPED  = 3,
} svc_state_t;

typedef struct {
    char        name[SUPERVISOR_NAME_MAX];
    char        path[SUPERVISOR_PATH_MAX];
    char        args[SUPERVISOR_PATH_MAX];
    char       *argv[16];
    int         argc;
    pid_t       pid;
    svc_state_t state;
    uint32_t    restart_count;
    uint64_t    last_start_ms;
    uint32_t    backoff_ms;
    uint64_t    restart_after_ms;
} svc_entry_t;

typedef struct {
    uint32_t magic;
    uint8_t  op;
    uint8_t  _pad[3];
} __attribute__((packed)) vsup_req_t;

typedef struct {
    uint32_t magic;
    int32_t  service_count;
} __attribute__((packed)) vsup_resp_hdr_t;

typedef struct {
    char     name[SUPERVISOR_NAME_MAX];
    int32_t  pid;
    uint32_t restart_count;
    uint8_t  state;
    uint8_t  _pad[3];
} __attribute__((packed)) vsup_svc_entry_t;

_Static_assert(sizeof(vsup_req_t) == 8, "vsup_req_t must be 8 bytes");
_Static_assert(sizeof(vsup_resp_hdr_t) == 8, "vsup_resp_hdr_t must be 8 bytes");
_Static_assert(sizeof(vsup_svc_entry_t) == 44, "vsup_svc_entry_t must be 44 bytes");

#endif /* _VANILLA_SUPERVISOR_H */
