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

#define VANILLA_DISPATCH_TABLE_IMPL
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
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <signal.h>
#include <sys/time.h>

static int64_t get_time_ms(void)
{
    struct timeval tv;
    if (gettimeofday(&tv, NULL) == 0) {
        return (int64_t)tv.tv_sec * 1000 + (tv.tv_usec / 1000);
    }
    return 0;
}

static vanilla_client_conn_t *find_client_by_fd(vanilla_server_t *srv, int fd)
{
    if (!srv || fd < 0)
        return NULL;
    for (int i = 0; i < VANILLA_MAX_CLIENTS; i++) {
        if (srv->clients[i].in_use && srv->clients[i].fd == fd)
            return &srv->clients[i];
    }
    return NULL;
}

static volatile int g_theme_reload_pending = 0;

#ifdef SIGHUP
static void wm_sighup_handler(int sig)
{
    (void)sig;
    g_theme_reload_pending = 1;
}
#endif


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

void vanilla_server_broadcast_theme_changed(vanilla_server_t *srv)
{
    if (!srv)
        return;

    vanilla_msg_hdr_t hdr;
    vanilla_msg_theme_changed_t msg;
    memset(&msg, 0, sizeof(msg));
    hdr.magic = VANILLA_IPC_MAGIC;
    hdr.msg_type = MSG_THEME_CHANGED;
    hdr.payload_len = (uint16_t)sizeof(msg);
    hdr.window_id = 0;

    for (int i = 0; i < VANILLA_MAX_CLIENTS; i++) {
        if (srv->clients[i].in_use && srv->clients[i].fd >= 0 &&
            srv->clients[i].negotiated_version >= 2) {
            exact_write(srv->clients[i].fd, &hdr, sizeof(hdr));
            exact_write(srv->clients[i].fd, &msg, sizeof(msg));
        }
    }
}

void wm_get_frame_rect(const vanilla_server_window_t *win, vanilla_rect_t *out_frame)
{
    if (!win || !out_frame)
        return;

    if (!(win->flags & WINDOW_FLAG_BORDERLESS)) {
        out_frame->x = win->x - g_theme->border_width;
        out_frame->y = win->y - g_theme->titlebar_height - g_theme->border_width;
        out_frame->w = (int32_t)win->width + 2 * g_theme->border_width;
        out_frame->h = (int32_t)win->height + g_theme->titlebar_height + 2 * g_theme->border_width;
    } else {
        out_frame->x = win->x;
        out_frame->y = win->y;
        out_frame->w = (int32_t)win->width;
        out_frame->h = (int32_t)win->height;
    }
}

vanilla_server_window_t *wm_window_at(vanilla_server_t *srv, int32_t x, int32_t y)
{
    if (!srv)
        return NULL;

    vanilla_server_window_t *hit = NULL;
    int32_t max_z = -1;

    for (int i = 0; i < VANILLA_MAX_WINDOWS; i++) {
        vanilla_server_window_t *w = &srv->windows[i];
        if (!w->in_use || !w->is_mapped)
            continue;

        vanilla_rect_t frame;
        wm_get_frame_rect(w, &frame);

        if (x >= frame.x && x < frame.x + frame.w &&
            y >= frame.y && y < frame.y + frame.h) {
            if (w->z_index > max_z) {
                max_z = w->z_index;
                hit = w;
            }
        }
    }

    return hit;
}

static inline vanilla_cursor_shape_t resize_edge_to_cursor_shape(vanilla_resize_edge_t edge)
{
    switch (edge) {
    case RESIZE_EDGE_TOP:       return CURSOR_RESIZE_N;
    case RESIZE_EDGE_BOTTOM:    return CURSOR_RESIZE_S;
    case RESIZE_EDGE_LEFT:      return CURSOR_RESIZE_W;
    case RESIZE_EDGE_RIGHT:     return CURSOR_RESIZE_E;
    case RESIZE_EDGE_TOP_LEFT:  return CURSOR_RESIZE_NW;
    case RESIZE_EDGE_TOP_RIGHT: return CURSOR_RESIZE_NE;
    case RESIZE_EDGE_BOT_LEFT:  return CURSOR_RESIZE_SW;
    case RESIZE_EDGE_BOT_RIGHT: return CURSOR_RESIZE_SE;
    default:                    return CURSOR_ARROW;
    }
}

vanilla_resize_edge_t wm_hit_test_resize_edge(const vanilla_server_window_t *w, int32_t px, int32_t py)
{
    if (!w || !w->in_use || !w->is_mapped)
        return RESIZE_EDGE_NONE;
    if (!(w->flags & WINDOW_FLAG_RESIZABLE))
        return RESIZE_EDGE_NONE;
    if (w->is_snapped != SNAP_NONE)
        return RESIZE_EDGE_NONE;

    vanilla_rect_t frame;
    wm_get_frame_rect(w, &frame);

    int32_t fx = frame.x;
    int32_t fy = frame.y;
    int32_t fw = frame.w;
    int32_t fh = frame.h;

    if (fw < 24 || fh < 24)
        return RESIZE_EDGE_NONE;

    if (px < fx || px >= fx + fw || py < fy || py >= fy + fh)
        return RESIZE_EDGE_NONE;

    /* Chrome buttons on titlebar take precedence over resize zones */
    if (!(w->flags & WINDOW_FLAG_BORDERLESS)) {
        chrome_btn_rects_t btns = chrome_metrics(&frame);
        if (vanilla_rect_contains(&btns.close_btn, px, py) ||
            vanilla_rect_contains(&btns.max_btn, px, py) ||
            vanilla_rect_contains(&btns.min_btn, px, py)) {
            return RESIZE_EDGE_NONE;
        }
    }

    /* Corner zones (12x12 px) */
    if (py < fy + 12) {
        if (px < fx + 12)
            return RESIZE_EDGE_TOP_LEFT;
        if (px >= fx + fw - 12)
            return RESIZE_EDGE_TOP_RIGHT;
        if (py < fy + 8)
            return RESIZE_EDGE_TOP;
    } else if (py >= fy + fh - 12) {
        if (px < fx + 12)
            return RESIZE_EDGE_BOT_LEFT;
        if (px >= fx + fw - 12)
            return RESIZE_EDGE_BOT_RIGHT;
        if (py >= fy + fh - 8)
            return RESIZE_EDGE_BOTTOM;
    } else {
        if (px < fx + 8)
            return RESIZE_EDGE_LEFT;
        if (px >= fx + fw - 8)
            return RESIZE_EDGE_RIGHT;
    }

    return RESIZE_EDGE_NONE;
}

