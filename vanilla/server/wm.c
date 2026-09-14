/*
 * Project Tsukasa — Vanilla Display Server Connection and Window Manager Engine
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

#include "server.h"
#include "blitter.h"
#include "font.h"
#include "shell.h"
#include "launcher.h"

#include <sys/socket.h>
#include <sys/un.h>
#include <sys/poll.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>

static const uint16_t cursor_arrow_mask[16] = {
    0b1000000000000000,
    0b1100000000000000,
    0b1110000000000000,
    0b1111000000000000,
    0b1111100000000000,
    0b1111110000000000,
    0b1111111000000000,
    0b1111111100000000,
    0b1111111110000000,
    0b1111111111000000,
    0b1111110000000000,
    0b1101111000000000,
    0b1000111100000000,
    0b0000011110000000,
    0b0000001110000000,
    0b0000000110000000,
};

static const uint16_t cursor_arrow_body[16] = {
    0b0000000000000000,
    0b0100000000000000,
    0b0110000000000000,
    0b0111000000000000,
    0b0111100000000000,
    0b0111110000000000,
    0b0111111000000000,
    0b0111111100000000,
    0b0111111000000000,
    0b0111100000000000,
    0b0110110000000000,
    0b0100011000000000,
    0b0000011100000000,
    0b0000001100000000,
    0b0000000100000000,
    0b0000000000000000,
};

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
                if (++retries > 100)
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
                if (++retries > 100)
                    return -1;
                sched_yield();
                continue;
            }
            return -1;
        }
    }
    return 0;
}

void wm_get_frame_rect(const vanilla_server_window_t *win, vanilla_rect_t *out_frame)
{
    if (!win || !out_frame)
        return;

    if (!(win->flags & WINDOW_FLAG_BORDERLESS)) {
        out_frame->x = win->x - WINDOW_BORDER_WIDTH;
        out_frame->y = win->y - TITLEBAR_HEIGHT - WINDOW_BORDER_WIDTH;
        out_frame->w = (int32_t)win->width + 2 * WINDOW_BORDER_WIDTH;
        out_frame->h = (int32_t)win->height + TITLEBAR_HEIGHT + 2 * WINDOW_BORDER_WIDTH;
    } else {
        out_frame->x = win->x;
        out_frame->y = win->y;
        out_frame->w = (int32_t)win->width;
        out_frame->h = (int32_t)win->height;
    }
}

void wm_invalidate_window(vanilla_server_t *srv, const vanilla_server_window_t *win)
{
    if (!srv || !win)
        return;

    vanilla_rect_t frame = { 0, 0, 0, 0 };
    wm_get_frame_rect(win, &frame);

    vanilla_rect_t dirty;
    dirty.x = frame.x - SHADOW_RADIUS;
    dirty.y = frame.y - SHADOW_RADIUS;
    dirty.w = frame.w + 2 * SHADOW_RADIUS;
    dirty.h = frame.h + 2 * SHADOW_RADIUS + SHADOW_RADIUS / 2;

    compositor_add_damage(&srv->compositor, &dirty);
}

void wm_raise_window(vanilla_server_t *srv, uint32_t window_id)
{
    if (!srv || window_id == 0)
        return;

    vanilla_server_window_t *target = vanilla_server_find_window(srv, window_id);
    if (!target)
        return;

    int32_t max_z = 0;
    for (int i = 0; i < VANILLA_MAX_WINDOWS; i++) {
        if (srv->windows[i].in_use && srv->windows[i].layer == target->layer) {
            if (srv->windows[i].z_index > max_z)
                max_z = srv->windows[i].z_index;
        }
    }

    target->z_index = max_z + 1;
    target->is_focused = 1;
    srv->focused_window_id = window_id;

    for (int i = 0; i < VANILLA_MAX_WINDOWS; i++) {
        if (srv->windows[i].in_use && srv->windows[i].window_id != window_id)
            srv->windows[i].is_focused = 0;
    }

    wm_invalidate_window(srv, target);
    shell_invalidate(srv);
}

void wm_lower_window(vanilla_server_t *srv, uint32_t window_id)
{
    if (!srv || window_id == 0)
        return;

    vanilla_server_window_t *target = vanilla_server_find_window(srv, window_id);
    if (!target)
        return;

    int32_t min_z = target->z_index;
    for (int i = 0; i < VANILLA_MAX_WINDOWS; i++) {
        if (srv->windows[i].in_use && srv->windows[i].layer == target->layer) {
            if (srv->windows[i].z_index < min_z)
                min_z = srv->windows[i].z_index;
        }
    }

    target->z_index = min_z - 1;
    wm_invalidate_window(srv, target);
    shell_invalidate(srv);
}

int vanilla_server_init(vanilla_server_t *srv, const char *socket_path)
{
    const char *path = socket_path ? socket_path : VANILLA_SOCKET_PATH;
    struct sockaddr_un addr;
    int fd;

    if (!srv)
        return -1;

    memset(srv, 0, sizeof(vanilla_server_t));
    strncpy(srv->socket_path, path, sizeof(srv->socket_path) - 1);

    unlink(srv->socket_path);

    fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (fd < 0)
        return -1;

    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    snprintf(addr.sun_path, sizeof(addr.sun_path), "%s", srv->socket_path);

    if (bind(fd, (const struct sockaddr *)&addr, sizeof(addr)) < 0) {
        close(fd);
        return -1;
    }

    if (listen(fd, 8) < 0) {
        close(fd);
        unlink(srv->socket_path);
        return -1;
    }

    srv->listen_fd = fd;
    srv->next_window_id = 1;
    srv->focused_window_id = 0;
    srv->next_z_index = 1;
    srv->running = 1;

    for (int i = 0; i < VANILLA_MAX_CLIENTS; i++) {
        srv->clients[i].in_use = 0;
        srv->clients[i].fd = -1;
    }

    for (int i = 0; i < VANILLA_MAX_WINDOWS; i++)
        srv->windows[i].in_use = 0;

    compositor_init(&srv->compositor, "/dev/fb0");

    srv->cursor_x = (int32_t)srv->compositor.width / 2;
    srv->cursor_y = (int32_t)srv->compositor.height / 2;
    srv->mouse_buttons = 0;
    srv->shift_pressed = 0;
    srv->alt_pressed = 0;
    srv->ctrl_pressed = 0;
    srv->is_dragging = 0;
    srv->drag_window_id = 0;

    shell_init(&srv->shell);
    launcher_init(&srv->launcher);

    /* Open kernel input stream with non-blocking fallback */
    srv->input_fd = open("/dev/input/events", O_RDONLY | O_NONBLOCK);

    compositor_damage_all(&srv->compositor);
    compositor_render_frame(srv);

    return 0;
}

