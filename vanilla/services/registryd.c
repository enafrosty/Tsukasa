/*
 * Project Tsukasa — Service Registry Daemon Implementation
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

#include "registryd.h"

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
#include <time.h>
#define close_socket(s) close(s)
#define unlink_file(p) unlink(p)
#define sleep_ms(ms) do { struct timespec ts = { 0, (ms) * 1000000 }; nanosleep(&ts, NULL); } while (0)
#endif

typedef struct {
    char name[VREG_NAME_MAX];
    char path[VREG_PATH_MAX];
    int  in_use;
} vreg_entry_t;

static vreg_entry_t g_entries[VREG_MAX_SERVICES];

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

static void vreg_handle_one(int client_fd)
{
    vreg_req_t req;
    vreg_resp_t resp;

    memset(&req, 0, sizeof(req));
    memset(&resp, 0, sizeof(resp));
    resp.magic = VREG_MAGIC;

    if (vreg_read_exact(client_fd, &req, sizeof(req)) < 0)
        return;

    if (req.magic != VREG_MAGIC) {
        resp.status = -EINVAL;
        (void)vreg_write_exact(client_fd, &resp, sizeof(resp));
        return;
    }

    req.name[VREG_NAME_MAX - 1] = '\0';
    req.path[VREG_PATH_MAX - 1] = '\0';

    switch (req.op) {
    case VREG_OP_REGISTER: {
        if (req.name[0] == '\0' || req.path[0] == '\0') {
            resp.status = -EINVAL;
            break;
        }

        int found_slot = -1;
        int free_slot = -1;
        for (int i = 0; i < VREG_MAX_SERVICES; i++) {
            if (g_entries[i].in_use) {
                if (strncmp(g_entries[i].name, req.name, VREG_NAME_MAX) == 0) {
                    found_slot = i;
                    break;
                }
            } else if (free_slot < 0) {
                free_slot = i;
            }
        }

        int target_slot = (found_slot >= 0) ? found_slot : free_slot;
        if (target_slot >= 0) {
            g_entries[target_slot].in_use = 1;
            strncpy(g_entries[target_slot].name, req.name, sizeof(g_entries[target_slot].name));
            g_entries[target_slot].name[sizeof(g_entries[target_slot].name) - 1] = '\0';
            strncpy(g_entries[target_slot].path, req.path, sizeof(g_entries[target_slot].path));
            g_entries[target_slot].path[sizeof(g_entries[target_slot].path) - 1] = '\0';
            resp.status = 0;
        } else {
            printf("[registryd] WARN: service registry full (max %d)\n", VREG_MAX_SERVICES);
            resp.status = -ENOSPC;
        }
        break;
    }

    case VREG_OP_UNREGISTER: {
        if (req.name[0] == '\0') {
            resp.status = -EINVAL;
            break;
        }

        int found = 0;
        for (int i = 0; i < VREG_MAX_SERVICES; i++) {
            if (g_entries[i].in_use &&
                strncmp(g_entries[i].name, req.name, VREG_NAME_MAX) == 0) {
                g_entries[i].in_use = 0;
                memset(g_entries[i].name, 0, sizeof(g_entries[i].name));
                memset(g_entries[i].path, 0, sizeof(g_entries[i].path));
                found = 1;
                break;
            }
        }

        resp.status = found ? 0 : -ENOENT;
        break;
    }

    case VREG_OP_LOOKUP: {
        if (req.name[0] == '\0') {
            resp.status = -EINVAL;
            break;
        }

        int found = 0;
        for (int i = 0; i < VREG_MAX_SERVICES; i++) {
            if (g_entries[i].in_use &&
                strncmp(g_entries[i].name, req.name, VREG_NAME_MAX) == 0) {
                strncpy(resp.path, g_entries[i].path, sizeof(resp.path));
                resp.path[sizeof(resp.path) - 1] = '\0';
                found = 1;
                break;
            }
        }

        resp.status = found ? 0 : -ENOENT;
        break;
    }

    default:
        resp.status = -EINVAL;
        break;
    }

    (void)vreg_write_exact(client_fd, &resp, sizeof(resp));
}

int registryd_run(const char *sock_path, volatile int *stop_flag)
{
    volatile int default_stop = 0;
    if (!stop_flag)
        stop_flag = &default_stop;

    memset(g_entries, 0, sizeof(g_entries));

    unlink_file(sock_path);

    int listen_fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (listen_fd < 0) {
        printf("[registryd] Failed to create socket: %d\n", errno);
        return 1;
    }

    struct sockaddr_un addr;
    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, sock_path, sizeof(addr.sun_path) - 1);

    if (bind(listen_fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        printf("[registryd] Failed to bind to %s: %d\n", sock_path, errno);
        close_socket(listen_fd);
        return 1;
    }

    if (listen(listen_fd, 16) < 0) {
        printf("[registryd] Failed to listen on socket: %d\n", errno);
        close_socket(listen_fd);
        return 1;
    }

    printf("[registryd] Service registry daemon listening on %s\n", sock_path);
    fflush(stdout);

    while (!*stop_flag) {
        int client_fd = accept(listen_fd, NULL, NULL);
        if (client_fd >= 0) {
            vreg_handle_one(client_fd);
            close_socket(client_fd);
        } else {
            sleep_ms(1);
        }
    }

    close_socket(listen_fd);
    unlink_file(sock_path);
    return 0;
}

#ifndef VREG_TEST_NO_MAIN
int main(int argc, char **argv)
{
    const char *sock_path = VREG_SOCKET_PATH;
    if (argc > 1 && argv[1] && argv[1][0] != '\0')
        sock_path = argv[1];

    volatile int stop_flag = 0;
    return registryd_run(sock_path, &stop_flag);
}
#endif