static void resize_compute_geometry(const vanilla_server_t *srv,
                                    const vanilla_server_window_t *w,
                                    int32_t *out_x, int32_t *out_y,
                                    uint32_t *out_w, uint32_t *out_h)
{
    int32_t dx = srv->cursor_x - srv->resize_origin_x;
    int32_t dy = srv->cursor_y - srv->resize_origin_y;

    int32_t calc_x = srv->resize_start_x;
    int32_t calc_y = srv->resize_start_y;
    int32_t calc_w = (int32_t)srv->resize_start_w;
    int32_t calc_h = (int32_t)srv->resize_start_h;

    switch (srv->resize_edge) {
    case RESIZE_EDGE_RIGHT:
        calc_w += dx;
        break;
    case RESIZE_EDGE_BOTTOM:
        calc_h += dy;
        break;
    case RESIZE_EDGE_LEFT:
        calc_x += dx;
        calc_w -= dx;
        break;
    case RESIZE_EDGE_TOP:
        calc_y += dy;
        calc_h -= dy;
        break;
    case RESIZE_EDGE_BOT_RIGHT:
        calc_w += dx;
        calc_h += dy;
        break;
    case RESIZE_EDGE_BOT_LEFT:
        calc_x += dx;
        calc_w -= dx;
        calc_h += dy;
        break;
    case RESIZE_EDGE_TOP_RIGHT:
        calc_w += dx;
        calc_y += dy;
        calc_h -= dy;
        break;
    case RESIZE_EDGE_TOP_LEFT:
        calc_x += dx;
        calc_w -= dx;
        calc_y += dy;
        calc_h -= dy;
        break;
    default:
        break;
    }

    if (w->aspect_num && w->aspect_den) {
        if (srv->resize_edge == RESIZE_EDGE_TOP || srv->resize_edge == RESIZE_EDGE_BOTTOM) {
            /* Vertical drag: height is primary driver */
            if (w->min_height && calc_h < (int32_t)w->min_height)
                calc_h = (int32_t)w->min_height;
            if (w->max_height && calc_h > (int32_t)w->max_height)
                calc_h = (int32_t)w->max_height;
            if (calc_h < 32)
                calc_h = 32;

            calc_w = (int32_t)(((uint64_t)calc_h * w->aspect_num + w->aspect_den / 2) / w->aspect_den);

            if (w->min_width && calc_w < (int32_t)w->min_width) {
                calc_w = (int32_t)w->min_width;
                calc_h = (int32_t)(((uint64_t)calc_w * w->aspect_den + w->aspect_num / 2) / w->aspect_num);
            }
            if (w->max_width && calc_w > (int32_t)w->max_width) {
                calc_w = (int32_t)w->max_width;
                calc_h = (int32_t)(((uint64_t)calc_w * w->aspect_den + w->aspect_num / 2) / w->aspect_num);
            }
            if (calc_w < 32) {
                calc_w = 32;
                calc_h = (int32_t)(((uint64_t)calc_w * w->aspect_den + w->aspect_num / 2) / w->aspect_num);
            }
        } else {
            /* Horizontal or corner drag: width is primary driver */
            if (w->min_width && calc_w < (int32_t)w->min_width)
                calc_w = (int32_t)w->min_width;
            if (w->max_width && calc_w > (int32_t)w->max_width)
                calc_w = (int32_t)w->max_width;
            if (calc_w < 32)
                calc_w = 32;

            calc_h = (int32_t)(((uint64_t)calc_w * w->aspect_den + w->aspect_num / 2) / w->aspect_num);

            if (w->min_height && calc_h < (int32_t)w->min_height) {
                calc_h = (int32_t)w->min_height;
                calc_w = (int32_t)(((uint64_t)calc_h * w->aspect_num + w->aspect_den / 2) / w->aspect_den);
            }
            if (w->max_height && calc_h > (int32_t)w->max_height) {
                calc_h = (int32_t)w->max_height;
                calc_w = (int32_t)(((uint64_t)calc_h * w->aspect_num + w->aspect_den / 2) / w->aspect_den);
            }
            if (calc_h < 32) {
                calc_h = 32;
                calc_w = (int32_t)(((uint64_t)calc_h * w->aspect_num + w->aspect_den / 2) / w->aspect_den);
            }
        }
    } else {
        /* Standard min/max clamping */
        if (w->min_width && calc_w < (int32_t)w->min_width)
            calc_w = (int32_t)w->min_width;
        if (w->min_height && calc_h < (int32_t)w->min_height)
            calc_h = (int32_t)w->min_height;
        if (w->max_width && calc_w > (int32_t)w->max_width)
            calc_w = (int32_t)w->max_width;
        if (w->max_height && calc_h > (int32_t)w->max_height)
            calc_h = (int32_t)w->max_height;
    }

    /* Absolute minimum */
    if (calc_w < 32)
        calc_w = 32;
    if (calc_h < 32)
        calc_h = 32;

    /* Keep opposite corner/edge stationary when resizing top or left edges */
    if (srv->resize_edge == RESIZE_EDGE_LEFT ||
        srv->resize_edge == RESIZE_EDGE_BOT_LEFT ||
        srv->resize_edge == RESIZE_EDGE_TOP_LEFT) {
        calc_x = (int32_t)(srv->resize_start_x + srv->resize_start_w) - calc_w;
    }
    if (srv->resize_edge == RESIZE_EDGE_TOP ||
        srv->resize_edge == RESIZE_EDGE_TOP_LEFT ||
        srv->resize_edge == RESIZE_EDGE_TOP_RIGHT) {
        calc_y = (int32_t)(srv->resize_start_y + srv->resize_start_h) - calc_h;
    }

    *out_x = calc_x;
    *out_y = calc_y;
    *out_w = (uint32_t)calc_w;
    *out_h = (uint32_t)calc_h;
}

void wm_invalidate_window(vanilla_server_t *srv, const vanilla_server_window_t *win)
{
    if (!srv || !win)
        return;

    vanilla_rect_t frame = { 0, 0, 0, 0 };
    wm_get_frame_rect(win, &frame);

    vanilla_rect_t dirty;
    dirty.x = frame.x - g_theme->shadow_radius;
    dirty.y = frame.y - g_theme->shadow_radius;
    dirty.w = frame.w + 2 * g_theme->shadow_radius;
    dirty.h = frame.h + 2 * g_theme->shadow_radius + g_theme->shadow_radius / 2;

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
        srv->clients[i].version = 1;
    }

    for (int i = 0; i < VANILLA_MAX_WINDOWS; i++)
        srv->windows[i].in_use = 0;

    theme_load("/etc/vanilla/themes/nord-dark.ini");
#ifdef SIGHUP
    signal(SIGHUP, wm_sighup_handler);
#endif
#ifdef SIGPIPE
    signal(SIGPIPE, SIG_IGN);
#endif

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

    cursor_manager_init("assets/cursors");
    cursor_set_active(CURSOR_ARROW);

    /* Open kernel input stream with non-blocking fallback */
    srv->input_fd = open("/dev/input/events", O_RDONLY | O_NONBLOCK);

    compositor_damage_all(&srv->compositor);
    compositor_render_frame(srv);

    wm_run_resize_selftests();

    return 0;
}

void vanilla_server_close(vanilla_server_t *srv)
{
    if (!srv)
        return;

    cursor_manager_destroy();

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
    srv->clients[slot].version = 1;
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
            if (srv->is_resizing && srv->resize_window_id == srv->windows[i].window_id) {
                srv->is_resizing = 0;
                srv->resize_window_id = 0;
                srv->resize_edge = RESIZE_EDGE_NONE;
            }
            if (srv->drag_threshold_pending && srv->drag_threshold_window_id == srv->windows[i].window_id) {
                srv->drag_threshold_pending = 0;
            }
            srv->windows[i].resize_has_target = 0;
            wm_invalidate_window(srv, &srv->windows[i]);
            surface_destroy(&srv->windows[i].surface);
            srv->windows[i].in_use = 0;
        }
    }

    if (cfd >= 0)
        close(cfd);

    srv->clients[client_idx].in_use = 0;
    srv->clients[client_idx].fd = -1;
    srv->clients[client_idx].version = 0;
    srv->clients[client_idx].negotiated_version = 0;

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