void vanilla_server_close(vanilla_server_t *srv)
{
    if (!srv)
        return;

    if (srv->input_fd >= 0) {
        close(srv->input_fd);
        srv->input_fd = -1;
    }

    for (int i = 0; i < VANILLA_MAX_CLIENTS; i++) {
        if (srv->clients[i].in_use)
            vanilla_server_remove_client(srv, i);
    }

    if (srv->listen_fd >= 0) {
        close(srv->listen_fd);
        srv->listen_fd = -1;
    }

    unlink(srv->socket_path);
    compositor_destroy(&srv->compositor);
    srv->running = 0;
}

int vanilla_server_accept(vanilla_server_t *srv)
{
    int cfd;
    int slot = -1;

    if (!srv || srv->listen_fd < 0)
        return -1;

    for (int i = 0; i < VANILLA_MAX_CLIENTS; i++) {
        if (!srv->clients[i].in_use) {
            slot = i;
            break;
        }
    }
    if (slot < 0)
        return -1;

    cfd = accept(srv->listen_fd, NULL, NULL);
    if (cfd < 0)
        return -1;

    srv->clients[slot].in_use = 1;
    srv->clients[slot].fd = cfd;
    printf("[vanilla] Accepted client connection (slot=%d, fd=%d)\n", slot, cfd);

    return slot;
}

void vanilla_server_remove_client(vanilla_server_t *srv, int client_idx)
{
    int cfd;

    if (!srv || client_idx < 0 || client_idx >= VANILLA_MAX_CLIENTS)
        return;

    if (!srv->clients[client_idx].in_use)
        return;

    cfd = srv->clients[client_idx].fd;

    for (int i = 0; i < VANILLA_MAX_WINDOWS; i++) {
        if (srv->windows[i].in_use && srv->windows[i].client_fd == cfd) {
            if (srv->focused_window_id == srv->windows[i].window_id)
                srv->focused_window_id = 0;
            if (srv->is_dragging && srv->drag_window_id == srv->windows[i].window_id) {
                srv->is_dragging = 0;
                srv->drag_window_id = 0;
            }
            wm_invalidate_window(srv, &srv->windows[i]);
            surface_destroy(&srv->windows[i].surface);
            srv->windows[i].in_use = 0;
        }
    }

    if (cfd >= 0)
        close(cfd);

    srv->clients[client_idx].in_use = 0;
    srv->clients[client_idx].fd = -1;

    shell_invalidate(srv);
}

vanilla_server_window_t *vanilla_server_find_window(vanilla_server_t *srv, uint32_t window_id)
{
    if (!srv || window_id == 0)
        return NULL;

    for (int i = 0; i < VANILLA_MAX_WINDOWS; i++) {
        if (srv->windows[i].in_use && srv->windows[i].window_id == window_id)
            return &srv->windows[i];
    }
    return NULL;
}

static void wm_send_configure(vanilla_server_window_t *w)
{
    if (!w || w->client_fd < 0)
        return;
    vanilla_msg_hdr_t hdr;
    vanilla_msg_window_configure_t cfg;
    hdr.magic = VANILLA_IPC_MAGIC;
    hdr.msg_type = MSG_WINDOW_CONFIGURE;
    hdr.payload_len = (uint16_t)sizeof(cfg);
    hdr.window_id = w->window_id;
    cfg.x = w->x;
    cfg.y = w->y;
    cfg.width = w->width;
    cfg.height = w->height;
    exact_write(w->client_fd, &hdr, sizeof(hdr));
    exact_write(w->client_fd, &cfg, sizeof(cfg));
}

