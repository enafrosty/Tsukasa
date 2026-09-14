/*
 * Project Tsukasa — tk_client: display-server connection for SDK apps
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

#ifndef TSUKASA_TK_CLIENT_H
#define TSUKASA_TK_CLIENT_H

#include <stdint.h>
#include "../../include/display_proto.h"

typedef struct tk_surface {
    int conn_fd;
    uint32_t surface_id;
    int shm_id;
    uint32_t *px;
    uint32_t w, h;
} tk_surface_t;

/* Bounded, dead-peer-safe framed I/O (shared by tk_app). Return 1 on success, 0 on HUP/spin-out. */
int tk_send_all(int fd, const void *buf, int n);
int tk_recv_all(int fd, void *buf, int n);
int tk_send_frame(int fd, uint32_t type, const void *payload, uint32_t size);

/* Connect to the compositor (retries while it starts up). fd or -1. */
int tk_connect(void);

/* CREATE_SURFACE round trip + shm attach. 0 on success. */
int tk_surface_create(int fd, uint32_t w, uint32_t h, tk_surface_t *out);

/* Detach the pixels and ask the server to destroy the surface. */
void tk_surface_teardown(tk_surface_t *surf);

#endif /* TSUKASA_TK_CLIENT_H */
