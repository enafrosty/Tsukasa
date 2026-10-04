/*
 * Project Tsukasa — Service Discovery Client Implementation
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

#include "service.h"
#include "../services/registryd.h"

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

static const char *get_vreg_socket_path(void)
{
    const char *env = getenv("VREG_SOCKET_PATH");
    return (env && env[0] != '\0') ? env : VREG_SOCKET_PATH;
}

static int vreg_write_exact(int fd, const void *buf, size_t count)
{
    const uint8_t *p = (const uint8_t *)buf;
    size_t written = 0;
    int retries = 0;

    while (written < count) {
#if defined(_WIN32) && defined(VANILLA_HOST)
        int ret = send(fd, (const char *)(p + written), (int)(count - written), 0);
        if (ret > 0) {
            written += (size_t)ret;
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

static int vreg_read_exact(int fd, void *buf, size_t count)
{
    uint8_t *p = (uint8_t *)buf;
    size_t received = 0;
    int retries = 0;

    while (received < count) {
#if defined(_WIN32) && defined(VANILLA_HOST)
        int ret = recv(fd, (char *)(p + received), (int)(count - received), 0);
        if (ret > 0) {
            received += (size_t)ret;
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

static int vreg_client_rpc(const vreg_req_t *req, vreg_resp_t *resp)
{
    const char *sock_path = get_vreg_socket_path();

    int fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (fd < 0) {
#if defined(_WIN32) && defined(VANILLA_HOST)
        return -ECONNREFUSED;
#else
        return -errno;
#endif
    }

    struct sockaddr_un addr;
    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, sock_path, sizeof(addr.sun_path) - 1);

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

    if (vreg_write_exact(fd, req, sizeof(*req)) < 0) {
        close_socket(fd);
        return -EIO;
    }

    if (vreg_read_exact(fd, resp, sizeof(*resp)) < 0) {
        close_socket(fd);
        return -EIO;
    }

    close_socket(fd);

    if (resp->magic != VREG_MAGIC)
        return -EIO;

    return 0;
}

int service_register(const char *name, const char *socket_path)
{
    if (!name || !socket_path)
        return -EINVAL;

    size_t nlen = strlen(name);
    size_t plen = strlen(socket_path);

    if (nlen == 0 || nlen >= VREG_NAME_MAX)
        return -EINVAL;
    if (plen == 0 || plen >= VREG_PATH_MAX)
        return -EINVAL;

    vreg_req_t req;
    vreg_resp_t resp;

    memset(&req, 0, sizeof(req));
    memset(&resp, 0, sizeof(resp));

    req.magic = VREG_MAGIC;
    req.op = VREG_OP_REGISTER;
    strncpy(req.name, name, sizeof(req.name) - 1);
    strncpy(req.path, socket_path, sizeof(req.path) - 1);

    int rc = vreg_client_rpc(&req, &resp);
    if (rc < 0)
        return rc;

    return resp.status;
}

int service_unregister(const char *name)
{
    if (!name)
        return -EINVAL;

    size_t nlen = strlen(name);
    if (nlen == 0 || nlen >= VREG_NAME_MAX)
        return -EINVAL;

    vreg_req_t req;
    vreg_resp_t resp;

    memset(&req, 0, sizeof(req));
    memset(&resp, 0, sizeof(resp));

    req.magic = VREG_MAGIC;
    req.op = VREG_OP_UNREGISTER;
    strncpy(req.name, name, sizeof(req.name) - 1);

    int rc = vreg_client_rpc(&req, &resp);
    if (rc < 0)
        return rc;

    return resp.status;
}

int service_connect(const char *name, char *path_buf, size_t path_buf_len)
{
    if (!name || !path_buf || path_buf_len == 0)
        return -EINVAL;

    size_t nlen = strlen(name);
    if (nlen == 0 || nlen >= VREG_NAME_MAX)
        return -EINVAL;

    vreg_req_t req;
    vreg_resp_t resp;

    memset(&req, 0, sizeof(req));
    memset(&resp, 0, sizeof(resp));

    req.magic = VREG_MAGIC;
    req.op = VREG_OP_LOOKUP;
    strncpy(req.name, name, sizeof(req.name) - 1);

    int rc = vreg_client_rpc(&req, &resp);
    if (rc < 0)
        return rc;

    if (resp.status < 0)
        return resp.status;

    size_t plen = strlen(resp.path);
    if (plen >= path_buf_len)
        return -ENAMETOOLONG;

    strncpy(path_buf, resp.path, path_buf_len - 1);
    path_buf[path_buf_len - 1] = '\0';

    return 0;
}