int vanilla_server_dispatch_client(vanilla_server_t *srv, int client_idx)
{
    vanilla_msg_hdr_t hdr;
    int cfd;

    if (!srv || client_idx < 0 || client_idx >= VANILLA_MAX_CLIENTS)
        return -1;

    if (!srv->clients[client_idx].in_use)
        return -1;

    cfd = srv->clients[client_idx].fd;

    if (exact_read(cfd, &hdr, sizeof(hdr)) < 0) {
        vanilla_server_remove_client(srv, client_idx);
        return -1;
    }

    if (hdr.magic != VANILLA_IPC_MAGIC) {
        vanilla_server_remove_client(srv, client_idx);
        return -1;
    }

    switch (hdr.msg_type) {
    case MSG_HELLO: {
        vanilla_msg_hello_t hello;
        vanilla_msg_hdr_t ack_hdr;
        vanilla_msg_hello_ack_t ack;

        if (hdr.payload_len != sizeof(hello)) {
            vanilla_server_remove_client(srv, client_idx);
            return -1;
        }

        if (exact_read(cfd, &hello, sizeof(hello)) < 0) {
            vanilla_server_remove_client(srv, client_idx);
            return -1;
        }

        ack_hdr.magic = VANILLA_IPC_MAGIC;
        ack_hdr.msg_type = MSG_HELLO_ACK;
        ack_hdr.payload_len = (uint16_t)sizeof(ack);
        ack_hdr.window_id = 0;

        ack.server_version = VANILLA_IPC_VERSION;
        ack.status = 0;

        exact_write(cfd, &ack_hdr, sizeof(ack_hdr));
        exact_write(cfd, &ack, sizeof(ack));
        return 0;
    }

    case MSG_CREATE_WINDOW: {
        vanilla_msg_create_window_t req;
        vanilla_msg_hdr_t ack_hdr;
        vanilla_msg_create_window_ack_t ack;
        int win_slot = -1;

        if (hdr.payload_len != sizeof(req)) {
            vanilla_server_remove_client(srv, client_idx);
            return -1;
        }

        if (exact_read(cfd, &req, sizeof(req)) < 0) {
            vanilla_server_remove_client(srv, client_idx);
            return -1;
        }

        ack_hdr.magic = VANILLA_IPC_MAGIC;
        ack_hdr.msg_type = MSG_CREATE_WINDOW_ACK;
        ack_hdr.payload_len = (uint16_t)sizeof(ack);
        ack_hdr.window_id = 0;

        memset(&ack, 0, sizeof(ack));

        if (req.width == 0 || req.height == 0 ||
            req.width > VANILLA_MAX_WIDTH || req.height > VANILLA_MAX_HEIGHT) {
            ack.status = -22;
            exact_write(cfd, &ack_hdr, sizeof(ack_hdr));
            exact_write(cfd, &ack, sizeof(ack));
            return 0;
        }

        for (int i = 0; i < VANILLA_MAX_WINDOWS; i++) {
            if (!srv->windows[i].in_use) {
                win_slot = i;
                break;
            }
        }

        if (win_slot < 0) {
            ack.status = -28;
            exact_write(cfd, &ack_hdr, sizeof(ack_hdr));
            exact_write(cfd, &ack, sizeof(ack));
            return 0;
        }

        vanilla_server_window_t *w = &srv->windows[win_slot];
        if (surface_create_shm(&w->surface, req.width, req.height) < 0) {
            ack.status = -12;
            exact_write(cfd, &ack_hdr, sizeof(ack_hdr));
            exact_write(cfd, &ack, sizeof(ack));
            return 0;
        }

        w->in_use = 1;
        w->window_id = srv->next_window_id++;
        w->client_fd = cfd;
        w->shm_id = w->surface.shm_id;
        w->x = req.x;
        w->y = req.y;
        w->width = req.width;
        w->height = req.height;
        w->flags = req.flags;
        w->is_mapped = 1;
        w->is_focused = 1;
        w->z_index = ++srv->next_z_index;

        w->is_snapped = SNAP_NONE;
        w->restore_x = req.x;
        w->restore_y = req.y;
        w->restore_w = req.width;
        w->restore_h = req.height;

        if (req.flags & WINDOW_FLAG_MODAL)
            w->layer = LAYER_TOPMOST;
        else if (req.flags & WINDOW_FLAG_ALWAYS_TOP)
            w->layer = LAYER_TOPMOST;
        else
            w->layer = LAYER_NORMAL;

        snprintf(w->title, sizeof(w->title), "%s", req.title);
        w->damage.x = 0;
        w->damage.y = 0;
        w->damage.w = (int32_t)req.width;
        w->damage.h = (int32_t)req.height;

        ack_hdr.window_id = w->window_id;
        ack.window_id = w->window_id;
        ack.shm_id = w->shm_id;
        ack.buffer_size = (uint32_t)w->surface.size;
        ack.pitch = w->surface.pitch;
        ack.status = 0;

        exact_write(cfd, &ack_hdr, sizeof(ack_hdr));
        exact_write(cfd, &ack, sizeof(ack));

        wm_raise_window(srv, w->window_id);
        wm_invalidate_window(srv, w);
        shell_invalidate(srv);
        printf("[vanilla] Created window id=%u title='%s' (%ux%u)\n", w->window_id, w->title, req.width, req.height);
        return 0;
    }

    case MSG_DESTROY_WINDOW: {
        vanilla_msg_destroy_window_t req;
        vanilla_server_window_t *w;

        if (hdr.payload_len != sizeof(req)) {
            vanilla_server_remove_client(srv, client_idx);
            return -1;
        }

        if (exact_read(cfd, &req, sizeof(req)) < 0) {
            vanilla_server_remove_client(srv, client_idx);
            return -1;
        }

        w = vanilla_server_find_window(srv, req.window_id);
        if (w && w->client_fd == cfd) {
            if (srv->focused_window_id == w->window_id)
                srv->focused_window_id = 0;
            if (srv->is_dragging && srv->drag_window_id == w->window_id) {
                srv->is_dragging = 0;
                srv->drag_window_id = 0;
            }
            wm_invalidate_window(srv, w);
            surface_destroy(&w->surface);
            w->in_use = 0;
            shell_invalidate(srv);
        }
        return 0;
    }

    case MSG_MAP_WINDOW: {
        vanilla_msg_map_window_t req;
        vanilla_server_window_t *w;

        if (hdr.payload_len != sizeof(req)) {
            vanilla_server_remove_client(srv, client_idx);
            return -1;
        }

        if (exact_read(cfd, &req, sizeof(req)) < 0) {
            vanilla_server_remove_client(srv, client_idx);
            return -1;
        }

        w = vanilla_server_find_window(srv, req.window_id);
        if (w && w->client_fd == cfd) {
            w->is_mapped = 1;
            wm_raise_window(srv, w->window_id);
            /* Automatically focus window upon map so keyboard input is directed immediately */
            vanilla_server_focus_window(srv, w->window_id);
            wm_invalidate_window(srv, w);
            shell_invalidate(srv);
        }
        return 0;
    }

    case MSG_UNMAP_WINDOW: {
        vanilla_msg_map_window_t req;
        vanilla_server_window_t *w;

        if (hdr.payload_len != sizeof(req)) {
            vanilla_server_remove_client(srv, client_idx);
            return -1;
        }

        if (exact_read(cfd, &req, sizeof(req)) < 0) {
            vanilla_server_remove_client(srv, client_idx);
            return -1;
        }

        w = vanilla_server_find_window(srv, req.window_id);
        if (w && w->client_fd == cfd) {
            wm_invalidate_window(srv, w);
            w->is_mapped = 0;
            if (srv->focused_window_id == w->window_id)
                srv->focused_window_id = 0;
            shell_invalidate(srv);
        }
        return 0;
    }

    case MSG_MOVE_RESIZE: {
        vanilla_msg_move_resize_t req;
        vanilla_server_window_t *w;

        if (hdr.payload_len != sizeof(req)) {
            vanilla_server_remove_client(srv, client_idx);
            return -1;
        }

        if (exact_read(cfd, &req, sizeof(req)) < 0) {
            vanilla_server_remove_client(srv, client_idx);
            return -1;
        }

        w = vanilla_server_find_window(srv, hdr.window_id);
        if (w && w->client_fd == cfd) {
            wm_invalidate_window(srv, w);
            w->x = req.x;
            w->y = req.y;
            if (req.width > 0 && req.height > 0 &&
                (req.width != w->width || req.height != w->height)) {
                w->width = req.width;
                w->height = req.height;
                wm_send_configure(w);
            }
            wm_invalidate_window(srv, w);
        }
        return 0;
    }

    case MSG_PRESENT: {
        vanilla_msg_present_t req;
        vanilla_server_window_t *w;

        if (hdr.payload_len != sizeof(req)) {
            vanilla_server_remove_client(srv, client_idx);
            return -1;
        }

        if (exact_read(cfd, &req, sizeof(req)) < 0) {
            vanilla_server_remove_client(srv, client_idx);
            return -1;
        }

        if (req.w <= 0 || req.h <= 0)
            return 0;

        w = vanilla_server_find_window(srv, hdr.window_id);
        if (w && w->client_fd == cfd) {
            if (w->damage.w == 0 || w->damage.h == 0) {
                w->damage.x = req.x;
                w->damage.y = req.y;
                w->damage.w = req.w;
                w->damage.h = req.h;
            } else {
                int32_t x1 = w->damage.x < req.x ? w->damage.x : req.x;
                int32_t y1 = w->damage.y < req.y ? w->damage.y : req.y;
                int32_t x2 = (w->damage.x + w->damage.w) > (req.x + req.w)
                             ? (w->damage.x + w->damage.w)
                             : (req.x + req.w);
                int32_t y2 = (w->damage.y + w->damage.h) > (req.y + req.h)
                             ? (w->damage.y + w->damage.h)
                             : (req.y + req.h);

                w->damage.x = x1;
                w->damage.y = y1;
                w->damage.w = x2 - x1;
                w->damage.h = y2 - y1;
            }

            vanilla_rect_t screen_damage;
            screen_damage.x = w->x + req.x;
            screen_damage.y = w->y + req.y;
            screen_damage.w = req.w;
            screen_damage.h = req.h;
            compositor_add_damage(&srv->compositor, &screen_damage);
        }
        return 0;
    }

    default: {
        if (hdr.payload_len > 0) {
            uint8_t discard[128];
            size_t rem = hdr.payload_len;
            while (rem > 0) {
                size_t to_read = rem < sizeof(discard) ? rem : sizeof(discard);
                if (exact_read(cfd, discard, to_read) < 0) {
                    vanilla_server_remove_client(srv, client_idx);
                    return -1;
                }
                rem -= to_read;
            }
        }
        return 0;
    }
    }
}

