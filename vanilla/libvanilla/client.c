/*
 * Project Tsukasa — Vanilla Display Server Client Library Implementation
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

#include "../include/vanilla.h"
#include <stdio.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/poll.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

#define VANILLA_EVENT_QUEUE_CAP 16

struct vanilla_client {
    int                      socket_fd;
    uint32_t                 server_version;
    vanilla_frame_callback_t frame_cb;
    vanilla_window_t         *windows;
    vanilla_event_t          event_queue[VANILLA_EVENT_QUEUE_CAP];
    size_t                   event_head;
    size_t                   event_tail;
    size_t                   event_count;
};

static vanilla_window_t *find_window(vanilla_client_t *client, uint32_t window_id)
{
    if (!client || window_id == 0)
        return NULL;
    for (vanilla_window_t *w = client->windows; w; w = w->next) {
        if (w->window_id == window_id)
            return w;
    }
    return NULL;
}

static void event_queue_push(vanilla_client_t *client, const vanilla_event_t *ev)
{
    if (client->event_count < VANILLA_EVENT_QUEUE_CAP) {
        client->event_queue[client->event_tail] = *ev;
        client->event_tail = (client->event_tail + 1) % VANILLA_EVENT_QUEUE_CAP;
        client->event_count++;
    }
}

static int event_queue_pop(vanilla_client_t *client, vanilla_event_t *out_ev)
{
    if (client->event_count == 0)
        return 0;

    *out_ev = client->event_queue[client->event_head];
    client->event_head = (client->event_head + 1) % VANILLA_EVENT_QUEUE_CAP;
    client->event_count--;
    return 1;
}

static int exact_write(int fd, const void *buf, size_t count)
{
    const uint8_t *p = (const uint8_t *)buf;
    size_t written = 0;
    int retries = 0;

    while (written < count) {
        ssize_t ret = write(fd, p + written, count - written);
        if (ret > 0) {
            written += (size_t)ret;
        } else if (ret == 0) {
            return -1;
        } else {
            if (errno == EAGAIN || errno == EINTR) {
                if (++retries > 1000)
                    return -1;
                sched_yield();
                continue;
            }
            return -1;
        }
    }
    return 0;
}

static int exact_read(int fd, void *buf, size_t count)
{
    uint8_t *p = (uint8_t *)buf;
    size_t received = 0;
    int retries = 0;

    while (received < count) {
        ssize_t ret = read(fd, p + received, count - received);
        if (ret > 0) {
            received += (size_t)ret;
        } else if (ret == 0) {
            return -1;
        } else {
            if (errno == EAGAIN || errno == EINTR) {
                if (++retries > 1000)
                    return -1;
                sched_yield();
                continue;
            }
            return -1;
        }
    }
    return 0;
}

vanilla_client_t *vanilla_connect(const char *socket_path)
{
    const char *path = socket_path ? socket_path : VANILLA_SOCKET_PATH;
    struct sockaddr_un addr;
    vanilla_msg_hdr_t hdr;
    vanilla_msg_hello_t hello;
    vanilla_msg_hdr_t ack_hdr;
    vanilla_msg_hello_ack_t ack_body;
    vanilla_client_t *client;
    int fd;

    fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (fd < 0) {
        return NULL;
    }

    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, path, sizeof(addr.sun_path) - 1);

    if (connect(fd, (const struct sockaddr *)&addr, sizeof(addr)) < 0) {
        close(fd);
        return NULL;
    }

    hdr.magic = VANILLA_IPC_MAGIC;
    hdr.msg_type = MSG_HELLO;
    hdr.payload_len = (uint16_t)sizeof(hello);
    hdr.window_id = 0;

    hello.client_version = VANILLA_IPC_VERSION;
    memset(hello.client_name, 0, sizeof(hello.client_name));
    strncpy(hello.client_name, "vanilla-client", sizeof(hello.client_name) - 1);

    if (exact_write(fd, &hdr, sizeof(hdr)) < 0 ||
        exact_write(fd, &hello, sizeof(hello)) < 0) {
        close(fd);
        return NULL;
    }

    if (exact_read(fd, &ack_hdr, sizeof(ack_hdr)) < 0) {
        close(fd);
        return NULL;
    }

    if (ack_hdr.magic != VANILLA_IPC_MAGIC || ack_hdr.msg_type != MSG_HELLO_ACK) {
        close(fd);
        return NULL;
    }

    if (ack_hdr.payload_len < sizeof(ack_body)) {
        close(fd);
        return NULL;
    }

    if (exact_read(fd, &ack_body, sizeof(ack_body)) < 0) {
        close(fd);
        return NULL;
    }

    if (ack_body.status != 0) {
        close(fd);
        return NULL;
    }

    client = (vanilla_client_t *)malloc(sizeof(vanilla_client_t));
    if (!client) {
        close(fd);
        return NULL;
    }

    client->socket_fd = fd;
    client->server_version = ack_body.server_version;
    client->frame_cb = NULL;
    client->windows = NULL;
    client->event_head = 0;
    client->event_tail = 0;
    client->event_count = 0;

    return client;
}

void vanilla_set_frame_callback(vanilla_client_t *client, vanilla_frame_callback_t fn)
{
    if (client)
        client->frame_cb = fn;
}

int vanilla_ack_configure(vanilla_client_t *client, uint32_t window_id, uint32_t serial)
{
    if (!client || client->socket_fd < 0)
        return -1;

    if (client->server_version < 2)
        return 0;

    vanilla_msg_hdr_t hdr;
    vanilla_msg_ack_configure_t ack;

    hdr.magic = VANILLA_IPC_MAGIC;
    hdr.msg_type = MSG_ACK_CONFIGURE;
    hdr.payload_len = (uint16_t)sizeof(ack);
    hdr.window_id = window_id;

    ack.window_id = window_id;
    ack.serial = serial;

    if (exact_write(client->socket_fd, &hdr, sizeof(hdr)) < 0 ||
        exact_write(client->socket_fd, &ack, sizeof(ack)) < 0) {
        return -1;
    }
    return 0;
}

int vanilla_set_size_hints(vanilla_client_t *client, uint32_t window_id,
                           uint32_t min_w, uint32_t min_h,
                           uint32_t max_w, uint32_t max_h,
                           uint32_t asp_n, uint32_t asp_d)
{
    if (!client || client->socket_fd < 0)
        return -1;

    if (client->server_version < 2)
        return 0;

    vanilla_msg_hdr_t hdr;
    vanilla_msg_set_size_hints_t msg;

    hdr.magic = VANILLA_IPC_MAGIC;
    hdr.msg_type = MSG_SET_SIZE_HINTS;
    hdr.payload_len = (uint16_t)sizeof(msg);
    hdr.window_id = window_id;

    msg.window_id = window_id;
    msg.min_width = min_w;
    msg.min_height = min_h;
    msg.max_width = max_w;
    msg.max_height = max_h;
    msg.aspect_num = asp_n;
    msg.aspect_den = asp_d;

    if (exact_write(client->socket_fd, &hdr, sizeof(hdr)) < 0 ||
        exact_write(client->socket_fd, &msg, sizeof(msg)) < 0) {
        return -1;
    }
    return 0;
}

void vanilla_disconnect(vanilla_client_t *client)
{
    if (!client)
        return;

    while (client->windows)
        vanilla_destroy_window(client->windows);

    if (client->socket_fd >= 0)
        close(client->socket_fd);

    free(client);
}

vanilla_window_t *vanilla_create_window(vanilla_client_t *client, const char *title,
                                        int x, int y, int w, int h, uint32_t flags)
{
    vanilla_msg_hdr_t hdr;
    vanilla_msg_create_window_t req;
    vanilla_msg_hdr_t ack_hdr;
    vanilla_msg_create_window_ack_t ack;
    vanilla_window_t *win;

    if (!client || w <= 0 || h <= 0)
        return NULL;

    if ((uint32_t)w > VANILLA_MAX_WIDTH || (uint32_t)h > VANILLA_MAX_HEIGHT)
        return NULL;

    hdr.magic = VANILLA_IPC_MAGIC;
    hdr.msg_type = MSG_CREATE_WINDOW;
    hdr.payload_len = (uint16_t)sizeof(req);
    hdr.window_id = 0;

    memset(&req, 0, sizeof(req));
    if (title)
        strncpy(req.title, title, sizeof(req.title) - 1);
    req.x = x;
    req.y = y;
    req.width = (uint32_t)w;
    req.height = (uint32_t)h;
    req.flags = flags;

    if (exact_write(client->socket_fd, &hdr, sizeof(hdr)) < 0 ||
        exact_write(client->socket_fd, &req, sizeof(req)) < 0)
        return NULL;

    for (;;) {
        if (exact_read(client->socket_fd, &ack_hdr, sizeof(ack_hdr)) < 0)
            return NULL;

        if (ack_hdr.magic != VANILLA_IPC_MAGIC)
            return NULL;

        if (ack_hdr.msg_type == MSG_CREATE_WINDOW_ACK)
            break;

        if (ack_hdr.msg_type == MSG_INPUT_EVENT) {
            vanilla_msg_input_event_t in_msg;
            if (exact_read(client->socket_fd, &in_msg, sizeof(in_msg)) < 0)
                return NULL;
            vanilla_event_t ev;
            ev.type = VANILLA_EVENT_INPUT;
            ev.window_id = ack_hdr.window_id;
            ev.input = in_msg.event;
            event_queue_push(client, &ev);
        } else if (ack_hdr.msg_type == MSG_WINDOW_FOCUS) {
            vanilla_msg_window_focus_t f_msg;
            if (exact_read(client->socket_fd, &f_msg, sizeof(f_msg)) < 0)
                return NULL;
            vanilla_event_t ev;
            ev.type = VANILLA_EVENT_FOCUS;
            ev.window_id = ack_hdr.window_id;
            ev.focus.focused = (int)f_msg.focused;
            event_queue_push(client, &ev);
        } else if (ack_hdr.msg_type == MSG_WINDOW_CONFIGURE) {
            vanilla_event_t ev;
            ev.type = VANILLA_EVENT_CONFIGURE;
            ev.window_id = ack_hdr.window_id;
            uint32_t cfg_serial = 0;
            if (ack_hdr.payload_len >= sizeof(vanilla_msg_window_configure_v2_t)) {
                vanilla_msg_window_configure_v2_t c_msg;
                if (exact_read(client->socket_fd, &c_msg, sizeof(c_msg)) < 0)
                    return NULL;
                ev.configure.x = c_msg.x;
                ev.configure.y = c_msg.y;
                ev.configure.width = c_msg.width;
                ev.configure.height = c_msg.height;
                ev.configure.serial = c_msg.serial;
                ev.configure.flags = c_msg.flags;
                cfg_serial = c_msg.serial;
                if (ack_hdr.payload_len > sizeof(c_msg)) {
                    uint8_t discard[128];
                    size_t rem = ack_hdr.payload_len - sizeof(c_msg);
                    while (rem > 0) {
                        size_t chunk = rem < sizeof(discard) ? rem : sizeof(discard);
                        if (exact_read(client->socket_fd, discard, chunk) < 0)
                            return NULL;
                        rem -= chunk;
                    }
                }
            } else {
                vanilla_msg_window_configure_t c_msg;
                if (exact_read(client->socket_fd, &c_msg, sizeof(c_msg)) < 0)
                    return NULL;
                ev.configure.x = c_msg.x;
                ev.configure.y = c_msg.y;
                ev.configure.width = c_msg.width;
                ev.configure.height = c_msg.height;
                ev.configure.serial = 0;
                ev.configure.flags = 0;
                if (ack_hdr.payload_len > sizeof(c_msg)) {
                    uint8_t discard[128];
                    size_t rem = ack_hdr.payload_len - sizeof(c_msg);
                    while (rem > 0) {
                        size_t chunk = rem < sizeof(discard) ? rem : sizeof(discard);
                        if (exact_read(client->socket_fd, discard, chunk) < 0)
                            return NULL;
                        rem -= chunk;
                    }
                }
            }
            vanilla_window_t *target_win = find_window(client, ack_hdr.window_id);
            if (target_win) {
                target_win->x = ev.configure.x;
                target_win->y = ev.configure.y;
                target_win->width = ev.configure.width;
                target_win->height = ev.configure.height;
                if (ev.configure.flags != 0)
                    target_win->flags = ev.configure.flags;
            }
            if (cfg_serial != 0) {
                vanilla_ack_configure(client, ack_hdr.window_id, cfg_serial);
            }
            event_queue_push(client, &ev);
        } else if (ack_hdr.msg_type == MSG_FRAME_BEGIN) {
            vanilla_msg_frame_begin_t fb;
            if (exact_read(client->socket_fd, &fb, sizeof(fb)) < 0)
                return NULL;
            if (ack_hdr.payload_len > sizeof(fb)) {
                uint8_t discard[128];
                size_t rem = ack_hdr.payload_len - sizeof(fb);
                while (rem > 0) {
                    size_t chunk = rem < sizeof(discard) ? rem : sizeof(discard);
                    if (exact_read(client->socket_fd, discard, chunk) < 0)
                        return NULL;
                    rem -= chunk;
                }
            }
            vanilla_window_t *target_win = find_window(client, ack_hdr.window_id ? ack_hdr.window_id : fb.window_id);
            if (target_win) {
                target_win->last_frame_serial = fb.frame_serial;
            }
            vanilla_event_t ev;
            ev.type = VANILLA_EVENT_FRAME_BEGIN;
            ev.window_id = ack_hdr.window_id ? ack_hdr.window_id : fb.window_id;
            ev.frame_begin.frame_serial = fb.frame_serial;
            ev.frame_begin.timestamp_ms = fb.timestamp_ms;
            event_queue_push(client, &ev);
        } else if (ack_hdr.msg_type == MSG_BUFFER_RELEASED) {
            vanilla_msg_buffer_released_t br;
            if (exact_read(client->socket_fd, &br, sizeof(br)) < 0)
                return NULL;
            if (ack_hdr.payload_len > sizeof(br)) {
                uint8_t discard[128];
                size_t rem = ack_hdr.payload_len - sizeof(br);
                while (rem > 0) {
                    size_t chunk = rem < sizeof(discard) ? rem : sizeof(discard);
                    if (exact_read(client->socket_fd, discard, chunk) < 0)
                        return NULL;
                    rem -= chunk;
                }
            }
            vanilla_event_t ev;
            ev.type = VANILLA_EVENT_BUFFER_RELEASED;
            ev.window_id = br.window_id;
            ev.buffer_released.serial = br.serial;
            event_queue_push(client, &ev);
        } else if (ack_hdr.msg_type == MSG_WINDOW_CLOSE_REQ) {
            if (ack_hdr.payload_len > 0) {
                uint8_t discard[128];
                size_t rem = ack_hdr.payload_len;
                while (rem > 0) {
                    size_t chunk = rem < sizeof(discard) ? rem : sizeof(discard);
                    if (exact_read(client->socket_fd, discard, chunk) < 0)
                        return NULL;
                    rem -= chunk;
                }
            }
            vanilla_event_t ev;
            ev.type = VANILLA_EVENT_CLOSE_REQ;
            ev.window_id = ack_hdr.window_id;
            event_queue_push(client, &ev);
        } else {
            if (ack_hdr.payload_len > 0) {
                uint8_t discard[128];
                size_t rem = ack_hdr.payload_len;
                while (rem > 0) {
                    size_t chunk = rem < sizeof(discard) ? rem : sizeof(discard);
                    if (exact_read(client->socket_fd, discard, chunk) < 0)
                        return NULL;
                    rem -= chunk;
                }
            }
        }
    }

    if (ack_hdr.payload_len < sizeof(ack))
        return NULL;

    if (exact_read(client->socket_fd, &ack, sizeof(ack)) < 0)
        return NULL;

    if (ack_hdr.payload_len > sizeof(ack)) {
        uint8_t discard[128];
        size_t rem = ack_hdr.payload_len - sizeof(ack);
        while (rem > 0) {
            size_t chunk = rem < sizeof(discard) ? rem : sizeof(discard);
            if (exact_read(client->socket_fd, discard, chunk) < 0)
                return NULL;
            rem -= chunk;
        }
    }

    if (ack.status != 0 || ack.shm_id <= 0)
        return NULL;

    win = (vanilla_window_t *)calloc(1, sizeof(vanilla_window_t));
    if (!win)
        return NULL;

    if (surface_attach_shm(&win->surface, ack.shm_id, (uint32_t)w, (uint32_t)h,
                           ack.pitch, ack.buffer_size) < 0) {
        vanilla_msg_hdr_t d_hdr;
        vanilla_msg_destroy_window_t d_req;
        d_hdr.magic = VANILLA_IPC_MAGIC;
        d_hdr.msg_type = MSG_DESTROY_WINDOW;
        d_hdr.payload_len = (uint16_t)sizeof(d_req);
        d_hdr.window_id = ack.window_id;
        d_req.window_id = ack.window_id;
        exact_write(client->socket_fd, &d_hdr, sizeof(d_hdr));
        exact_write(client->socket_fd, &d_req, sizeof(d_req));
        free(win);
        return NULL;
    }

    win->client = client;
    win->window_id = ack.window_id;
    win->x = x;
    win->y = y;
    win->width = (uint32_t)w;
    win->height = (uint32_t)h;
    win->flags = flags;
    win->last_frame_serial = 0;
    win->next = client->windows;
    client->windows = win;

    for (size_t i = 0; i < client->event_count; i++) {
        size_t idx = (client->event_head + i) % VANILLA_EVENT_QUEUE_CAP;
        if (client->event_queue[idx].window_id == win->window_id &&
            client->event_queue[idx].type == VANILLA_EVENT_FRAME_BEGIN) {
            win->last_frame_serial = client->event_queue[idx].frame_begin.frame_serial;
        }
    }

    return win;
}

void vanilla_destroy_window(vanilla_window_t *win)
{
    vanilla_msg_hdr_t hdr;
    vanilla_msg_destroy_window_t req;
    vanilla_client_t *client;
    vanilla_window_t **curr;

    if (!win || !win->client)
        return;

    client = win->client;

    hdr.magic = VANILLA_IPC_MAGIC;
    hdr.msg_type = MSG_DESTROY_WINDOW;
    hdr.payload_len = (uint16_t)sizeof(req);
    hdr.window_id = win->window_id;

    req.window_id = win->window_id;

    exact_write(client->socket_fd, &hdr, sizeof(hdr));
    exact_write(client->socket_fd, &req, sizeof(req));

    surface_detach(&win->surface);

    curr = &client->windows;
    while (*curr) {
        if (*curr == win) {
            *curr = win->next;
            break;
        }
        curr = &(*curr)->next;
    }

    free(win);
}

int vanilla_map_window(vanilla_window_t *win)
{
    vanilla_msg_hdr_t hdr;
    vanilla_msg_map_window_t req;

    if (!win || !win->client)
        return -1;

    hdr.magic = VANILLA_IPC_MAGIC;
    hdr.msg_type = MSG_MAP_WINDOW;
    hdr.payload_len = (uint16_t)sizeof(req);
    hdr.window_id = win->window_id;

    req.window_id = win->window_id;

    if (exact_write(win->client->socket_fd, &hdr, sizeof(hdr)) < 0 ||
        exact_write(win->client->socket_fd, &req, sizeof(req)) < 0)
        return -1;

    return 0;
}

int vanilla_unmap_window(vanilla_window_t *win)
{
    vanilla_msg_hdr_t hdr;
    vanilla_msg_map_window_t req;

    if (!win || !win->client)
        return -1;

    hdr.magic = VANILLA_IPC_MAGIC;
    hdr.msg_type = MSG_UNMAP_WINDOW;
    hdr.payload_len = (uint16_t)sizeof(req);
    hdr.window_id = win->window_id;

    req.window_id = win->window_id;

    if (exact_write(win->client->socket_fd, &hdr, sizeof(hdr)) < 0 ||
        exact_write(win->client->socket_fd, &req, sizeof(req)) < 0)
        return -1;

    return 0;
}

int vanilla_move_window(vanilla_window_t *win, int32_t x, int32_t y)
{
    vanilla_msg_hdr_t hdr;
    vanilla_msg_move_resize_t req;

    if (!win || !win->client)
        return -1;

    hdr.magic = VANILLA_IPC_MAGIC;
    hdr.msg_type = MSG_MOVE_RESIZE;
    hdr.payload_len = (uint16_t)sizeof(req);
    hdr.window_id = win->window_id;

    req.x = x;
    req.y = y;
    req.width = win->width;
    req.height = win->height;

    if (exact_write(win->client->socket_fd, &hdr, sizeof(hdr)) < 0 ||
        exact_write(win->client->socket_fd, &req, sizeof(req)) < 0)
        return -1;

    win->x = x;
    win->y = y;
    return 0;
}

void vanilla_present(vanilla_window_t *win, const vanilla_rect_t *damage)
{
    if (!win || !win->client)
        return;

    vanilla_msg_hdr_t hdr;
    hdr.magic = VANILLA_IPC_MAGIC;
    hdr.msg_type = MSG_PRESENT;
    hdr.window_id = win->window_id;

    if (win->client->server_version >= 2) {
        vanilla_msg_present_v2_t req2;
        hdr.payload_len = (uint16_t)sizeof(req2);
        if (damage) {
            req2.x = damage->x;
            req2.y = damage->y;
            req2.w = damage->w;
            req2.h = damage->h;
        } else {
            req2.x = 0;
            req2.y = 0;
            req2.w = (int32_t)win->width;
            req2.h = (int32_t)win->height;
        }
        req2.frame_serial = win->last_frame_serial;

        exact_write(win->client->socket_fd, &hdr, sizeof(hdr));
        exact_write(win->client->socket_fd, &req2, sizeof(req2));
    } else {
        vanilla_msg_present_t req;
        hdr.payload_len = (uint16_t)sizeof(req);
        if (damage) {
            req.x = damage->x;
            req.y = damage->y;
            req.w = damage->w;
            req.h = damage->h;
        } else {
            req.x = 0;
            req.y = 0;
            req.w = (int32_t)win->width;
            req.h = (int32_t)win->height;
        }

        exact_write(win->client->socket_fd, &hdr, sizeof(hdr));
        exact_write(win->client->socket_fd, &req, sizeof(req));
    }
}

int vanilla_poll_event(vanilla_client_t *client, vanilla_event_t *out_ev)
{
    struct pollfd pfd;
    vanilla_msg_hdr_t hdr;
    int ret;

    if (!client || !out_ev)
        return -1;

    for (;;) {
        if (event_queue_pop(client, out_ev)) {
            if (out_ev->type == VANILLA_EVENT_FRAME_BEGIN) {
                vanilla_window_t *target_win = find_window(client, out_ev->window_id);
                if (target_win) {
                    target_win->last_frame_serial = out_ev->frame_begin.frame_serial;
                }
                if (client->frame_cb) {
                    client->frame_cb(client, out_ev->window_id,
                                     out_ev->frame_begin.frame_serial,
                                     out_ev->frame_begin.timestamp_ms);
                }
            }
            return 1;
        }

        pfd.fd = client->socket_fd;
        pfd.events = POLLIN;
        pfd.revents = 0;

        ret = poll(&pfd, 1, 0);
        if (ret <= 0)
            return ret;

        if (!(pfd.revents & POLLIN))
            return 0;

        if (exact_read(client->socket_fd, &hdr, sizeof(hdr)) < 0)
            return -1;

        if (hdr.magic != VANILLA_IPC_MAGIC)
            return -1;

        out_ev->window_id = hdr.window_id;

        if (hdr.msg_type == MSG_INPUT_EVENT_V2) {
            vanilla_msg_input_event_v2_t in_msg;
            if (exact_read(client->socket_fd, &in_msg, sizeof(in_msg)) < 0)
                return -1;
            if (hdr.payload_len > sizeof(in_msg)) {
                uint8_t discard[128];
                size_t rem = hdr.payload_len - sizeof(in_msg);
                while (rem > 0) {
                    size_t chunk = rem < sizeof(discard) ? rem : sizeof(discard);
                    if (exact_read(client->socket_fd, discard, chunk) < 0)
                        return -1;
                    rem -= chunk;
                }
            }
            out_ev->mod_state = in_msg.mod_state;
            out_ev->input = in_msg.event;
            if (in_msg.event.type == EV_KEY &&
                (in_msg.event.code == BTN_LEFT || in_msg.event.code == BTN_RIGHT || in_msg.event.code == BTN_MIDDLE)) {
                if (in_msg.event.value == 2)
                    out_ev->type = VANILLA_EVENT_DOUBLE_CLICK;
                else if (in_msg.event.value == 3)
                    out_ev->type = VANILLA_EVENT_TRIPLE_CLICK;
                else if (in_msg.event.value == 4)
                    out_ev->type = VANILLA_EVENT_LONG_PRESS;
                else
                    out_ev->type = VANILLA_EVENT_INPUT;
            } else {
                out_ev->type = VANILLA_EVENT_INPUT;
            }
            return 1;
        }

        if (hdr.msg_type == MSG_INPUT_EVENT) {
            vanilla_msg_input_event_t in_msg;
            if (exact_read(client->socket_fd, &in_msg, sizeof(in_msg)) < 0)
                return -1;
            if (hdr.payload_len > sizeof(in_msg)) {
                uint8_t discard[128];
                size_t rem = hdr.payload_len - sizeof(in_msg);
                while (rem > 0) {
                    size_t chunk = rem < sizeof(discard) ? rem : sizeof(discard);
                    if (exact_read(client->socket_fd, discard, chunk) < 0)
                        return -1;
                    rem -= chunk;
                }
            }
            out_ev->mod_state = 0;
            out_ev->input = in_msg.event;
            if (in_msg.event.type == EV_KEY &&
                (in_msg.event.code == BTN_LEFT || in_msg.event.code == BTN_RIGHT || in_msg.event.code == BTN_MIDDLE)) {
                if (in_msg.event.value == 2)
                    out_ev->type = VANILLA_EVENT_DOUBLE_CLICK;
                else if (in_msg.event.value == 3)
                    out_ev->type = VANILLA_EVENT_TRIPLE_CLICK;
                else if (in_msg.event.value == 4)
                    out_ev->type = VANILLA_EVENT_LONG_PRESS;
                else
                    out_ev->type = VANILLA_EVENT_INPUT;
            } else {
                out_ev->type = VANILLA_EVENT_INPUT;
            }
            return 1;
        }

        if (hdr.msg_type == MSG_WINDOW_FOCUS) {
            vanilla_msg_window_focus_t f_msg;
            if (exact_read(client->socket_fd, &f_msg, sizeof(f_msg)) < 0)
                return -1;
            out_ev->type = VANILLA_EVENT_FOCUS;
            out_ev->focus.focused = (int)f_msg.focused;
            return 1;
        }

        if (hdr.msg_type == MSG_WINDOW_CONFIGURE) {
            out_ev->type = VANILLA_EVENT_CONFIGURE;
            uint32_t cfg_serial = 0;
            if (hdr.payload_len >= sizeof(vanilla_msg_window_configure_v2_t)) {
                vanilla_msg_window_configure_v2_t c_msg;
                if (exact_read(client->socket_fd, &c_msg, sizeof(c_msg)) < 0)
                    return -1;
                out_ev->configure.x = c_msg.x;
                out_ev->configure.y = c_msg.y;
                out_ev->configure.width = c_msg.width;
                out_ev->configure.height = c_msg.height;
                out_ev->configure.serial = c_msg.serial;
                out_ev->configure.flags = c_msg.flags;
                cfg_serial = c_msg.serial;
                if (hdr.payload_len > sizeof(c_msg)) {
                    uint8_t discard[128];
                    size_t rem = hdr.payload_len - sizeof(c_msg);
                    while (rem > 0) {
                        size_t chunk = rem < sizeof(discard) ? rem : sizeof(discard);
                        if (exact_read(client->socket_fd, discard, chunk) < 0)
                            return -1;
                        rem -= chunk;
                    }
                }
            } else {
                vanilla_msg_window_configure_t c_msg;
                if (exact_read(client->socket_fd, &c_msg, sizeof(c_msg)) < 0)
                    return -1;
                out_ev->configure.x = c_msg.x;
                out_ev->configure.y = c_msg.y;
                out_ev->configure.width = c_msg.width;
                out_ev->configure.height = c_msg.height;
                out_ev->configure.serial = 0;
                out_ev->configure.flags = 0;
                if (hdr.payload_len > sizeof(c_msg)) {
                    uint8_t discard[128];
                    size_t rem = hdr.payload_len - sizeof(c_msg);
                    while (rem > 0) {
                        size_t chunk = rem < sizeof(discard) ? rem : sizeof(discard);
                        if (exact_read(client->socket_fd, discard, chunk) < 0)
                            return -1;
                        rem -= chunk;
                    }
                }
            }
            vanilla_window_t *target_win = find_window(client, hdr.window_id);
            if (target_win) {
                target_win->x = out_ev->configure.x;
                target_win->y = out_ev->configure.y;
                target_win->width = out_ev->configure.width;
                target_win->height = out_ev->configure.height;
                if (out_ev->configure.flags != 0)
                    target_win->flags = out_ev->configure.flags;
            }
            if (cfg_serial != 0) {
                vanilla_ack_configure(client, hdr.window_id, cfg_serial);
            }
            return 1;
        }

        if (hdr.msg_type == MSG_FRAME_BEGIN) {
            vanilla_msg_frame_begin_t fb;
            if (exact_read(client->socket_fd, &fb, sizeof(fb)) < 0)
                return -1;
            if (hdr.payload_len > sizeof(fb)) {
                uint8_t discard[128];
                size_t rem = hdr.payload_len - sizeof(fb);
                while (rem > 0) {
                    size_t chunk = rem < sizeof(discard) ? rem : sizeof(discard);
                    if (exact_read(client->socket_fd, discard, chunk) < 0)
                        return -1;
                    rem -= chunk;
                }
            }
            vanilla_window_t *target_win = find_window(client, hdr.window_id ? hdr.window_id : fb.window_id);
            if (target_win) {
                target_win->last_frame_serial = fb.frame_serial;
            }
            if (client->frame_cb) {
                client->frame_cb(client, fb.window_id, fb.frame_serial, fb.timestamp_ms);
            }
            out_ev->type = VANILLA_EVENT_FRAME_BEGIN;
            out_ev->window_id = fb.window_id;
            out_ev->frame_begin.frame_serial = fb.frame_serial;
            out_ev->frame_begin.timestamp_ms = fb.timestamp_ms;
            return 1;
        }

        if (hdr.msg_type == MSG_BUFFER_RELEASED) {
            vanilla_msg_buffer_released_t br;
            if (exact_read(client->socket_fd, &br, sizeof(br)) < 0)
                return -1;
            if (hdr.payload_len > sizeof(br)) {
                uint8_t discard[128];
                size_t rem = hdr.payload_len - sizeof(br);
                while (rem > 0) {
                    size_t chunk = rem < sizeof(discard) ? rem : sizeof(discard);
                    if (exact_read(client->socket_fd, discard, chunk) < 0)
                        return -1;
                    rem -= chunk;
                }
            }
            out_ev->type = VANILLA_EVENT_BUFFER_RELEASED;
            out_ev->window_id = br.window_id;
            out_ev->buffer_released.serial = br.serial;
            return 1;
        }

        if (hdr.msg_type == MSG_WINDOW_CLOSE_REQ) {
            if (hdr.payload_len > 0) {
                uint8_t discard[128];
                size_t rem = hdr.payload_len;
                while (rem > 0) {
                    size_t chunk = rem < sizeof(discard) ? rem : sizeof(discard);
                    if (exact_read(client->socket_fd, discard, chunk) < 0)
                        return -1;
                    rem -= chunk;
                }
            }
            out_ev->type = VANILLA_EVENT_CLOSE_REQ;
            return 1;
        }

        if (hdr.payload_len > 0) {
            uint8_t discard[128];
            size_t rem = hdr.payload_len;
            while (rem > 0) {
                size_t chunk = rem < sizeof(discard) ? rem : sizeof(discard);
                if (exact_read(client->socket_fd, discard, chunk) < 0)
                    return -1;
                rem -= chunk;
            }
        }
    }
}

int vanilla_wait_event(vanilla_client_t *client, vanilla_event_t *out_ev)
{
    struct pollfd pfd;
    int ret;

    if (!client || !out_ev)
        return -1;

    ret = vanilla_poll_event(client, out_ev);
    if (ret != 0)
        return ret;

    pfd.fd = client->socket_fd;
    pfd.events = POLLIN;
    pfd.revents = 0;

    ret = poll(&pfd, 1, -1);
    if (ret <= 0)
        return -1;

    return vanilla_poll_event(client, out_ev);
}
