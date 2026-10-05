/*
 * Project Tsukasa — Service Registry Daemon Protocol and Definitions
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

#ifndef _VANILLA_SERVICES_REGISTRYD_H
#define _VANILLA_SERVICES_REGISTRYD_H

#include <stdint.h>
#include <stddef.h>

#define VREG_MAGIC         0x56524547u   /* "VREG" */
#define VREG_OP_REGISTER   1
#define VREG_OP_UNREGISTER 2
#define VREG_OP_LOOKUP     3

#define VREG_NAME_MAX      32
#define VREG_PATH_MAX      108   /* matches sun_path length in struct sockaddr_un */

#define VREG_SOCKET_PATH   "/tmp/vreg.sock"
#define VREG_MAX_SERVICES  32

typedef struct {
    uint32_t magic;       /* VREG_MAGIC */
    uint8_t  op;          /* VREG_OP_* */
    uint8_t  _pad[3];
    char     name[VREG_NAME_MAX];
    char     path[VREG_PATH_MAX];  /* used by REGISTER; ignored by LOOKUP */
} __attribute__((packed)) vreg_req_t;

typedef struct {
    uint32_t magic;
    int32_t  status;       /* 0 = ok, negative errno on failure */
    char     path[VREG_PATH_MAX]; /* filled by LOOKUP, empty otherwise */
} __attribute__((packed)) vreg_resp_t;

#endif /* _VANILLA_SERVICES_REGISTRYD_H */