int vanilla_server_poll(vanilla_server_t *srv, int timeout_ms)
{
    struct pollfd fds[2 + VANILLA_MAX_CLIENTS];
    int nfds = 0;
    int client_map[VANILLA_MAX_CLIENTS];
    int input_poll_idx = -1;
    int ret;

    if (!srv || srv->listen_fd < 0)
        return -1;

    shell_update_clock(&srv->shell, srv);

    fds[0].fd = srv->listen_fd;
    fds[0].events = POLLIN;
    fds[0].revents = 0;
    nfds = 1;

    if (srv->input_fd >= 0) {
        input_poll_idx = nfds;
        fds[nfds].fd = srv->input_fd;
        fds[nfds].events = POLLIN;
        fds[nfds].revents = 0;
        nfds++;
    }

    for (int i = 0; i < VANILLA_MAX_CLIENTS; i++) {
        if (srv->clients[i].in_use && srv->clients[i].fd >= 0) {
            fds[nfds].fd = srv->clients[i].fd;
            fds[nfds].events = POLLIN;
            fds[nfds].revents = 0;
            client_map[nfds] = i;
            nfds++;
        }
    }

    ret = poll(fds, (nfds_t)nfds, timeout_ms);
    if (ret <= 0) {
        if (srv->compositor.dirty_count > 0)
            compositor_render_frame(srv);
        return ret;
    }

    if (fds[0].revents & POLLIN)
        vanilla_server_accept(srv);

    if (input_poll_idx >= 0 && (fds[input_poll_idx].revents & POLLIN)) {
        struct input_event ev;
        while (read(srv->input_fd, &ev, sizeof(ev)) == (ssize_t)sizeof(ev)) {
            wm_handle_input_event(srv, &ev);
        }
    }

    int start_client_idx = (input_poll_idx >= 0) ? 2 : 1;
    for (int i = start_client_idx; i < nfds; i++) {
        if (fds[i].revents & (POLLIN | POLLHUP | POLLERR)) {
            int c_idx = client_map[i];
            vanilla_server_dispatch_client(srv, c_idx);
        }
    }

    if (srv->compositor.dirty_count > 0)
        compositor_render_frame(srv);

    return 0;
}

