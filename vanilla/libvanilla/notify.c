/*
 * Project Tsukasa — Notification Client Library Implementation
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

#include "notify.h"
#include "service.h"
#include "../services/notifyd.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

#if defined(_WIN32) && defined(VANILLA_HOST)
#include <winsock2.h>
#include <afunix.h>
#include <ws2tcpip.h>
#define close_socket(s) closesocket(s)
#define sleep_ms(ms) Sleep(ms)
#else
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>
#include <time.h>
#define close_socket(s) close(s)
#define sleep_ms(ms) do { struct timespec ts = { 0, (ms) * 1000000 }; nanosleep(&ts, NULL); } while (0)
#endif

#define MAX_CLIENT_NOTIFS 16

typedef struct {
    uint32_t notif_id;
    int      fd;
} client_notif_entry_t;

static client_notif_entry_t g_client_notifs[MAX_CLIENT_NOTIFS];

static int vnotif_write_exact(int fd, const void *buf, size_t count)
{
    const uint8_t *p = (const uint8_t *)buf;
    size_t written = 0;
    int retries = 0;

    while (written < count) {
#if defined(_WIN32) && defined(VANILLA_HOST)
        int ret = send(fd, (const char *)(p + written), (int)(count - written), 0);
        if (ret > 0) {
            written += (size_t)ret;
            retries = 0;
        } else if (ret == 0) {
            return -1;
        } else {
            int err = WSAGetLastError();
            if (err == WSAEWOULDBLOCK || err == WSAEINTR) {
                if (++retries > 1000)
                    return -1;
                sleep_ms(1);
                continue;
            }
            return -1;
        }
#else
        ssize_t ret = write(fd, p + written, count - written);
        if (ret > 0) {
            written += (size_t)ret;
            retries = 0;
        } else if (ret == 0) {
            return -1;
        } else {
            if (errno == EAGAIN || errno == EINTR) {
                if (++retries > 1000)
                    return -1;
                sleep_ms(1);
                continue;
            }
            return -1;
        }
#endif
    }
    return 0;
}

static int vnotif_read_exact(int fd, void *buf, size_t count)
{
    uint8_t *p = (uint8_t *)buf;
    size_t received = 0;
    int retries = 0;

    while (received < count) {
#if defined(_WIN32) && defined(VANILLA_HOST)
        int ret = recv(fd, (char *)(p + received), (int)(count - received), 0);
        if (ret > 0) {
            received += (size_t)ret;
            retries = 0;
        } else if (ret == 0) {
            return -1;
        } else {
            int err = WSAGetLastError();
            if (err == WSAEWOULDBLOCK || err == WSAEINTR) {
                if (++retries > 1000)
                    return -1;
                sleep_ms(1);
                continue;
            }
            return -1;
        }
#else
        ssize_t ret = read(fd, p + received, count - received);
        if (ret > 0) {
            received += (size_t)ret;
            retries = 0;
        } else if (ret == 0) {
            return -1;
        } else {
            if (errno == EAGAIN || errno == EINTR) {
                if (++retries > 1000)
                    return -1;
                sleep_ms(1);
                continue;
            }
            return -1;
        }
#endif
    }
    return 0;
}

static int vnotif_connect(void)
{
    char sock_buf[108];
    const char *sock_path = getenv("VNOTIF_SOCKET_PATH");
    if (!sock_path || sock_path[0] == '\0') {
        if (service_connect("notification", sock_buf, sizeof(sock_buf)) == 0 && sock_buf[0] != '\0') {
            sock_path = sock_buf;
        } else {
            sock_path = VNOTIF_SOCKET_PATH;
        }
    }

    int fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (fd < 0) {
#if defined(_WIN32) && defined(VANILLA_HOST)
        return -ECONNREFUSED;
#else
        return errno ? -errno : -ECONNREFUSED;
#endif
    }

    struct sockaddr_un addr;
    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, sock_path, sizeof(addr.sun_path) - 1);
    addr.sun_path[sizeof(addr.sun_path) - 1] = '\0';

    if (connect(fd, (const struct sockaddr *)&addr, sizeof(addr)) < 0) {
        close_socket(fd);
#if defined(_WIN32) && defined(VANILLA_HOST)
        return -ECONNREFUSED;
#else
        if (errno == ENOENT || errno == ECONNREFUSED)
            return -ECONNREFUSED;
        return errno ? -errno : -ECONNREFUSED;
#endif
    }

    return fd;
}

int32_t notify_send(const char *title, const char *body, const char *icon_name,
                    uint32_t timeout_ms,
                    const vnotif_action_t *actions, int action_count)
{
    if (!title)
        return -EINVAL;
    if (action_count < 0 || action_count > VNOTIF_ACTION_MAX)
        return -EINVAL;
    if (action_count > 0 && !actions)
        return -EINVAL;

    vnotif_send_t req;
    memset(&req, 0, sizeof(req));
    req.magic = VNOTIF_MAGIC;
    req.op = VNOTIF_OP_SEND;
    req.action_count = (uint8_t)action_count;
    req.timeout_ms = timeout_ms;

    strncpy(req.title, title, sizeof(req.title) - 1);
    req.title[sizeof(req.title) - 1] = '\0';

    if (body) {
        strncpy(req.body, body, sizeof(req.body) - 1);
        req.body[sizeof(req.body) - 1] = '\0';
    }

    if (icon_name) {
        strncpy(req.icon_name, icon_name, sizeof(req.icon_name) - 1);
        req.icon_name[sizeof(req.icon_name) - 1] = '\0';
    }

    if (action_count > 0) {
        memcpy(req.actions, actions, sizeof(vnotif_action_t) * (size_t)action_count);
    }

    int fd = vnotif_connect();
    if (fd < 0)
        return fd;

    if (vnotif_write_exact(fd, &req, sizeof(req)) < 0) {
        close_socket(fd);
        return -EIO;
    }

    vnotif_resp_t resp;
    if (vnotif_read_exact(fd, &resp, sizeof(resp)) < 0 || resp.magic != VNOTIF_MAGIC) {
        close_socket(fd);
        return -EIO;
    }

    if (resp.status != 0) {
        close_socket(fd);
        return resp.status;
    }

    if (action_count > 0) {
        int saved = 0;
        for (int i = 0; i < MAX_CLIENT_NOTIFS; i++) {
            if (g_client_notifs[i].notif_id == 0) {
                g_client_notifs[i].notif_id = resp.notif_id;
                g_client_notifs[i].fd = fd;
                saved = 1;
                break;
            }
        }
        if (!saved) {
            /* Close oldest if full */
            close_socket(g_client_notifs[0].fd);
            g_client_notifs[0].notif_id = resp.notif_id;
            g_client_notifs[0].fd = fd;
        }
    } else {
        close_socket(fd);
    }

    return (int32_t)resp.notif_id;
}

