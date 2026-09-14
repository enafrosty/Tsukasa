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

#include "tk_client.h"

#include "tsukasa_sdk.h"
#include "../../include/socket_defs.h"

#include <stddef.h>

#define TK_SPIN_BUDGET 20000

static void addr_fill(struct tsk_sockaddr_un *ua, const char *path)
{
    int i = 0;
    ua->sun_family = TSK_AF_UNIX;
    while (path[i] && i < TSK_UNIX_PATH_MAX - 1) {
        ua->sun_path[i] = path[i];
        i++;
    }
    ua->sun_path[i] = '\0';
}

int tk_send_all(int fd, const void *buf, int n)
{
    const char *p = (const char *)buf;
    int sent = 0;
    for (int spin = 0; spin < TK_SPIN_BUDGET && sent < n; spin++) {
        long r = write(fd, p + sent, (unsigned long)(n - sent));
        if (r > 0) {
            sent += (int)r;
            continue;
        }
        {
            vfs_pollfd_t pf;
            pf.fd = fd;
            pf.events = (int16_t)(VFS_POLLOUT | VFS_POLLHUP);
            pf.revents = 0;
            poll(&pf, 1, 0);
            if (pf.revents & VFS_POLLHUP)
                return 0;
        }
        sched_yield();
    }
    return sent == n;
}

int tk_recv_all(int fd, void *buf, int n)
{
    char *p = (char *)buf;
    int got = 0;
    for (int spin = 0; spin < TK_SPIN_BUDGET && got < n; spin++) {
        long r = read(fd, p + got, (unsigned long)(n - got));
        if (r > 0) {
            got += (int)r;
            continue;
        }
        {
            vfs_pollfd_t pf;
            pf.fd = fd;
            pf.events = (int16_t)(VFS_POLLIN | VFS_POLLHUP);
            pf.revents = 0;
            poll(&pf, 1, 0);
            if ((pf.revents & VFS_POLLHUP) && !(pf.revents & VFS_POLLIN))
                return 0;
        }
        sched_yield();
    }
    return got == n;
}

int tk_send_frame(int fd, uint32_t type, const void *payload, uint32_t size)
{
    wsd_hdr_t h;
    h.magic = WSD_MAGIC;
    h.version = WSD_VERSION;
    h.flags = 0;
    h.msg_type = type;
    h.payload_size = size;
    if (!tk_send_all(fd, &h, (int)sizeof(h)))
        return 0;
    if (size > 0 && payload && !tk_send_all(fd, payload, (int)size))
        return 0;
    return 1;
}

int tk_connect(void)
{
    struct tsk_sockaddr_un ua;
    int fd = socket(TSK_AF_UNIX, TSK_SOCK_STREAM, 0);
    if (fd < 0)
        return -1;
    addr_fill(&ua, WSD_SOCKET_PATH);
    for (int spin = 0; spin < TK_SPIN_BUDGET; spin++) {
        if (connect(fd, &ua, (long)sizeof(ua)) == 0)
            return fd;
        sched_yield();
    }
    close(fd);
    return -1;
}

int tk_surface_create(int fd, uint32_t w, uint32_t h, tk_surface_t *out)
{
    wsd_create_req_t req;
    wsd_create_reply_t reply;
    wsd_hdr_t hd;

    if (!out || fd < 0)
        return -1;
    req.w = w;
    req.h = h;
    req.flags = WSD_SURF_FLAG_NONE;
    if (!tk_send_frame(fd, WSD_MSG_CREATE_SURFACE, &req, (uint32_t)sizeof(req)))
        return -1;
    if (!tk_recv_all(fd, &hd, (int)sizeof(hd)) ||
        hd.magic != WSD_MAGIC || hd.msg_type != WSD_MSG_CREATE_REPLY ||
        hd.payload_size != (uint32_t)sizeof(reply) ||
        !tk_recv_all(fd, &reply, (int)sizeof(reply)))
        return -1;
    if (reply.shm_id < 0)
        return -1;

    out->conn_fd = fd;
    out->surface_id = reply.surface_id;
    out->shm_id = reply.shm_id;
    out->w = reply.w;
    out->h = reply.h;
    out->px = (uint32_t *)shm_attach(reply.shm_id);
    if (!out->px)
        return -1;
    return 0;
}

void tk_surface_teardown(tk_surface_t *surf)
{
    wsd_surface_ref_t ref;
    if (!surf)
        return;
    if (surf->px) {
        shm_detach(surf->px);
        surf->px = 0;
    }
    if (surf->conn_fd >= 0) {
        ref.surface_id = surf->surface_id;
        tk_send_frame(surf->conn_fd, WSD_MSG_DESTROY_SURFACE, &ref,
                      (uint32_t)sizeof(ref));
    }
}