int vanilla_server_send_input(vanilla_server_t *srv, uint32_t window_id, const struct input_event *ev)
{
    vanilla_server_window_t *w;
    vanilla_msg_hdr_t hdr;
    vanilla_msg_input_event_t msg;

    if (!srv || !ev)
        return -1;

    w = vanilla_server_find_window(srv, window_id);
    if (!w || !w->in_use || w->client_fd < 0)
        return -1;

    hdr.magic = VANILLA_IPC_MAGIC;
    hdr.msg_type = MSG_INPUT_EVENT;
    hdr.payload_len = (uint16_t)sizeof(msg);
    hdr.window_id = window_id;

    msg.event = *ev;

    if (exact_write(w->client_fd, &hdr, sizeof(hdr)) < 0 ||
        exact_write(w->client_fd, &msg, sizeof(msg)) < 0)
        return -1;

    return 0;
}

int vanilla_server_focus_window(vanilla_server_t *srv, uint32_t window_id)
{
    vanilla_server_window_t *old_w;
    vanilla_server_window_t *new_w;
    vanilla_msg_hdr_t hdr;
    vanilla_msg_window_focus_t msg;

    if (!srv)
        return -1;

    if (srv->focused_window_id == window_id)
        return 0;

    old_w = vanilla_server_find_window(srv, srv->focused_window_id);
    if (old_w && old_w->in_use) {
        old_w->is_focused = 0;
        if (old_w->client_fd >= 0) {
            hdr.magic = VANILLA_IPC_MAGIC;
            hdr.msg_type = MSG_WINDOW_FOCUS;
            hdr.payload_len = (uint16_t)sizeof(msg);
            hdr.window_id = old_w->window_id;
            msg.focused = 0;
            exact_write(old_w->client_fd, &hdr, sizeof(hdr));
            exact_write(old_w->client_fd, &msg, sizeof(msg));
        }
        wm_invalidate_window(srv, old_w);
    }

    new_w = vanilla_server_find_window(srv, window_id);
    if (new_w && new_w->in_use) {
        new_w->is_focused = 1;
        srv->focused_window_id = window_id;
        if (new_w->client_fd >= 0) {
            hdr.magic = VANILLA_IPC_MAGIC;
            hdr.msg_type = MSG_WINDOW_FOCUS;
            hdr.payload_len = (uint16_t)sizeof(msg);
            hdr.window_id = new_w->window_id;
            msg.focused = 1;
            exact_write(new_w->client_fd, &hdr, sizeof(hdr));
            exact_write(new_w->client_fd, &msg, sizeof(msg));
        }
        wm_raise_window(srv, window_id);
    } else {
        srv->focused_window_id = 0;
    }

    shell_invalidate(srv);
    return 0;
}

