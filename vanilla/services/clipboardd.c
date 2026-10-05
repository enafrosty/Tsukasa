/*
 * Project Tsukasa — Clipboard Daemon Implementation
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

#include "clipboardd.h"
#include "../libvanilla/service.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

#if defined(_WIN32) && defined(VANILLA_HOST)
#include <winsock2.h>
#include <afunix.h>
#include <ws2tcpip.h>
#define close_socket(s) closesocket(s)
#define unlink_file(p) remove(p)
#define sleep_ms(ms) Sleep(ms)
#else
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>
#include <fcntl.h>
#include <time.h>
#include <signal.h>
#define close_socket(s) close(s)
#define unlink_file(p) unlink(p)
#define sleep_ms(ms) do { struct timespec ts = { 0, (ms) * 1000000 }; nanosleep(&ts, NULL); } while (0)
#endif

static char    *g_clip_buf = NULL;
static uint32_t g_clip_len = 0;

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

static void vclip_handle_one(int client_fd)
{
    vclip_req_hdr_t req;
    vclip_resp_hdr_t resp;

    memset(&req, 0, sizeof(req));
    memset(&resp, 0, sizeof(resp));
    resp.magic = VCLIP_MAGIC;

    if (vclip_read_exact(client_fd, &req, sizeof(req)) < 0)
        return;

    if (req.magic != VCLIP_MAGIC) {
        resp.status = -EINVAL;
        (void)vclip_write_exact(client_fd, &resp, sizeof(resp));
        return;
    }

    switch (req.op) {
    case VCLIP_OP_SET: {
        if (req.text_len > VCLIP_TEXT_MAX) {
            resp.status = -EMSGSIZE;
            (void)vclip_write_exact(client_fd, &resp, sizeof(resp));
            break;
        }

        if (req.text_len == 0) {
            if (g_clip_buf) {
                free(g_clip_buf);
                g_clip_buf = NULL;
            }
            g_clip_len = 0;
            resp.status = 0;
            resp.text_len = 0;
            (void)vclip_write_exact(client_fd, &resp, sizeof(resp));
            break;
        }

        char *buf = (char *)malloc(req.text_len + 1);
        if (!buf) {
            resp.status = -ENOMEM;
            (void)vclip_write_exact(client_fd, &resp, sizeof(resp));
            break;
        }

        if (vclip_read_exact(client_fd, buf, req.text_len) < 0) {
            free(buf);
            return;
        }
        buf[req.text_len] = '\0';

        if (g_clip_buf)
            free(g_clip_buf);
        g_clip_buf = buf;
        g_clip_len = req.text_len;

        resp.status = 0;
        resp.text_len = 0;
        (void)vclip_write_exact(client_fd, &resp, sizeof(resp));
        break;
    }

    case VCLIP_OP_GET: {
        if (req.text_len != 0) {
            resp.status = -EINVAL;
            (void)vclip_write_exact(client_fd, &resp, sizeof(resp));
            break;
        }
        resp.status = 0;
        resp.text_len = g_clip_len;
        if (vclip_write_exact(client_fd, &resp, sizeof(resp)) < 0)
            break;
        if (g_clip_len > 0 && g_clip_buf) {
            (void)vclip_write_exact(client_fd, g_clip_buf, g_clip_len);
        }
        break;
    }

    case VCLIP_OP_CLEAR: {
        if (req.text_len != 0) {
            resp.status = -EINVAL;
            (void)vclip_write_exact(client_fd, &resp, sizeof(resp));
            break;
        }
        if (g_clip_buf) {
            free(g_clip_buf);
            g_clip_buf = NULL;
        }
        g_clip_len = 0;
        resp.status = 0;
        resp.text_len = 0;
        (void)vclip_write_exact(client_fd, &resp, sizeof(resp));
        break;
    }

    default:
        resp.status = -EINVAL;
        (void)vclip_write_exact(client_fd, &resp, sizeof(resp));
        break;
    }
}

int clipboardd_run(const char *sock_path, volatile int *stop_flag)
{
    volatile int default_stop = 0;
    if (!stop_flag)
        stop_flag = &default_stop;

#ifdef SIGPIPE
    signal(SIGPIPE, SIG_IGN);
#endif

    unlink_file(sock_path);

    int listen_fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (listen_fd < 0) {
        printf("[clipboardd] Failed to create socket: %d\n", errno);
        return 1;
    }

#if defined(_WIN32) && defined(VANILLA_HOST)
    u_long nonblock_mode = 1;
    ioctlsocket(listen_fd, FIONBIO, &nonblock_mode);
#else
    int flags = fcntl(listen_fd, F_GETFL, 0);
    if (flags >= 0)
        fcntl(listen_fd, F_SETFL, flags | O_NONBLOCK);
#endif

    struct sockaddr_un addr;
    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, sock_path, sizeof(addr.sun_path) - 1);

    if (bind(listen_fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        printf("[clipboardd] Failed to bind to %s: %d\n", sock_path, errno);
        close_socket(listen_fd);
        return 1;
    }

    if (listen(listen_fd, 16) < 0) {
        printf("[clipboardd] Failed to listen on socket: %d\n", errno);
        close_socket(listen_fd);
        return 1;
    }

    printf("[clipboardd] Clipboard daemon listening on %s\n", sock_path);
    fflush(stdout);

    int registered = 0;
    if (!getenv("VCLIP_NO_REGISTER")) {
        for (int retry = 0; retry < 30; retry++) {
            if (*stop_flag)
                break;
            if (service_register("clipboard", sock_path) == 0) {
                registered = 1;
                break;
            }
            sleep_ms(100);
        }
        if (!registered && !*stop_flag) {
            printf("[clipboardd] WARN: failed to register with registryd\n");
            fflush(stdout);
        }
    }

    while (!*stop_flag) {
        int client_fd = accept(listen_fd, NULL, NULL);
        if (client_fd >= 0) {
            vclip_handle_one(client_fd);
            close_socket(client_fd);
        } else {
            sleep_ms(1);
        }
    }

    if (registered)
        service_unregister("clipboard");

    close_socket(listen_fd);
    unlink_file(sock_path);

    if (g_clip_buf) {
        free(g_clip_buf);
        g_clip_buf = NULL;
    }
    g_clip_len = 0;

    return 0;
}

#ifndef VCLIP_TEST_NO_MAIN
int main(int argc, char **argv)
{
    const char *sock_path = VCLIP_SOCKET_PATH;
    if (argc > 1 && argv[1] && argv[1][0] != '\0')
        sock_path = argv[1];

    volatile int stop_flag = 0;
    return clipboardd_run(sock_path, &stop_flag);
}
#endif