int notify_close(uint32_t notif_id)
{
    if (notif_id == 0)
        return -EINVAL;

    for (int i = 0; i < MAX_CLIENT_NOTIFS; i++) {
        if (g_client_notifs[i].notif_id == notif_id) {
            close_socket(g_client_notifs[i].fd);
            g_client_notifs[i].notif_id = 0;
            g_client_notifs[i].fd = -1;
            break;
        }
    }

    int fd = vnotif_connect();
    if (fd < 0)
        return fd;

    vnotif_close_t req;
    memset(&req, 0, sizeof(req));
    req.magic = VNOTIF_MAGIC;
    req.op = VNOTIF_OP_CLOSE;
    req.notif_id = notif_id;

    if (vnotif_write_exact(fd, &req, sizeof(req)) < 0) {
        close_socket(fd);
        return -EIO;
    }

    vnotif_resp_t resp;
    if (vnotif_read_exact(fd, &resp, sizeof(resp)) < 0 || resp.magic != VNOTIF_MAGIC) {
        close_socket(fd);
        return -EIO;
    }

    close_socket(fd);
    return resp.status;
}

int notify_wait_action(uint32_t notif_id, uint32_t *out_action_id)
{
    if (notif_id == 0 || !out_action_id)
        return -EINVAL;

    int slot = -1;
    for (int i = 0; i < MAX_CLIENT_NOTIFS; i++) {
        if (g_client_notifs[i].notif_id == notif_id) {
            slot = i;
            break;
        }
    }

    if (slot < 0)
        return -ENOENT;

    int fd = g_client_notifs[slot].fd;
    vnotif_event_t evt;
    if (vnotif_read_exact(fd, &evt, sizeof(evt)) < 0 ||
        evt.magic != VNOTIF_MAGIC || evt.op != VNOTIF_OP_EVENT) {
        close_socket(fd);
        g_client_notifs[slot].notif_id = 0;
        g_client_notifs[slot].fd = -1;
        return -EIO;
    }

    *out_action_id = evt.action_id;
    close_socket(fd);
    g_client_notifs[slot].notif_id = 0;
    g_client_notifs[slot].fd = -1;

    return 0;
}