int vanilla_server_close_request(vanilla_server_t *srv, uint32_t window_id)
{
    vanilla_server_window_t *w;
    vanilla_msg_hdr_t hdr;
    vanilla_msg_window_close_req_t msg;

    if (!srv)
        return -1;

    w = vanilla_server_find_window(srv, window_id);
    if (!w || !w->in_use)
        return -1;

    if (w->client_fd >= 0) {
        hdr.magic = VANILLA_IPC_MAGIC;
        hdr.msg_type = MSG_WINDOW_CLOSE_REQ;
        hdr.payload_len = (uint16_t)sizeof(msg);
        hdr.window_id = window_id;
        msg.reserved = 0;

        if (exact_write(w->client_fd, &hdr, sizeof(hdr)) < 0 ||
            exact_write(w->client_fd, &msg, sizeof(msg)) < 0)
            return -1;
    } else {
        w->is_mapped = 0;
        w->is_focused = 0;
        if (srv->focused_window_id == window_id)
            srv->focused_window_id = 0;
        wm_invalidate_window(srv, w);
        shell_invalidate(srv);
    }

    return 0;
}

void wm_snap_window(vanilla_server_t *srv, uint32_t window_id, int snap_type)
{
    vanilla_server_window_t *w = vanilla_server_find_window(srv, window_id);
    if (!srv || !w || !w->in_use)
        return;

    int32_t screen_w = (int32_t)srv->compositor.width;
    int32_t screen_h = (int32_t)srv->compositor.height;
    int32_t work_h = screen_h - TASKBAR_HEIGHT;
    if (work_h < 100)
        work_h = screen_h;

    wm_invalidate_window(srv, w);

    if (snap_type == SNAP_NONE) {
        if (w->is_snapped != SNAP_NONE) {
            w->x = w->restore_x;
            w->y = w->restore_y;
            w->width = w->restore_w;
            w->height = w->restore_h;
            w->is_snapped = SNAP_NONE;
        }
    } else {
        if (w->is_snapped == SNAP_NONE) {
            w->restore_x = w->x;
            w->restore_y = w->y;
            w->restore_w = w->width;
            w->restore_h = w->height;
        }

        int32_t target_fx = 0, target_fy = 0, target_fw = screen_w, target_fh = work_h;
        if (snap_type == SNAP_MAXIMIZE) {
            target_fx = 0;
            target_fy = 0;
            target_fw = screen_w;
            target_fh = work_h;
        } else if (snap_type == SNAP_LEFT) {
            target_fx = 0;
            target_fy = 0;
            target_fw = screen_w / 2;
            target_fh = work_h;
        } else if (snap_type == SNAP_RIGHT) {
            target_fx = screen_w / 2;
            target_fy = 0;
            target_fw = screen_w - (screen_w / 2);
            target_fh = work_h;
        }

        if (!(w->flags & WINDOW_FLAG_BORDERLESS)) {
            w->x = target_fx + WINDOW_BORDER_WIDTH;
            w->y = target_fy + TITLEBAR_HEIGHT + WINDOW_BORDER_WIDTH;
            w->width = (uint32_t)(target_fw - 2 * WINDOW_BORDER_WIDTH);
            w->height = (uint32_t)(target_fh - TITLEBAR_HEIGHT - 2 * WINDOW_BORDER_WIDTH);
        } else {
            w->x = target_fx;
            w->y = target_fy;
            w->width = (uint32_t)target_fw;
            w->height = (uint32_t)target_fh;
        }

        w->is_snapped = snap_type;
    }

    wm_send_configure(w);
    wm_invalidate_window(srv, w);
}

void wm_unsnap_window(vanilla_server_t *srv, uint32_t window_id)
{
    wm_snap_window(srv, window_id, SNAP_NONE);
}

