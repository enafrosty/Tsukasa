/*
 * Project Tsukasa — Clipboard Client Implementation
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

#include "clipboard.h"
#include "service.h"
#include "../services/clipboardd.h"

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

static const char *get_vclip_socket_path(void)
{
    const char *env = getenv("VCLIP_SOCKET_PATH");
    if (env && env[0] != '\0')
        return env;

    static char sock_path[108];
    if (service_connect("clipboard", sock_path, sizeof(sock_path)) == 0)
        return sock_path;

    return VCLIP_SOCKET_PATH;
}

static int vclip_write_exact(int fd, const void *buf, size_t count)
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

static int vclip_read_exact(int fd, void *buf, size_t count)
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

static int vclip_connect(void)
{
    const char *sock_path = get_vclip_socket_path();

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

int clipboard_set(const char *text, size_t len)
{
    if (len > VCLIP_TEXT_MAX)
        return -EMSGSIZE;
    if (!text && len > 0)
        return -EINVAL;

    int fd = vclip_connect();
    if (fd < 0)
        return fd;

    vclip_req_hdr_t req;
    memset(&req, 0, sizeof(req));
    req.magic = VCLIP_MAGIC;
    req.op = VCLIP_OP_SET;
    req.text_len = (uint32_t)len;

    if (vclip_write_exact(fd, &req, sizeof(req)) < 0) {
        close_socket(fd);
        return -EIO;
    }

    if (len > 0) {
        if (vclip_write_exact(fd, text, len) < 0) {
            close_socket(fd);
            return -EIO;
        }
    }

    vclip_resp_hdr_t resp;
    memset(&resp, 0, sizeof(resp));
    if (vclip_read_exact(fd, &resp, sizeof(resp)) < 0 || resp.magic != VCLIP_MAGIC) {
        close_socket(fd);
        return -EIO;
    }

    close_socket(fd);
    return resp.status;
}

int clipboard_get(char *buf, size_t buf_len)
{
    if (!buf || buf_len == 0)
        return -EINVAL;

    int fd = vclip_connect();
    if (fd < 0)
        return fd;

    vclip_req_hdr_t req;
    memset(&req, 0, sizeof(req));
    req.magic = VCLIP_MAGIC;
    req.op = VCLIP_OP_GET;
    req.text_len = 0;

    if (vclip_write_exact(fd, &req, sizeof(req)) < 0) {
        close_socket(fd);
        return -EIO;
    }

    vclip_resp_hdr_t resp;
    memset(&resp, 0, sizeof(resp));
    if (vclip_read_exact(fd, &resp, sizeof(resp)) < 0 || resp.magic != VCLIP_MAGIC) {
        close_socket(fd);
        return -EIO;
    }

    if (resp.status < 0) {
        close_socket(fd);
        return resp.status;
    }

    if (resp.text_len == 0) {
        buf[0] = '\0';
        close_socket(fd);
        return 0;
    }

    size_t to_copy = (resp.text_len < buf_len) ? resp.text_len : (buf_len - 1);
    if (vclip_read_exact(fd, buf, to_copy) < 0) {
        close_socket(fd);
        return -EIO;
    }
    buf[to_copy] = '\0';

    if (resp.text_len > to_copy) {
        uint8_t discard[128];
        size_t rem = resp.text_len - to_copy;
        while (rem > 0) {
            size_t chunk = rem < sizeof(discard) ? rem : sizeof(discard);
            if (vclip_read_exact(fd, discard, chunk) < 0) {
                close_socket(fd);
                return -EIO;
            }
            rem -= chunk;
        }
    }

    close_socket(fd);
    return (int)to_copy;
}

int clipboard_clear(void)
{
    int fd = vclip_connect();
    if (fd < 0)
        return fd;

    vclip_req_hdr_t req;
    memset(&req, 0, sizeof(req));
    req.magic = VCLIP_MAGIC;
    req.op = VCLIP_OP_CLEAR;
    req.text_len = 0;

    if (vclip_write_exact(fd, &req, sizeof(req)) < 0) {
        close_socket(fd);
        return -EIO;
    }

    vclip_resp_hdr_t resp;
    memset(&resp, 0, sizeof(resp));
    if (vclip_read_exact(fd, &resp, sizeof(resp)) < 0 || resp.magic != VCLIP_MAGIC) {
        close_socket(fd);
        return -EIO;
    }

    close_socket(fd);
    return resp.status;
}

int clipboard_offer(const char * const *types, int type_count)
{
    (void)types;
    (void)type_count;
    return -ENOSYS;
}

int clipboard_fetch(const char *mime_type, char *out_buf, size_t out_len)
{
    (void)mime_type;
    (void)out_buf;
    (void)out_len;
    return -ENOSYS;
}