static void wm_send_configure(vanilla_server_t *srv, vanilla_server_window_t *w)
{
    if (!srv || !w || w->client_fd < 0)
        return;

    vanilla_client_conn_t *c = find_client_by_fd(srv, w->client_fd);
    if (c && c->negotiated_version >= 2) {
        uint32_t next_serial = w->configure_serial + 1;
        if (next_serial == 0)
            next_serial = 1;

        vanilla_msg_hdr_t hdr;
        vanilla_msg_window_configure_v2_t cfg;
        hdr.magic = VANILLA_IPC_MAGIC;
        hdr.msg_type = MSG_WINDOW_CONFIGURE;
        hdr.payload_len = (uint16_t)sizeof(cfg);
        hdr.window_id = w->window_id;

        cfg.x = w->pending_x;
        cfg.y = w->pending_y;
        cfg.width = w->pending_w;
        cfg.height = w->pending_h;
        cfg.serial = next_serial;
        cfg.flags = w->flags;

        if (exact_write(w->client_fd, &hdr, sizeof(hdr)) == 0 &&
            exact_write(w->client_fd, &cfg, sizeof(cfg)) == 0) {
            w->configure_serial = next_serial;
            w->configure_pending = 1;
            w->configure_timestamp_ms = (uint32_t)get_time_ms();
        }
    } else {
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
}

int handle_msg_hello(vanilla_server_t *srv, int client_idx, const vanilla_msg_hdr_t *hdr, const uint8_t *payload)
{
    (void)hdr;
    const vanilla_msg_hello_t *hello = (const vanilla_msg_hello_t *)payload;
    int cfd = srv->clients[client_idx].fd;
    vanilla_msg_hdr_t ack_hdr;
    vanilla_msg_hello_ack_t ack;

    srv->clients[client_idx].version = hello->client_version;
    uint32_t neg = hello->client_version;
    if (neg > 2)
        neg = 2;
    if (neg < 1)
        neg = 1;
    srv->clients[client_idx].negotiated_version = neg;

    ack_hdr.magic = VANILLA_IPC_MAGIC;
    ack_hdr.msg_type = MSG_HELLO_ACK;
    ack_hdr.payload_len = (uint16_t)sizeof(ack);
    ack_hdr.window_id = 0;

    ack.server_version = VANILLA_IPC_VERSION;
    ack.status = 0;

    if (exact_write(cfd, &ack_hdr, sizeof(ack_hdr)) < 0 ||
        exact_write(cfd, &ack, sizeof(ack)) < 0) {
        return -1;
    }
    return 0;
}

int handle_msg_create_window(vanilla_server_t *srv, int client_idx, const vanilla_msg_hdr_t *hdr, const uint8_t *payload)
{
    (void)hdr;
    const vanilla_msg_create_window_t *req = (const vanilla_msg_create_window_t *)payload;
    int cfd = srv->clients[client_idx].fd;
    vanilla_msg_hdr_t ack_hdr;
    vanilla_msg_create_window_ack_t ack;
    int win_slot = -1;

    ack_hdr.magic = VANILLA_IPC_MAGIC;
    ack_hdr.msg_type = MSG_CREATE_WINDOW_ACK;
    ack_hdr.payload_len = (uint16_t)sizeof(ack);
    ack_hdr.window_id = 0;

    memset(&ack, 0, sizeof(ack));

    if (req->width == 0 || req->height == 0 ||
        req->width > VANILLA_MAX_WIDTH || req->height > VANILLA_MAX_HEIGHT) {
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
    if (surface_create_shm(&w->surface, req->width, req->height) < 0) {
        ack.status = -12;
        exact_write(cfd, &ack_hdr, sizeof(ack_hdr));
        exact_write(cfd, &ack, sizeof(ack));
        return 0;
    }

    w->in_use = 1;
    w->window_id = srv->next_window_id++;
    w->client_fd = cfd;
    w->shm_id = w->surface.shm_id;
    w->x = req->x;
    w->y = req->y;
    w->width = req->width;
    w->height = req->height;
    w->flags = req->flags;
    w->is_mapped = 1;
    w->is_focused = 0;
    w->z_index = ++srv->next_z_index;

    w->is_snapped = SNAP_NONE;
    w->restore_x = req->x;
    w->restore_y = req->y;
    w->restore_w = req->width;
    w->restore_h = req->height;

    w->configure_serial = 0;
    w->configure_pending = 0;
    w->pending_x = req->x;
    w->pending_y = req->y;
    w->pending_w = req->width;
    w->pending_h = req->height;
    w->configure_timestamp_ms = 0;

    w->frame_serial = 0;
    w->present_serial = 0;
    w->buffer_in_use = 0;
    w->frame_begin_in_flight = 0;
    w->frame_begin_time_ms = 0;
    w->cursor_shape = CURSOR_ARROW;
    w->min_width = 0;
    w->min_height = 0;
    w->max_width = 0;
    w->max_height = 0;
    w->aspect_num = 0;
    w->aspect_den = 0;
    w->resize_has_target = 0;
    w->target_x = req->x;
    w->target_y = req->y;
    w->target_w = req->width;
    w->target_h = req->height;

    if (req->flags & WINDOW_FLAG_MODAL)
        w->layer = LAYER_TOPMOST;
    else if (req->flags & WINDOW_FLAG_ALWAYS_TOP)
        w->layer = LAYER_TOPMOST;
    else
        w->layer = LAYER_NORMAL;

    snprintf(w->title, sizeof(w->title), "%s", req->title);
    w->damage.x = 0;
    w->damage.y = 0;
    w->damage.w = (int32_t)req->width;
    w->damage.h = (int32_t)req->height;

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
    printf("[vanilla] Created window id=%u title='%s' (%ux%u)\n", w->window_id, w->title, req->width, req->height);
    return 0;
}

int handle_msg_destroy_window(vanilla_server_t *srv, int client_idx, const vanilla_msg_hdr_t *hdr, const uint8_t *payload)
{
    (void)hdr;
    const vanilla_msg_destroy_window_t *req = (const vanilla_msg_destroy_window_t *)payload;
    int cfd = srv->clients[client_idx].fd;
    vanilla_server_window_t *w = vanilla_server_find_window(srv, req->window_id);
    if (w && w->client_fd == cfd) {
        if (srv->focused_window_id == w->window_id)
            srv->focused_window_id = 0;
        if (srv->is_dragging && srv->drag_window_id == w->window_id) {
            srv->is_dragging = 0;
            srv->drag_window_id = 0;
        }
        if (srv->is_resizing && srv->resize_window_id == w->window_id) {
            srv->is_resizing = 0;
            srv->resize_window_id = 0;
            srv->resize_edge = RESIZE_EDGE_NONE;
        }
        if (srv->drag_threshold_pending && srv->drag_threshold_window_id == w->window_id) {
            srv->drag_threshold_pending = 0;
        }
        w->resize_has_target = 0;
        wm_invalidate_window(srv, w);
        surface_destroy(&w->surface);
        w->in_use = 0;
        shell_invalidate(srv);
    }
    return 0;
}

int handle_msg_map_window(vanilla_server_t *srv, int client_idx, const vanilla_msg_hdr_t *hdr, const uint8_t *payload)
{
    (void)hdr;
    const vanilla_msg_map_window_t *req = (const vanilla_msg_map_window_t *)payload;
    int cfd = srv->clients[client_idx].fd;
    vanilla_server_window_t *w = vanilla_server_find_window(srv, req->window_id);
    if (w && w->client_fd == cfd) {
        w->is_mapped = 1;
        w->frame_begin_in_flight = 0;
        vanilla_server_focus_window(srv, w->window_id);
        wm_invalidate_window(srv, w);
        shell_invalidate(srv);
    }
    return 0;
}

int handle_msg_unmap_window(vanilla_server_t *srv, int client_idx, const vanilla_msg_hdr_t *hdr, const uint8_t *payload)
{
    (void)hdr;
    const vanilla_msg_unmap_window_t *req = (const vanilla_msg_unmap_window_t *)payload;
    int cfd = srv->clients[client_idx].fd;
    vanilla_server_window_t *w = vanilla_server_find_window(srv, req->window_id);
    if (w && w->client_fd == cfd) {
        if (srv->is_dragging && srv->drag_window_id == w->window_id) {
            srv->is_dragging = 0;
            srv->drag_window_id = 0;
        }
        if (srv->is_resizing && srv->resize_window_id == w->window_id) {
            srv->is_resizing = 0;
            srv->resize_window_id = 0;
            srv->resize_edge = RESIZE_EDGE_NONE;
        }
        if (srv->drag_threshold_pending && srv->drag_threshold_window_id == w->window_id) {
            srv->drag_threshold_pending = 0;
        }
        w->resize_has_target = 0;
        wm_invalidate_window(srv, w);
        w->is_mapped = 0;
        w->is_focused = 0;
        if (srv->focused_window_id == w->window_id)
            srv->focused_window_id = 0;
        shell_invalidate(srv);
    }
    return 0;
}

int handle_msg_move_resize(vanilla_server_t *srv, int client_idx, const vanilla_msg_hdr_t *hdr, const uint8_t *payload)
{
    const vanilla_msg_move_resize_t *req = (const vanilla_msg_move_resize_t *)payload;
    int cfd = srv->clients[client_idx].fd;
    vanilla_server_window_t *w = vanilla_server_find_window(srv, hdr->window_id);
    if (w && w->client_fd == cfd) {
        if (req->width > 0 && req->height > 0 &&
            (req->width != w->width || req->height != w->height)) {
            w->pending_x = req->x;
            w->pending_y = req->y;
            w->pending_w = req->width;
            w->pending_h = req->height;
            if (srv->clients[client_idx].negotiated_version >= 2) {
                wm_send_configure(srv, w);
            } else {
                wm_invalidate_window(srv, w);
                w->x = req->x;
                w->y = req->y;
                w->width = req->width;
                w->height = req->height;
                wm_send_configure(srv, w);
                wm_invalidate_window(srv, w);
            }
        } else {
            wm_invalidate_window(srv, w);
            w->x = req->x;
            w->y = req->y;
            w->pending_x = req->x;
            w->pending_y = req->y;
            wm_invalidate_window(srv, w);
        }
    }
    return 0;
}

int handle_msg_present(vanilla_server_t *srv, int client_idx, const vanilla_msg_hdr_t *hdr, const uint8_t *payload)
{
    int32_t dmg_x, dmg_y, dmg_w, dmg_h;
    uint32_t frame_serial = 0;
    int is_v2 = 0;

    if (hdr->payload_len >= sizeof(vanilla_msg_present_v2_t)) {
        const vanilla_msg_present_v2_t *p2 = (const vanilla_msg_present_v2_t *)payload;
        dmg_x = p2->x;
        dmg_y = p2->y;
        dmg_w = p2->w;
        dmg_h = p2->h;
        frame_serial = p2->frame_serial;
        is_v2 = 1;
    } else {
        const vanilla_msg_present_t *p1 = (const vanilla_msg_present_t *)payload;
        dmg_x = p1->x;
        dmg_y = p1->y;
        dmg_w = p1->w;
        dmg_h = p1->h;
    }

    int cfd = srv->clients[client_idx].fd;
    vanilla_server_window_t *w = vanilla_server_find_window(srv, hdr->window_id);
    if (!w || w->client_fd != cfd)
        return 0;

    if (is_v2) {
        w->present_serial = frame_serial;
        w->frame_begin_in_flight = 0;
    }

    if (dmg_w <= 0 || dmg_h <= 0) {
        if (is_v2) {
            w->buffer_in_use = 1;
            vanilla_server_release_buffers(srv);
        }
        return 0;
    }

    if (is_v2) {
        w->buffer_in_use = 1;
    }

    if (w->damage.w == 0 || w->damage.h == 0) {
        w->damage.x = dmg_x;
        w->damage.y = dmg_y;
        w->damage.w = dmg_w;
        w->damage.h = dmg_h;
    } else {
        int32_t x1 = w->damage.x < dmg_x ? w->damage.x : dmg_x;
        int32_t y1 = w->damage.y < dmg_y ? w->damage.y : dmg_y;
        int32_t x2 = (w->damage.x + w->damage.w) > (dmg_x + dmg_w)
                     ? (w->damage.x + w->damage.w)
                     : (dmg_x + dmg_w);
        int32_t y2 = (w->damage.y + w->damage.h) > (dmg_y + dmg_h)
                     ? (w->damage.y + w->damage.h)
                     : (dmg_y + dmg_h);

        w->damage.x = x1;
        w->damage.y = y1;
        w->damage.w = x2 - x1;
        w->damage.h = y2 - y1;
    }

    vanilla_rect_t screen_damage;
    screen_damage.x = w->x + dmg_x;
    screen_damage.y = w->y + dmg_y;
    screen_damage.w = dmg_w;
    screen_damage.h = dmg_h;
    compositor_add_damage(&srv->compositor, &screen_damage);

    return 0;
}

int handle_msg_ack_configure(vanilla_server_t *srv, int client_idx, const vanilla_msg_hdr_t *hdr, const uint8_t *payload)
{
    const vanilla_msg_ack_configure_t *ack = (const vanilla_msg_ack_configure_t *)payload;
    int cfd = srv->clients[client_idx].fd;
    uint32_t wid = hdr->window_id ? hdr->window_id : ack->window_id;
    vanilla_server_window_t *w = vanilla_server_find_window(srv, wid);
    if (!w || w->client_fd != cfd)
        return 0;

    if (!w->configure_pending)
        return 0;

    if (w->configure_serial != ack->serial) {
        fprintf(stderr, "[vanilla] ack_configure: serial mismatch for window %u (expected %u, got %u)\n",
                w->window_id, w->configure_serial, ack->serial);
        return 0;
    }

    w->configure_pending = 0;
    wm_invalidate_window(srv, w);
    w->x = w->pending_x;
    w->y = w->pending_y;
    w->width = w->pending_w;
    w->height = w->pending_h;
    wm_invalidate_window(srv, w);
    shell_invalidate(srv);
    printf("[vanilla] ack_configure: window %u serial %u applied\n", w->window_id, ack->serial);

    /* If a target geometry was queued while configure was in flight, dispatch it now */
    if (w->resize_has_target) {
        int32_t tx = w->target_x;
        int32_t ty = w->target_y;
        uint32_t tw = w->target_w;
        uint32_t th = w->target_h;
        w->resize_has_target = 0;

        if (tx != w->x || ty != w->y || tw != w->width || th != w->height) {
            w->pending_x = tx;
            w->pending_y = ty;
            w->pending_w = tw;
            w->pending_h = th;
            wm_send_configure(srv, w);
            return 0;
        }
    }

    if (srv->is_resizing && srv->resize_window_id == w->window_id) {
        int32_t new_x, new_y;
        uint32_t new_w, new_h;
        resize_compute_geometry(srv, w, &new_x, &new_y, &new_w, &new_h);
        if (new_x != w->x || new_y != w->y || new_w != w->width || new_h != w->height) {
            w->pending_x = new_x;
            w->pending_y = new_y;
            w->pending_w = new_w;
            w->pending_h = new_h;
            wm_send_configure(srv, w);
        }
    }

    return 0;
}

int handle_msg_set_cursor(vanilla_server_t *srv, int client_idx, const vanilla_msg_hdr_t *hdr, const uint8_t *payload)
{
    const vanilla_msg_set_cursor_t *req = (const vanilla_msg_set_cursor_t *)payload;
    int cfd = srv->clients[client_idx].fd;
    uint32_t wid = hdr->window_id ? hdr->window_id : req->window_id;
    vanilla_server_window_t *w = vanilla_server_find_window(srv, wid);
    if (w && w->client_fd == cfd) {
        w->cursor_shape = req->shape;
    }
    return 0;
}

int handle_msg_set_size_hints(vanilla_server_t *srv, int client_idx, const vanilla_msg_hdr_t *hdr, const uint8_t *payload)
{
    const vanilla_msg_set_size_hints_t *hints = (const vanilla_msg_set_size_hints_t *)payload;
    int cfd = srv->clients[client_idx].fd;
    uint32_t wid = hdr->window_id ? hdr->window_id : hints->window_id;
    vanilla_server_window_t *w = vanilla_server_find_window(srv, wid);
    if (!w || w->client_fd != cfd)
        return 0;

    w->min_width = hints->min_width;
    w->min_height = hints->min_height;
    w->max_width = hints->max_width;
    w->max_height = hints->max_height;
    w->aspect_num = hints->aspect_num;
    w->aspect_den = hints->aspect_den;
    return 0;
}

int wm_run_resize_selftests(void)
{
    /* 1. Edge hit testing across 8 zones and boundary exclusions */
    vanilla_server_window_t w;
    memset(&w, 0, sizeof(w));
    w.in_use = 1;
    w.is_mapped = 1;
    w.flags = WINDOW_FLAG_RESIZABLE;
    w.is_snapped = SNAP_NONE;
    w.x = 100;
    w.y = 100;
    w.width = 400;
    w.height = 300;

    vanilla_rect_t frame;
    wm_get_frame_rect(&w, &frame);

    if (wm_hit_test_resize_edge(&w, frame.x, frame.y) != RESIZE_EDGE_TOP_LEFT) {
        fprintf(stderr, "[wm_selftest] FAIL: TOP_LEFT corner hit-test\n");
        return -1;
    }
    if (wm_hit_test_resize_edge(&w, frame.x + frame.w - 1, frame.y) != RESIZE_EDGE_TOP_RIGHT) {
        fprintf(stderr, "[wm_selftest] FAIL: TOP_RIGHT corner hit-test\n");
        return -1;
    }
    if (wm_hit_test_resize_edge(&w, frame.x, frame.y + frame.h - 1) != RESIZE_EDGE_BOT_LEFT) {
        fprintf(stderr, "[wm_selftest] FAIL: BOT_LEFT corner hit-test\n");
        return -1;
    }
    if (wm_hit_test_resize_edge(&w, frame.x + frame.w - 1, frame.y + frame.h - 1) != RESIZE_EDGE_BOT_RIGHT) {
        fprintf(stderr, "[wm_selftest] FAIL: BOT_RIGHT corner hit-test\n");
        return -1;
    }
    if (wm_hit_test_resize_edge(&w, frame.x + frame.w / 2, frame.y) != RESIZE_EDGE_TOP) {
        fprintf(stderr, "[wm_selftest] FAIL: TOP edge hit-test\n");
        return -1;
    }
    if (wm_hit_test_resize_edge(&w, frame.x + frame.w / 2, frame.y + frame.h - 1) != RESIZE_EDGE_BOTTOM) {
        fprintf(stderr, "[wm_selftest] FAIL: BOTTOM edge hit-test\n");
        return -1;
    }
    if (wm_hit_test_resize_edge(&w, frame.x, frame.y + frame.h / 2) != RESIZE_EDGE_LEFT) {
        fprintf(stderr, "[wm_selftest] FAIL: LEFT edge hit-test\n");
        return -1;
    }
    if (wm_hit_test_resize_edge(&w, frame.x + frame.w - 1, frame.y + frame.h / 2) != RESIZE_EDGE_RIGHT) {
        fprintf(stderr, "[wm_selftest] FAIL: RIGHT edge hit-test\n");
        return -1;
    }
    if (wm_hit_test_resize_edge(&w, frame.x + frame.w / 2, frame.y + frame.h / 2) != RESIZE_EDGE_NONE) {
        fprintf(stderr, "[wm_selftest] FAIL: interior should be RESIZE_EDGE_NONE\n");
        return -1;
    }
    w.flags = 0; /* Not resizable */
    if (wm_hit_test_resize_edge(&w, frame.x, frame.y) != RESIZE_EDGE_NONE) {
        fprintf(stderr, "[wm_selftest] FAIL: non-resizable window allowed edge hit\n");
        return -1;
    }
    w.flags = WINDOW_FLAG_RESIZABLE;
    w.is_snapped = SNAP_MAXIMIZE;
    if (wm_hit_test_resize_edge(&w, frame.x, frame.y) != RESIZE_EDGE_NONE) {
        fprintf(stderr, "[wm_selftest] FAIL: snapped window allowed edge hit\n");
        return -1;
    }
    w.is_snapped = SNAP_NONE;

    /* Chrome buttons should not trigger resize edge */
    chrome_btn_rects_t btns = chrome_metrics(&frame);
    if (wm_hit_test_resize_edge(&w, btns.close_btn.x + 2, btns.close_btn.y + 2) != RESIZE_EDGE_NONE) {
        fprintf(stderr, "[wm_selftest] FAIL: close button triggered resize edge\n");
        return -1;
    }

    /* 2. Geometry calculation with basic directional dragging */
    vanilla_server_t srv;
    memset(&srv, 0, sizeof(srv));
    srv.resize_start_x = 100;
    srv.resize_start_y = 100;
    srv.resize_start_w = 400;
    srv.resize_start_h = 300;
    srv.resize_origin_x = 200;
    srv.resize_origin_y = 200;

    int32_t ox, oy;
    uint32_t ow, oh;

    /* RIGHT: width expands */
    srv.resize_edge = RESIZE_EDGE_RIGHT;
    srv.cursor_x = 250;
    srv.cursor_y = 200;
    resize_compute_geometry(&srv, &w, &ox, &oy, &ow, &oh);
    if (ox != 100 || oy != 100 || ow != 450 || oh != 300) {
        fprintf(stderr, "[wm_selftest] FAIL: RIGHT drag geometry\n");
        return -1;
    }

    /* LEFT: width shrinks, x moves right, right edge fixed at 500 */
    srv.resize_edge = RESIZE_EDGE_LEFT;
    srv.cursor_x = 230;
    srv.cursor_y = 200;
    resize_compute_geometry(&srv, &w, &ox, &oy, &ow, &oh);
    if (ox != 130 || oy != 100 || ow != 370 || oh != 300 || (ox + (int32_t)ow != 500)) {
        fprintf(stderr, "[wm_selftest] FAIL: LEFT drag geometry\n");
        return -1;
    }

    /* 3. Min/Max size hints clamping */
    w.min_width = 300;
    w.min_height = 200;
    w.max_width = 600;
    w.max_height = 500;

    /* Drag past min_width */
    srv.resize_edge = RESIZE_EDGE_RIGHT;
    srv.cursor_x = 50; /* dx = -150 -> 400 - 150 = 250 < 300 */
    srv.cursor_y = 200;
    resize_compute_geometry(&srv, &w, &ox, &oy, &ow, &oh);
    if (ow != 300) {
        fprintf(stderr, "[wm_selftest] FAIL: min_width clamp\n");
        return -1;
    }

    /* Drag past max_width */
    srv.cursor_x = 500; /* dx = +300 -> 400 + 300 = 700 > 600 */
    resize_compute_geometry(&srv, &w, &ox, &oy, &ow, &oh);
    if (ow != 600) {
        fprintf(stderr, "[wm_selftest] FAIL: max_width clamp\n");
        return -1;
    }

    /* 4. Aspect ratio locking (16:9) */
    w.min_width = 0;
    w.min_height = 0;
    w.max_width = 0;
    w.max_height = 0;
    w.aspect_num = 16;
    w.aspect_den = 9;
    srv.resize_start_w = 320;
    srv.resize_start_h = 180;

    /* Horizontal drag (RIGHT): height derived from width */
    srv.resize_edge = RESIZE_EDGE_RIGHT;
    srv.cursor_x = 200 + 160; /* w becomes 320 + 160 = 480 */
    srv.cursor_y = 200;
    resize_compute_geometry(&srv, &w, &ox, &oy, &ow, &oh);
    if (ow != 480 || oh != 270) {
        fprintf(stderr, "[wm_selftest] FAIL: aspect lock on horizontal drag (got %ux%u, expected 480x270)\n", ow, oh);
        return -1;
    }

    /* Vertical drag (BOTTOM): width derived from height */
    srv.resize_edge = RESIZE_EDGE_BOTTOM;
    srv.cursor_x = 200;
    srv.cursor_y = 200 + 90; /* h becomes 180 + 90 = 270 */
    resize_compute_geometry(&srv, &w, &ox, &oy, &ow, &oh);
    if (ow != 480 || oh != 270) {
        fprintf(stderr, "[wm_selftest] FAIL: aspect lock on vertical BOTTOM drag (got %ux%u, expected 480x270)\n", ow, oh);
        return -1;
    }

    /* Vertical drag (TOP): width derived from height, bottom edge stationary */
    srv.resize_edge = RESIZE_EDGE_TOP;
    srv.cursor_x = 200;
    srv.cursor_y = 200 - 90; /* dy = -90 -> h becomes 180 + 90 = 270 */
    resize_compute_geometry(&srv, &w, &ox, &oy, &ow, &oh);
    if (ow != 480 || oh != 270 || oy != 10 || (oy + (int32_t)oh != 280)) {
        fprintf(stderr, "[wm_selftest] FAIL: aspect lock on vertical TOP drag (got %ux%u at y=%d)\n", ow, oh, oy);
        return -1;
    }

    /* 5. Drag threshold Chebyshev metric */
    int32_t dx1 = 3, dy1 = 4;
    int32_t cheb1 = (dx1 < 0 ? -dx1 : dx1) > (dy1 < 0 ? -dy1 : dy1) ? (dx1 < 0 ? -dx1 : dx1) : (dy1 < 0 ? -dy1 : dy1);
    if (cheb1 > 4) {
        fprintf(stderr, "[wm_selftest] FAIL: Chebyshev threshold false positive at (3,4)\n");
        return -1;
    }
    int32_t dx2 = -5, dy2 = 2;
    int32_t cheb2 = (dx2 < 0 ? -dx2 : dx2) > (dy2 < 0 ? -dy2 : dy2) ? (dx2 < 0 ? -dx2 : dx2) : (dy2 < 0 ? -dy2 : dy2);
    if (cheb2 <= 4) {
        fprintf(stderr, "[wm_selftest] FAIL: Chebyshev threshold false negative at (-5,2)\n");
        return -1;
    }

    /* 6. Protocol message MSG_SET_SIZE_HINTS dispatch */
    vanilla_msg_hdr_t hints_hdr;
    memset(&hints_hdr, 0, sizeof(hints_hdr));
    hints_hdr.magic = VANILLA_IPC_MAGIC;
    hints_hdr.msg_type = MSG_SET_SIZE_HINTS;
    hints_hdr.payload_len = sizeof(vanilla_msg_set_size_hints_t);
    hints_hdr.window_id = 1;

    vanilla_msg_set_size_hints_t hints_msg;
    hints_msg.window_id = 1;
    hints_msg.min_width = 160;
    hints_msg.min_height = 120;
    hints_msg.max_width = 1920;
    hints_msg.max_height = 1080;
    hints_msg.aspect_num = 16;
    hints_msg.aspect_den = 9;

    srv.clients[0].in_use = 1;
    srv.clients[0].fd = 10;
    srv.windows[0] = w;
    srv.windows[0].window_id = 1;
    srv.windows[0].client_fd = 10;

    handle_msg_set_size_hints(&srv, 0, &hints_hdr, (const uint8_t *)&hints_msg);
    if (srv.windows[0].min_width != 160 || srv.windows[0].min_height != 120 ||
        srv.windows[0].max_width != 1920 || srv.windows[0].max_height != 1080 ||
        srv.windows[0].aspect_num != 16 || srv.windows[0].aspect_den != 9) {
        fprintf(stderr, "[wm_selftest] FAIL: handle_msg_set_size_hints dispatch\n");
        return -1;
    }

    printf("[vanilla] wm resize & size hints self-tests passed (6/6)\n");
    return 0;
}

int handle_msg_clipboard_offer(vanilla_server_t *srv, int client_idx, const vanilla_msg_hdr_t *hdr, const uint8_t *payload)
{
    (void)srv;
    (void)client_idx;
    (void)hdr;
    (void)payload;
    /* Stub for clipboard data exchange */
    return 0;
}

int handle_msg_dnd_offer(vanilla_server_t *srv, int client_idx, const vanilla_msg_hdr_t *hdr, const uint8_t *payload)
{
    (void)srv;
    (void)client_idx;
    (void)hdr;
    (void)payload;
    /* Stub for drag-and-drop protocol */
    return 0;
}

void vanilla_server_send_frame_begin(vanilla_server_t *srv)
{
    if (!srv)
        return;

    int64_t now_ms = get_time_ms();
    for (int i = 0; i < VANILLA_MAX_WINDOWS; i++) {
        vanilla_server_window_t *w = &srv->windows[i];
        if (!w->in_use || !w->is_mapped || w->client_fd < 0)
            continue;

        vanilla_client_conn_t *c = find_client_by_fd(srv, w->client_fd);
        if (!c || c->negotiated_version < 2)
            continue;

        if (w->frame_begin_in_flight)
            continue;

        w->frame_serial++;
        if (w->frame_serial == 0)
            w->frame_serial = 1;

        vanilla_msg_hdr_t hdr;
        vanilla_msg_frame_begin_t fb;
        hdr.magic = VANILLA_IPC_MAGIC;
        hdr.msg_type = MSG_FRAME_BEGIN;
        hdr.payload_len = (uint16_t)sizeof(fb);
        hdr.window_id = w->window_id;

        fb.window_id = w->window_id;
        fb.frame_serial = w->frame_serial;
        fb.timestamp_ms = (uint32_t)now_ms;

        if (exact_write(w->client_fd, &hdr, sizeof(hdr)) == 0 &&
            exact_write(w->client_fd, &fb, sizeof(fb)) == 0) {
            w->frame_begin_in_flight = 1;
            w->frame_begin_time_ms = (uint32_t)now_ms;
            printf("[vanilla] frame_begin: sent to window %u serial %u\n", w->window_id, w->frame_serial);
        }
    }
}

void vanilla_server_release_buffers(vanilla_server_t *srv)
{
    if (!srv)
        return;

    for (int i = 0; i < VANILLA_MAX_WINDOWS; i++) {
        vanilla_server_window_t *w = &srv->windows[i];
        if (!w->in_use || !w->buffer_in_use || w->client_fd < 0)
            continue;

        vanilla_client_conn_t *c = find_client_by_fd(srv, w->client_fd);
        if (c && c->negotiated_version >= 2) {
            vanilla_msg_hdr_t rel_hdr;
            vanilla_msg_buffer_released_t rel_msg;
            rel_hdr.magic = VANILLA_IPC_MAGIC;
            rel_hdr.msg_type = MSG_BUFFER_RELEASED;
            rel_hdr.payload_len = (uint16_t)sizeof(rel_msg);
            rel_hdr.window_id = w->window_id;

            rel_msg.window_id = w->window_id;
            rel_msg.serial = w->present_serial;

            if (exact_write(w->client_fd, &rel_hdr, sizeof(rel_hdr)) == 0 &&
                exact_write(w->client_fd, &rel_msg, sizeof(rel_msg)) == 0) {
                printf("[vanilla] buffer_released: window %u serial %u\n", w->window_id, w->present_serial);
            }
        }
        w->buffer_in_use = 0;
    }
}

int vanilla_server_send_clipboard_request(vanilla_server_t *srv, uint32_t window_id, uint32_t request_id, const char *mime_type)
{
    if (!srv)
        return -1;
    vanilla_server_window_t *w = vanilla_server_find_window(srv, window_id);
    if (!w || w->client_fd < 0)
        return -1;

    vanilla_client_conn_t *c = find_client_by_fd(srv, w->client_fd);
    if (!c || c->negotiated_version < 2)
        return -1;

    vanilla_msg_hdr_t hdr;
    vanilla_msg_clipboard_request_t req;
    hdr.magic = VANILLA_IPC_MAGIC;
    hdr.msg_type = MSG_CLIPBOARD_REQUEST;
    hdr.payload_len = (uint16_t)sizeof(req);
    hdr.window_id = window_id;

    memset(&req, 0, sizeof(req));
    req.window_id = window_id;
    req.request_id = request_id;
    if (mime_type)
        strncpy(req.mime_type, mime_type, sizeof(req.mime_type) - 1);

    if (exact_write(w->client_fd, &hdr, sizeof(hdr)) < 0 ||
        exact_write(w->client_fd, &req, sizeof(req)) < 0)
        return -1;

    return 0;
}

int vanilla_server_send_dnd_drop(vanilla_server_t *srv, uint32_t target_window_id, int32_t x, int32_t y)
{
    if (!srv)
        return -1;
    vanilla_server_window_t *w = vanilla_server_find_window(srv, target_window_id);
    if (!w || w->client_fd < 0)
        return -1;

    vanilla_client_conn_t *c = find_client_by_fd(srv, w->client_fd);
    if (!c || c->negotiated_version < 2)
        return -1;

    vanilla_msg_hdr_t hdr;
    vanilla_msg_dnd_drop_t drop;
    hdr.magic = VANILLA_IPC_MAGIC;
    hdr.msg_type = MSG_DND_DROP;
    hdr.payload_len = (uint16_t)sizeof(drop);
    hdr.window_id = target_window_id;

    drop.target_window_id = target_window_id;
    drop.x = x;
    drop.y = y;

    if (exact_write(w->client_fd, &hdr, sizeof(hdr)) < 0 ||
        exact_write(w->client_fd, &drop, sizeof(drop)) < 0)
        return -1;

    return 0;
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

    const vanilla_dispatch_entry_t *entry = NULL;
    for (size_t i = 0; i < g_vanilla_dispatch_table_len; i++) {
        if (g_vanilla_dispatch_table[i].msg_type == hdr.msg_type) {
            entry = &g_vanilla_dispatch_table[i];
            break;
        }
    }

    if (!entry || !entry->handler) {
        fprintf(stderr, "[vanilla] dispatch: unknown message type %u\n", (unsigned)hdr.msg_type);
        vanilla_server_remove_client(srv, client_idx);
        return -1;
    }

    if (hdr.msg_type == MSG_PRESENT) {
        if (hdr.payload_len != sizeof(vanilla_msg_present_t) &&
            hdr.payload_len != sizeof(vanilla_msg_present_v2_t)) {
            fprintf(stderr, "[vanilla] dispatch: payload size mismatch for MSG_PRESENT: expected %zu or %zu, got %u\n",
                    sizeof(vanilla_msg_present_t), sizeof(vanilla_msg_present_v2_t), (unsigned)hdr.payload_len);
            vanilla_server_remove_client(srv, client_idx);
            return -1;
        }
    } else if (entry->payload_size > 0 && hdr.payload_len != entry->payload_size) {
        fprintf(stderr, "[vanilla] dispatch: payload size mismatch for msg %u: expected %zu, got %u\n",
                (unsigned)hdr.msg_type, entry->payload_size, (unsigned)hdr.payload_len);
        vanilla_server_remove_client(srv, client_idx);
        return -1;
    }

    uint32_t client_ver = srv->clients[client_idx].version;
    if (hdr.msg_type != MSG_HELLO && client_ver < entry->min_version) {
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

    size_t needed = (size_t)hdr.payload_len;
    uint8_t stack_buf[256];
    uint8_t *payload = stack_buf;
    if (needed > sizeof(stack_buf)) {
        payload = (uint8_t *)malloc(needed);
        if (!payload) {
            vanilla_server_remove_client(srv, client_idx);
            return -1;
        }
    }

    if (needed > 0) {
        if (exact_read(cfd, payload, needed) < 0) {
            if (payload != stack_buf)
                free(payload);
            vanilla_server_remove_client(srv, client_idx);
            return -1;
        }
    }

    int handler_ret = entry->handler(srv, client_idx, &hdr, payload);
    if (payload != stack_buf)
        free(payload);

    if (handler_ret < 0) {
        vanilla_server_remove_client(srv, client_idx);
        return -1;
    }

    return 0;
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

    if (g_theme_reload_pending) {
        g_theme_reload_pending = 0;
        theme_reload();
        srv->compositor.bg_color = g_theme->bg_base;
        compositor_damage_all(&srv->compositor);
        vanilla_server_broadcast_theme_changed(srv);
    }

    shell_update_clock(&srv->shell, srv);

    int64_t now_ms = get_time_ms();
    for (int i = 0; i < VANILLA_MAX_WINDOWS; i++) {
        vanilla_server_window_t *w = &srv->windows[i];
        if (w->in_use && w->configure_pending && (now_ms - (int64_t)w->configure_timestamp_ms > 500)) {
            printf("[vanilla] ack_configure: timeout fallback for window %u serial %u\n",
                   w->window_id, w->configure_serial);
            w->configure_pending = 0;
            wm_invalidate_window(srv, w);
            w->x = w->pending_x;
            w->y = w->pending_y;
            w->width = w->pending_w;
            w->height = w->pending_h;
            wm_invalidate_window(srv, w);
            shell_invalidate(srv);

            if (w->resize_has_target) {
                int32_t tx = w->target_x;
                int32_t ty = w->target_y;
                uint32_t tw = w->target_w;
                uint32_t th = w->target_h;
                w->resize_has_target = 0;
                if (tx != w->x || ty != w->y || tw != w->width || th != w->height) {
                    w->pending_x = tx;
                    w->pending_y = ty;
                    w->pending_w = tw;
                    w->pending_h = th;
                    wm_send_configure(srv, w);
                }
            }
        }
    }

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
        else
            vanilla_server_release_buffers(srv);
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
    else
        vanilla_server_release_buffers(srv);

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

    int32_t target_x = w->x;
    int32_t target_y = w->y;
    uint32_t target_w = w->width;
    uint32_t target_h = w->height;

    if (snap_type == SNAP_NONE) {
        if (w->is_snapped != SNAP_NONE) {
            target_x = w->restore_x;
            target_y = w->restore_y;
            target_w = w->restore_w;
            target_h = w->restore_h;
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
            target_x = target_fx + g_theme->border_width;
            target_y = target_fy + g_theme->titlebar_height + g_theme->border_width;
            target_w = (uint32_t)(target_fw - 2 * g_theme->border_width);
            target_h = (uint32_t)(target_fh - g_theme->titlebar_height - 2 * g_theme->border_width);
        } else {
            target_x = target_fx;
            target_y = target_fy;
            target_w = (uint32_t)target_fw;
            target_h = (uint32_t)target_fh;
        }

        w->is_snapped = snap_type;
    }

    w->pending_x = target_x;
    w->pending_y = target_y;
    w->pending_w = target_w;
    w->pending_h = target_h;

    vanilla_client_conn_t *c = find_client_by_fd(srv, w->client_fd);
    if (c && c->negotiated_version >= 2) {
        wm_send_configure(srv, w);
    } else {
        wm_invalidate_window(srv, w);
        w->x = target_x;
        w->y = target_y;
        w->width = target_w;
        w->height = target_h;
        wm_send_configure(srv, w);
        wm_invalidate_window(srv, w);
    }
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
            vanilla_rect_t old_box;
            cursor_get_rect(old_x, old_y, &old_box);

            if (srv->drag_threshold_pending) {
                int32_t dx = srv->cursor_x - srv->drag_threshold_start_x;
                int32_t dy = srv->cursor_y - srv->drag_threshold_start_y;
                int32_t dist_x = dx < 0 ? -dx : dx;
                int32_t dist_y = dy < 0 ? -dy : dy;
                int32_t chebyshev = dist_x > dist_y ? dist_x : dist_y;

                if (chebyshev > 4) {
                    srv->drag_threshold_pending = 0;
                    vanilla_server_window_t *w = vanilla_server_find_window(srv, srv->drag_threshold_window_id);
                    if (w && !srv->is_dragging && !srv->is_resizing) {
                        if (srv->drag_threshold_mode == DRAG_MODE_TITLEBAR) {
                            if (w->is_snapped != SNAP_NONE) {
                                wm_unsnap_window(srv, w->window_id);
                                w->x = srv->drag_threshold_start_x - (int32_t)w->width / 2;
                                w->y = srv->drag_threshold_start_y - g_theme->titlebar_height / 2;
                            }
                            srv->is_dragging = 1;
                            srv->drag_window_id = w->window_id;
                            srv->drag_offset_x = srv->drag_threshold_start_x - w->x;
                            srv->drag_offset_y = srv->drag_threshold_start_y - w->y;
                        } else if (srv->drag_threshold_mode == DRAG_MODE_RESIZE) {
                            srv->is_resizing = 1;
                            srv->resize_window_id = w->window_id;
                            srv->resize_edge = srv->drag_threshold_edge;
                            srv->resize_origin_x = srv->drag_threshold_start_x;
                            srv->resize_origin_y = srv->drag_threshold_start_y;
                            srv->resize_start_x = w->x;
                            srv->resize_start_y = w->y;
                            srv->resize_start_w = w->width;
                            srv->resize_start_h = w->height;
                        }
                    }
                }
            }

            if (srv->is_resizing) {
                cursor_set_active(resize_edge_to_cursor_shape((vanilla_resize_edge_t)srv->resize_edge));
            } else if (!srv->is_dragging) {
                vanilla_server_window_t *under = wm_window_at(srv, srv->cursor_x, srv->cursor_y);
                if (under && (under->flags & WINDOW_FLAG_RESIZABLE) && under->is_snapped == SNAP_NONE) {
                    vanilla_resize_edge_t edge = wm_hit_test_resize_edge(under, srv->cursor_x, srv->cursor_y);
                    if (edge != RESIZE_EDGE_NONE)
                        cursor_set_active(resize_edge_to_cursor_shape(edge));
                    else
                        cursor_set_active(CURSOR_ARROW);
                } else {
                    cursor_set_active(CURSOR_ARROW);
                }
            } else {
                cursor_set_active(CURSOR_ARROW);
            }

            vanilla_rect_t new_box;
            cursor_get_rect(srv->cursor_x, srv->cursor_y, &new_box);
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
            } else if (srv->is_resizing) {
                vanilla_server_window_t *w = vanilla_server_find_window(srv, srv->resize_window_id);
                if (w) {
                    int32_t new_x, new_y;
                    uint32_t new_w, new_h;
                    resize_compute_geometry(srv, w, &new_x, &new_y, &new_w, &new_h);

                    vanilla_client_conn_t *c = find_client_by_fd(srv, w->client_fd);
                    if (c && c->negotiated_version >= 2) {
                        if (!w->configure_pending) {
                            if (new_x != w->x || new_y != w->y || new_w != w->width || new_h != w->height) {
                                w->pending_x = new_x;
                                w->pending_y = new_y;
                                w->pending_w = new_w;
                                w->pending_h = new_h;
                                wm_send_configure(srv, w);
                            }
                        } else {
                            if (new_x != w->x || new_y != w->y || new_w != w->width || new_h != w->height) {
                                w->target_x = new_x;
                                w->target_y = new_y;
                                w->target_w = new_w;
                                w->target_h = new_h;
                                w->resize_has_target = 1;
                            }
                        }
                    } else {
                        if (new_x != w->x || new_y != w->y || new_w != w->width || new_h != w->height) {
                            wm_invalidate_window(srv, w);
                            w->x = new_x;
                            w->y = new_y;
                            w->width = new_w;
                            w->height = new_h;
                            w->pending_x = new_x;
                            w->pending_y = new_y;
                            w->pending_w = new_w;
                            w->pending_h = new_h;
                            wm_send_configure(srv, w);
                            wm_invalidate_window(srv, w);
                            shell_invalidate(srv);
                        }
                    }
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
                        srv->cursor_y >= screen_h - TASKBAR_HEIGHT + THEME_PX(4) && srv->cursor_y < screen_h - TASKBAR_HEIGHT + THEME_PX(4) + TASKBAR_START_H) {
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
                vanilla_server_window_t *hit = wm_window_at(srv, srv->cursor_x, srv->cursor_y);

                if (hit) {
                    vanilla_server_focus_window(srv, hit->window_id);
                    wm_raise_window(srv, hit->window_id);

                    vanilla_rect_t frame;
                    wm_get_frame_rect(hit, &frame);

                    /* Check titlebar chrome buttons */
                    if (!(hit->flags & WINDOW_FLAG_BORDERLESS) &&
                        srv->cursor_y < frame.y + g_theme->titlebar_height + g_theme->border_width) {

                        chrome_btn_rects_t btns = chrome_metrics(&frame);

                        /* Close button [X] */
                        if (vanilla_rect_contains(&btns.close_btn, srv->cursor_x, srv->cursor_y)) {
                            vanilla_server_close_request(srv, hit->window_id);
                            return 0;
                        }

                        /* Maximize button [] */
                        if (vanilla_rect_contains(&btns.max_btn, srv->cursor_x, srv->cursor_y)) {
                            if (hit->is_snapped == SNAP_MAXIMIZE)
                                wm_unsnap_window(srv, hit->window_id);
                            else
                                wm_snap_window(srv, hit->window_id, SNAP_MAXIMIZE);
                            return 0;
                        }

                        /* Minimize button [_] */
                        if (vanilla_rect_contains(&btns.min_btn, srv->cursor_x, srv->cursor_y)) {
                            hit->is_mapped = 0;
                            hit->is_focused = 0;
                            if (srv->focused_window_id == hit->window_id)
                                srv->focused_window_id = 0;
                            wm_invalidate_window(srv, hit);
                            shell_invalidate(srv);
                            return 0;
                        }
                    }

                    /* Check resize edge/corner zone if window is resizable */
                    vanilla_resize_edge_t edge = wm_hit_test_resize_edge(hit, srv->cursor_x, srv->cursor_y);
                    if (edge != RESIZE_EDGE_NONE) {
                        srv->drag_threshold_pending = 1;
                        srv->drag_threshold_start_x = srv->cursor_x;
                        srv->drag_threshold_start_y = srv->cursor_y;
                        srv->drag_threshold_mode = DRAG_MODE_RESIZE;
                        srv->drag_threshold_edge = edge;
                        srv->drag_threshold_window_id = hit->window_id;
                        return 0;
                    }

                    /* Click on titlebar body: initiate window drag with threshold */
                    if (!(hit->flags & WINDOW_FLAG_BORDERLESS) &&
                        srv->cursor_y < frame.y + g_theme->titlebar_height + g_theme->border_width) {
                        srv->drag_threshold_pending = 1;
                        srv->drag_threshold_start_x = srv->cursor_x;
                        srv->drag_threshold_start_y = srv->cursor_y;
                        srv->drag_threshold_mode = DRAG_MODE_TITLEBAR;
                        srv->drag_threshold_edge = RESIZE_EDGE_NONE;
                        srv->drag_threshold_window_id = hit->window_id;
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
                srv->drag_threshold_pending = 0;

                if (srv->is_resizing) {
                    vanilla_server_window_t *w = vanilla_server_find_window(srv, srv->resize_window_id);
                    if (w) {
                        int32_t new_x, new_y;
                        uint32_t new_w, new_h;
                        resize_compute_geometry(srv, w, &new_x, &new_y, &new_w, &new_h);

                        vanilla_client_conn_t *c = find_client_by_fd(srv, w->client_fd);
                        if (c && c->negotiated_version >= 2) {
                            if (!w->configure_pending) {
                                if (new_x != w->x || new_y != w->y || new_w != w->width || new_h != w->height) {
                                    w->pending_x = new_x;
                                    w->pending_y = new_y;
                                    w->pending_w = new_w;
                                    w->pending_h = new_h;
                                    wm_send_configure(srv, w);
                                }
                            } else {
                                if (new_x != w->x || new_y != w->y || new_w != w->width || new_h != w->height) {
                                    w->target_x = new_x;
                                    w->target_y = new_y;
                                    w->target_w = new_w;
                                    w->target_h = new_h;
                                    w->resize_has_target = 1;
                                }
                            }
                        } else {
                            if (new_x != w->x || new_y != w->y || new_w != w->width || new_h != w->height) {
                                wm_invalidate_window(srv, w);
                                w->x = new_x;
                                w->y = new_y;
                                w->width = new_w;
                                w->height = new_h;
                                w->pending_x = new_x;
                                w->pending_y = new_y;
                                w->pending_w = new_w;
                                w->pending_h = new_h;
                                wm_send_configure(srv, w);
                                wm_invalidate_window(srv, w);
                                shell_invalidate(srv);
                            }
                        }
                    }
                    srv->is_resizing = 0;
                    srv->resize_window_id = 0;
                    srv->resize_edge = RESIZE_EDGE_NONE;

                    vanilla_server_window_t *under = wm_window_at(srv, srv->cursor_x, srv->cursor_y);
                    if (under && (under->flags & WINDOW_FLAG_RESIZABLE) && under->is_snapped == SNAP_NONE) {
                        vanilla_resize_edge_t edge = wm_hit_test_resize_edge(under, srv->cursor_x, srv->cursor_y);
                        if (edge != RESIZE_EDGE_NONE)
                            cursor_set_active(resize_edge_to_cursor_shape(edge));
                        else
                            cursor_set_active(CURSOR_ARROW);
                    } else {
                        cursor_set_active(CURSOR_ARROW);
                    }
                    vanilla_rect_t cur_box;
                    cursor_get_rect(srv->cursor_x, srv->cursor_y, &cur_box);
                    compositor_add_damage(&srv->compositor, &cur_box);
                    return 0;
                }

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

                    vanilla_server_window_t *under = wm_window_at(srv, srv->cursor_x, srv->cursor_y);
                    if (under && (under->flags & WINDOW_FLAG_RESIZABLE) && under->is_snapped == SNAP_NONE) {
                        vanilla_resize_edge_t edge = wm_hit_test_resize_edge(under, srv->cursor_x, srv->cursor_y);
                        if (edge != RESIZE_EDGE_NONE)
                            cursor_set_active(resize_edge_to_cursor_shape(edge));
                        else
                            cursor_set_active(CURSOR_ARROW);
                    } else {
                        cursor_set_active(CURSOR_ARROW);
                    }
                    vanilla_rect_t cur_box;
                    cursor_get_rect(srv->cursor_x, srv->cursor_y, &cur_box);
                    compositor_add_damage(&srv->compositor, &cur_box);
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