int wm_handle_input_event(vanilla_server_t *srv, const struct input_event *ev)
{
    if (!srv || !ev)
        return -1;

    int32_t screen_w = (int32_t)srv->compositor.width;
    int32_t screen_h = (int32_t)srv->compositor.height;

    if (ev->type == EV_REL) {
        int32_t old_x = srv->cursor_x;
        int32_t old_y = srv->cursor_y;

        if (ev->code == REL_X)
            srv->cursor_x += ev->value;
        else if (ev->code == REL_Y)
            srv->cursor_y += ev->value;

        if (srv->cursor_x < 0)
            srv->cursor_x = 0;
        if (srv->cursor_x >= screen_w)
            srv->cursor_x = screen_w - 1;
        if (srv->cursor_y < 0)
            srv->cursor_y = 0;
        if (srv->cursor_y >= screen_h)
            srv->cursor_y = screen_h - 1;

        if (srv->cursor_x != old_x || srv->cursor_y != old_y) {
            vanilla_rect_t old_box = { old_x, old_y, 16, 16 };
            vanilla_rect_t new_box = { srv->cursor_x, srv->cursor_y, 16, 16 };
            compositor_add_damage(&srv->compositor, &old_box);
            compositor_add_damage(&srv->compositor, &new_box);

            if (srv->is_dragging) {
                vanilla_server_window_t *w = vanilla_server_find_window(srv, srv->drag_window_id);
                if (w) {
                    wm_invalidate_window(srv, w);
                    w->x = srv->cursor_x - srv->drag_offset_x;
                    w->y = srv->cursor_y - srv->drag_offset_y;
                    wm_invalidate_window(srv, w);
                }
            }
        }
        return 0;
    }

    if (ev->type == EV_KEY) {
        /* Update modifier keys */
        if (ev->code == KEY_LEFTSHIFT || ev->code == KEY_RIGHTSHIFT) {
            srv->shift_pressed = (ev->value != 0);
            return 0;
        }
        if (ev->code == KEY_LEFTALT) {
            srv->alt_pressed = (ev->value != 0);
            return 0;
        }
        if (ev->code == KEY_LEFTCTRL) {
            srv->ctrl_pressed = (ev->value != 0);
            return 0;
        }

        /* Mouse buttons */
        if (ev->code == BTN_LEFT) {
            if (ev->value == 1) {
                srv->mouse_buttons |= (1u << 0);

                /* 1. If Quick Launcher is visible, dispatch to it */
                if (srv->launcher.visible) {
                    launcher_handle_click(srv, srv->cursor_x, srv->cursor_y, BTN_LEFT);
                    return 0;
                }

                /* 2. Start Menu dispatch and click-through prevention */
                if (srv->shell.start_menu_open) {
                    int32_t sm_y = screen_h - TASKBAR_HEIGHT - START_MENU_HEIGHT;
                    if (srv->cursor_x >= 0 && srv->cursor_x < START_MENU_WIDTH &&
                        srv->cursor_y >= sm_y && srv->cursor_y < sm_y + START_MENU_HEIGHT) {
                        shell_handle_click(srv, srv->cursor_x, srv->cursor_y, BTN_LEFT);
                        return 0;
                    }
                    if (srv->cursor_x >= TASKBAR_START_X && srv->cursor_x < TASKBAR_START_X + TASKBAR_START_W &&
                        srv->cursor_y >= screen_h - TASKBAR_HEIGHT + 4 && srv->cursor_y < screen_h - TASKBAR_HEIGHT + 4 + TASKBAR_START_H) {
                        shell_handle_click(srv, srv->cursor_x, srv->cursor_y, BTN_LEFT);
                        return 0;
                    }
                    srv->shell.start_menu_open = 0;
                    shell_invalidate_start_menu(srv);
                    shell_invalidate(srv);
                }

                /* 3. If Taskbar is clicked, dispatch to shell */
                if (srv->cursor_y >= screen_h - TASKBAR_HEIGHT) {
                    shell_handle_click(srv, srv->cursor_x, srv->cursor_y, BTN_LEFT);
                    return 0;
                }

                /* 4. Window interaction: hit test top-to-bottom */
                vanilla_server_window_t *hit = NULL;
                int32_t max_z = -1;

                for (int i = 0; i < VANILLA_MAX_WINDOWS; i++) {
                    vanilla_server_window_t *w = &srv->windows[i];
                    if (!w->in_use || !w->is_mapped)
                        continue;

                    vanilla_rect_t frame;
                    wm_get_frame_rect(w, &frame);

                    if (srv->cursor_x >= frame.x && srv->cursor_x < frame.x + frame.w &&
                        srv->cursor_y >= frame.y && srv->cursor_y < frame.y + frame.h) {
                        if (w->z_index > max_z) {
                            max_z = w->z_index;
                            hit = w;
                        }
                    }
                }

                if (hit) {
                    vanilla_server_focus_window(srv, hit->window_id);
                    wm_raise_window(srv, hit->window_id);

                    vanilla_rect_t frame;
                    wm_get_frame_rect(hit, &frame);

                    /* Check titlebar click */
                    if (!(hit->flags & WINDOW_FLAG_BORDERLESS) &&
                        srv->cursor_y < frame.y + TITLEBAR_HEIGHT + WINDOW_BORDER_WIDTH) {

                        /* Close button [X] */
                        if (srv->cursor_x >= frame.x + frame.w - 18 &&
                            srv->cursor_x < frame.x + frame.w - 6 &&
                            srv->cursor_y >= frame.y + 6 &&
                            srv->cursor_y < frame.y + 18) {
                            vanilla_server_close_request(srv, hit->window_id);
                            return 0;
                        }

                        /* Maximize button [] */
                        if (srv->cursor_x >= frame.x + frame.w - 34 &&
                            srv->cursor_x < frame.x + frame.w - 22 &&
                            srv->cursor_y >= frame.y + 6 &&
                            srv->cursor_y < frame.y + 18) {
                            if (hit->is_snapped == SNAP_MAXIMIZE)
                                wm_unsnap_window(srv, hit->window_id);
                            else
                                wm_snap_window(srv, hit->window_id, SNAP_MAXIMIZE);
                            return 0;
                        }

                        /* Minimize button [_] */
                        if (srv->cursor_x >= frame.x + frame.w - 50 &&
                            srv->cursor_x < frame.x + frame.w - 38 &&
                            srv->cursor_y >= frame.y + 6 &&
                            srv->cursor_y < frame.y + 18) {
                            hit->is_mapped = 0;
                            hit->is_focused = 0;
                            if (srv->focused_window_id == hit->window_id)
                                srv->focused_window_id = 0;
                            wm_invalidate_window(srv, hit);
                            shell_invalidate(srv);
                            return 0;
                        }

                        /* Click on titlebar body: initiate window drag */
                        if (hit->is_snapped != SNAP_NONE) {
                            wm_unsnap_window(srv, hit->window_id);
                            hit->x = srv->cursor_x - (int32_t)hit->width / 2;
                            hit->y = srv->cursor_y - 12;
                        }

                        srv->is_dragging = 1;
                        srv->drag_window_id = hit->window_id;
                        srv->drag_offset_x = srv->cursor_x - hit->x;
                        srv->drag_offset_y = srv->cursor_y - hit->y;
                        return 0;
                    }

                    /* Click within client surface area */
                    if (srv->cursor_x >= hit->x && srv->cursor_x < hit->x + (int32_t)hit->width &&
                        srv->cursor_y >= hit->y && srv->cursor_y < hit->y + (int32_t)hit->height) {
                        int lx = srv->cursor_x - hit->x;
                        int ly = srv->cursor_y - hit->y;
                        struct input_event client_ev = *ev;
                        client_ev.pad1 = (uint16_t)(lx < 0 ? 0 : lx);
                        client_ev.pad2 = (uint32_t)(ly < 0 ? 0 : ly);
                        vanilla_server_send_input(srv, hit->window_id, &client_ev);
                        return 0;
                    }
                } else {
                    /* Clicked empty desktop: unfocus windows */
                    if (srv->focused_window_id != 0) {
                        vanilla_server_focus_window(srv, 0);
                        shell_invalidate(srv);
                    }
                }
                return 0;
            } else {
                srv->mouse_buttons &= ~(1u << 0);

                if (srv->is_dragging) {
                    /* Evaluate Aero-snap boundary triggers upon release */
                    if (srv->cursor_y <= 2) {
                        wm_snap_window(srv, srv->drag_window_id, SNAP_MAXIMIZE);
                    } else if (srv->cursor_x <= 2) {
                        wm_snap_window(srv, srv->drag_window_id, SNAP_LEFT);
                    } else if (srv->cursor_x >= screen_w - 3) {
                        wm_snap_window(srv, srv->drag_window_id, SNAP_RIGHT);
                    }
                    srv->is_dragging = 0;
                    srv->drag_window_id = 0;
                    return 0;
                }

                if (srv->focused_window_id != 0) {
                    vanilla_server_window_t *w = vanilla_server_find_window(srv, srv->focused_window_id);
                    if (w) {
                        int lx = srv->cursor_x - w->x;
                        int ly = srv->cursor_y - w->y;
                        struct input_event client_ev = *ev;
                        client_ev.pad1 = (uint16_t)(lx < 0 ? 0 : lx);
                        client_ev.pad2 = (uint32_t)(ly < 0 ? 0 : ly);
                        vanilla_server_send_input(srv, srv->focused_window_id, &client_ev);
                    }
                }
                return 0;
            }
        }

        if (ev->code == BTN_RIGHT) {
            if (ev->value == 1)
                srv->mouse_buttons |= (1u << 1);
            else
                srv->mouse_buttons &= ~(1u << 1);

            if (srv->focused_window_id != 0) {
                vanilla_server_window_t *w = vanilla_server_find_window(srv, srv->focused_window_id);
                if (w) {
                    int lx = srv->cursor_x - w->x;
                    int ly = srv->cursor_y - w->y;
                    struct input_event client_ev = *ev;
                    client_ev.pad1 = (uint16_t)(lx < 0 ? 0 : lx);
                    client_ev.pad2 = (uint32_t)(ly < 0 ? 0 : ly);
                    vanilla_server_send_input(srv, srv->focused_window_id, &client_ev);
                }
            }
            return 0;
        }

        /* Hotkey: Alt+Space toggles Quick Launcher */
        if (srv->alt_pressed && ev->code == KEY_SPACE && ev->value == 1) {
            launcher_toggle(srv);
            return 0;
        }

        /* Forward to launcher if active */
        if (srv->launcher.visible) {
            launcher_handle_key(srv, ev->code, ev->value);
            return 0;
        }

        /* ESC closes Start Menu if open */
        if (ev->code == KEY_ESC && ev->value == 1 && srv->shell.start_menu_open) {
            srv->shell.start_menu_open = 0;
            shell_invalidate_start_menu(srv);
            shell_invalidate(srv);
            return 0;
        }

        /* Otherwise forward key event to focused window */
        if (srv->focused_window_id != 0) {
            vanilla_server_send_input(srv, srv->focused_window_id, ev);
            return 0;
        }
    }

    return 0;
}

