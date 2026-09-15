/*
 * Project Tsukasa — wapp15: guide-15 ring-3 display-server test client
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

#include "tsukasa_sdk.h"
#include "../../include/socket_defs.h"
#include "../../include/display_proto.h"

#include <stdint.h>
#include <stddef.h>

#define APP_W 200u
#define APP_H 120u
#define APP_PATTERN 0xFF33CC66u   /* opaque green — matches wsrv15 m2 check */

/* usock20's gate-proven spin scale; see the note in wsrv15.c — bigger budgets read as a hang on a... */
#define SPIN_BUDGET 20000

static int str_eq(const char *a, const char *b)
{
    if (!a || !b)
        return 0;
    while (*a && *a == *b) {
        a++;
        b++;
    }
    return *a == *b;
}

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

/* Write all `n` bytes; write() can accept only a partial ring's worth at a time. */
static int send_all(int fd, const void *buf, int n)
{
    const char *p = (const char *)buf;
    int sent = 0;
    for (int spin = 0; spin < SPIN_BUDGET && sent < n; spin++) {
        long r = write(fd, p + sent, (size_t)(n - sent));
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

/* Read exactly `n` bytes, polling for HUP so a dead server ends the loop instead of spinning the full budget. */
static int recv_all(int fd, void *buf, int n)
{
    char *p = (char *)buf;
    int got = 0;
    for (int spin = 0; spin < SPIN_BUDGET && got < n; spin++) {
        long r = read(fd, p + got, (size_t)(n - got));
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

static int send_frame(int fd, uint32_t type, const void *payload, uint32_t size)
{
    wsd_hdr_t h;
    h.magic = WSD_MAGIC;
    h.version = WSD_VERSION;
    h.flags = 0;
    h.msg_type = type;
    h.payload_size = size;
    if (!send_all(fd, &h, (int)sizeof(h)))
        return 0;
    if (size > 0 && payload && !send_all(fd, payload, (int)size))
        return 0;
    return 1;
}

static int connect_retry(void)
{
    struct tsk_sockaddr_un ua;
    int fd = socket(TSK_AF_UNIX, TSK_SOCK_STREAM, 0);
    if (fd < 0)
        return -1;
    addr_fill(&ua, WSD_SOCKET_PATH);
    for (int spin = 0; spin < SPIN_BUDGET; spin++) {
        if (connect(fd, &ua, (long)sizeof(ua)) == 0)
            return fd;
        sched_yield();
    }
    close(fd);
    return -1;
}

/* Connect, CREATE_SURFACE, read the CREATE_REPLY. */
static int do_create(int *fd_out, wsd_create_reply_t *reply)
{
    wsd_create_req_t req;
    wsd_hdr_t h;
    int fd = connect_retry();
    if (fd < 0)
        return -1;

    req.w = APP_W;
    req.h = APP_H;
    req.flags = WSD_SURF_FLAG_NONE;
    if (!send_frame(fd, WSD_MSG_CREATE_SURFACE, &req, (uint32_t)sizeof(req))) {
        close(fd);
        return -2;
    }
    if (!recv_all(fd, &h, (int)sizeof(h)) ||
        h.magic != WSD_MAGIC || h.msg_type != WSD_MSG_CREATE_REPLY ||
        h.payload_size != (uint32_t)sizeof(*reply) ||
        !recv_all(fd, reply, (int)sizeof(*reply))) {
        close(fd);
        return -3;
    }
    *fd_out = fd;
    return 0;
}

/* Attach the surface, fill it with the known pattern, and DAMAGE it. */
static int draw_surface(int fd, const wsd_create_reply_t *reply, uint32_t **px_out)
{
    wsd_damage_t dmg;
    uint32_t *px;
    uint32_t count;

    *px_out = NULL;
    if (reply->shm_id < 0)
        return 31;
    px = (uint32_t *)shm_attach(reply->shm_id);
    if (!px)
        return 36;
    *px_out = px;

    count = reply->w * reply->h;
    for (uint32_t i = 0; i < count; i++)
        px[i] = APP_PATTERN;

    dmg.surface_id = reply->surface_id;
    dmg.x = 0;
    dmg.y = 0;
    dmg.w = (int32_t)reply->w;
    dmg.h = (int32_t)reply->h;
    if (!send_frame(fd, WSD_MSG_DAMAGE, &dmg, (uint32_t)sizeof(dmg)))
        return 35;
    return 0;
}

static int map_create_err(int rc)
{
    if (rc == -1)
        return 34;
    if (rc == -2)
        return 33;
    return 32;
}

static int run_handshake(void)
{
    wsd_create_reply_t reply;
    wsd_surface_ref_t ref;
    int fd = -1;
    int rc = do_create(&fd, &reply);

    if (rc != 0)
        return map_create_err(rc);
    if (reply.shm_id < 0)
        return 31;

    ref.surface_id = reply.surface_id;
    if (!send_frame(fd, WSD_MSG_DESTROY_SURFACE, &ref, (uint32_t)sizeof(ref))) {
        close(fd);
        return 30;
    }
    close(fd);
    return 0;
}

static int run_draw(void)
{
    wsd_create_reply_t reply;
    wsd_surface_ref_t ref;
    wsd_hdr_t h;
    wsd_evt_damage_ack_t ack;
    uint32_t *px = NULL;
    int fd = -1;
    int rc = do_create(&fd, &reply);
    int d;

    if (rc != 0)
        return map_create_err(rc);

    d = draw_surface(fd, &reply, &px);
    if (d != 0) {
        close(fd);
        return d;
    }

    /* Wait for the server's DAMAGE_ACK — the deterministic sequencing barrier proving the composite consumed our... */
    if (!recv_all(fd, &h, (int)sizeof(h)) ||
        h.magic != WSD_MAGIC || h.msg_type != WSD_EVT_DAMAGE_ACK ||
        h.payload_size != (uint32_t)sizeof(ack) ||
        !recv_all(fd, &ack, (int)sizeof(ack)) ||
        ack.surface_id != reply.surface_id) {
        close(fd);
        return 38;
    }

    /* Detach BEFORE asking the server to destroy: the region's owner is the server, and ipc/shm.c only reaps at... */
    if (px)
        shm_detach(px);
    ref.surface_id = reply.surface_id;
    send_frame(fd, WSD_MSG_DESTROY_SURFACE, &ref, (uint32_t)sizeof(ref));
    close(fd);
    return 0;
}

static int run_die(void)
{
    wsd_create_reply_t reply;
    uint32_t *px = NULL;
    int fd = -1;
    int rc = do_create(&fd, &reply);
    int d;

    if (rc != 0)
        return map_create_err(rc);

    d = draw_surface(fd, &reply, &px);
    if (d != 0) {
        close(fd);
        return d;
    }
    /* Exit WITHOUT destroy or close: process teardown closes the socket fd, the server observes HUP and must... */
    if (px)
        shm_detach(px);
    return 0;
}

int main(int argc, char **argv)
{
    if (argc < 2)
        return 29;
    if (str_eq(argv[1], "handshake"))
        return run_handshake();
    if (str_eq(argv[1], "draw"))
        return run_draw();
    if (str_eq(argv[1], "die"))
        return run_die();
    return 29;
}