void wm_render_cursor(vanilla_server_t *srv, const vanilla_rect_t *dirty)
{
    if (!srv || !dirty)
        return;

    vanilla_compositor_t *comp = &srv->compositor;
    vanilla_rect_t cursor_rect = { srv->cursor_x, srv->cursor_y, 16, 16 };
    vanilla_rect_t vis;

    if (!vanilla_rect_intersect(&cursor_rect, dirty, &vis))
        return;

    for (int cy = 0; cy < 16; cy++) {
        int32_t py = srv->cursor_y + cy;
        if (py < dirty->y || py >= dirty->y + dirty->h || py >= (int32_t)comp->height)
            continue;

        for (int cx = 0; cx < 16; cx++) {
            int32_t px = srv->cursor_x + cx;
            if (px < dirty->x || px >= dirty->x + dirty->w || px >= (int32_t)comp->width)
                continue;

            uint16_t mask_bit = (cursor_arrow_mask[cy] >> (15 - cx)) & 1;
            if (mask_bit) {
                uint16_t body_bit = (cursor_arrow_body[cy] >> (15 - cx)) & 1;
                uint32_t col = body_bit ? 0xFFFFFFFF : 0xFF000000;
                comp->backbuffer[py * comp->pitch_px + px] = col;
            }
        }
    }
}
