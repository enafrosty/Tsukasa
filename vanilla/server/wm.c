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
#include "anim.h"

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
        if (!w->in_use || !w->is_mapped || w->anim_state == ANIM_CLOSING || w->anim_state == ANIM_MINIMIZING)
            continue;

        vanilla_rect_t frame;
        wm_get_frame_rect(w, &frame);

        if (x >= frame.x && x < frame.x + frame.w &&
            y >= frame.y && y < frame.y + frame.h) {
            if (!hit || w->layer > hit->layer || (w->layer == hit->layer && w->z_index > max_z)) {
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
}

static void update_mod_state(vanilla_server_t *srv, uint16_t bit, int toggle, int value)
{
    if (toggle) {
        if (value == 1)
            srv->mod_state ^= bit;
    } else {
        if (value)
            srv->mod_state |= bit;
        else
            srv->mod_state &= ~bit;
    }
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
    srv->mod_state = 0;
    srv->is_dragging = 0;
    srv->drag_window_id = 0;

    srv->context_menu_depth = 0;
    srv->context_menu_pool_count = 0;
    memset(srv->context_menu_stack, 0, sizeof(srv->context_menu_stack));
    memset(srv->context_menu_pool, 0, sizeof(srv->context_menu_pool));

    cursor_manager_init("assets/cursors");
    cursor_set_active(CURSOR_ARROW);

    /* Open kernel input stream with non-blocking fallback */
    srv->input_fd = open("/dev/input/events", O_RDONLY | O_NONBLOCK);

    compositor_damage_all(&srv->compositor);
    compositor_render_frame(srv);

    wm_run_resize_selftests();
    wm_run_input_selftests();
    wm_run_animation_selftests();
    wm_run_context_menu_selftests();

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

void vanilla_server_destroy_window_record(vanilla_server_t *srv, vanilla_server_window_t *w)
{
    if (!srv || !w || !w->in_use)
        return;

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
    w->anim_state = ANIM_IDLE;
    w->anim_destroy_on_done = 0;
    w->anim_unmap_on_done = 0;
    w->is_mapped = 0;

    for (int i = 0; i < VANILLA_MAX_CLIENTS; i++) {
        if (!srv->clients[i].in_use)
            continue;
        int other_fd = srv->clients[i].fd;
        vanilla_msg_hdr_t b_hdr;
        vanilla_msg_window_configure_v2_t b_cfg;
        b_hdr.magic = VANILLA_IPC_MAGIC;
        b_hdr.msg_type = MSG_WINDOW_CONFIGURE;
        b_hdr.payload_len = (uint16_t)sizeof(b_cfg);
        b_hdr.window_id = w->window_id;

        b_cfg.x = w->x;
        b_cfg.y = w->y;
        b_cfg.width = 0;
        b_cfg.height = 0;
        b_cfg.serial = 0;
        b_cfg.flags = w->flags;

        exact_write(other_fd, &b_hdr, sizeof(b_hdr));
        exact_write(other_fd, &b_cfg, sizeof(b_cfg));
    }

    wm_invalidate_window(srv, w);
    surface_destroy(&w->surface);
    w->in_use = 0;
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
            vanilla_server_destroy_window_record(srv, &srv->windows[i]);
        }
    }

    if (cfd >= 0)
        close(cfd);

    srv->clients[client_idx].in_use = 0;
    srv->clients[client_idx].fd = -1;
    srv->clients[client_idx].version = 0;
    srv->clients[client_idx].negotiated_version = 0;
}

vanilla_server_window_t *vanilla_server_find_window(vanilla_server_t *srv, uint32_t window_id)
{
    if (!srv || window_id == 0)
        return NULL;

    for (int i = 0; i < VANILLA_MAX_WINDOWS; i++) {
        if (srv->windows[i].in_use && srv->windows[i].anim_state != ANIM_CLOSING && srv->windows[i].window_id == window_id)
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

    for (int i = 0; i < VANILLA_MAX_CLIENTS; i++) {
        if (!srv->clients[i].in_use || srv->clients[i].fd == w->client_fd)
            continue;
        int other_fd = srv->clients[i].fd;
        vanilla_msg_hdr_t b_hdr;
        vanilla_msg_window_configure_v2_t b_cfg;
        b_hdr.magic = VANILLA_IPC_MAGIC;
        b_hdr.msg_type = MSG_WINDOW_CONFIGURE;
        b_hdr.payload_len = (uint16_t)sizeof(b_cfg);
        b_hdr.window_id = w->window_id;

        b_cfg.x = w->pending_x ? w->pending_x : w->x;
        b_cfg.y = w->pending_y ? w->pending_y : w->y;
        b_cfg.width = w->pending_w ? w->pending_w : w->width;
        b_cfg.height = w->pending_h ? w->pending_h : w->height;
        b_cfg.serial = 0;
        b_cfg.flags = w->flags;

        exact_write(other_fd, &b_hdr, sizeof(b_hdr));
        exact_write(other_fd, &b_cfg, sizeof(b_cfg));
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

    w->has_keyboard_focus = 0;
    w->anim_state = ANIM_IDLE;
    w->anim_start_ticks = 0;
    w->anim_dur_ticks = 0;
    w->anim_scale_start = 1.0f;
    w->anim_scale_end = 1.0f;
    w->anim_alpha_start = 1.0f;
    w->anim_alpha_end = 1.0f;
    w->anim_origin_x = req->x + (int32_t)req->width / 2;
    w->anim_origin_y = req->y + (int32_t)req->height / 2;
    w->anim_destroy_on_done = 0;
    w->anim_unmap_on_done = 0;
    w->anim_current_scale = 1.0f;
    w->anim_current_alpha = 1.0f;

    if ((req->flags & WINDOW_FLAG_POPUP) || (req->flags & WINDOW_FLAG_ALWAYS_TOP))
        w->layer = LAYER_OVERLAY;
    else if (req->flags & WINDOW_FLAG_MODAL)
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
    if (req->flags & WINDOW_FLAG_POPUP)
        vanilla_server_focus_window(srv, w->window_id);
    wm_send_configure(srv, w);
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

        if (g_theme->reduce_motion != 0 || !w->is_mapped) {
            vanilla_server_destroy_window_record(srv, w);
        } else {
            vanilla_rect_t fr;
            wm_get_frame_rect(w, &fr);
            int32_t cx = fr.x + fr.w / 2;
            int32_t cy = fr.y + fr.h / 2;
            compositor_start_anim(w, ANIM_CLOSING, 100, 1.0f, 0.85f, 1.0f, 0.0f, cx, cy, 1);
            wm_invalidate_window(srv, w);
        }
    }
    return 0;
}

int handle_msg_map_window(vanilla_server_t *srv, int client_idx, const vanilla_msg_hdr_t *hdr, const uint8_t *payload)
{
    (void)client_idx;
    (void)hdr;
    const vanilla_msg_map_window_t *req = (const vanilla_msg_map_window_t *)payload;
    vanilla_server_window_t *w = vanilla_server_find_window(srv, req->window_id);
    if (w) {
        w->is_mapped = 1;
        w->frame_begin_in_flight = 0;
        vanilla_server_focus_window(srv, w->window_id);
        wm_raise_window(srv, w->window_id);

        if (g_theme->reduce_motion == 0) {
            vanilla_rect_t fr;
            wm_get_frame_rect(w, &fr);
            int32_t cx = fr.x + fr.w / 2;
            int32_t cy = fr.y + fr.h / 2;
            compositor_start_anim(w, ANIM_OPENING, 120, 0.85f, 1.0f, 0.0f, 1.0f, cx, cy, 0);
        }

        wm_invalidate_window(srv, w);
        wm_send_configure(srv, w);
    }
    return 0;
}

int handle_msg_unmap_window(vanilla_server_t *srv, int client_idx, const vanilla_msg_hdr_t *hdr, const uint8_t *payload)
{
    (void)client_idx;
    (void)hdr;
    const vanilla_msg_unmap_window_t *req = (const vanilla_msg_unmap_window_t *)payload;
    vanilla_server_window_t *w = vanilla_server_find_window(srv, req->window_id);
    if (w) {
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
        w->anim_state = ANIM_IDLE;
        w->anim_destroy_on_done = 0;
        w->anim_unmap_on_done = 0;
        w->anim_current_scale = 1.0f;
        w->anim_current_alpha = 1.0f;
        wm_invalidate_window(srv, w);
        w->is_mapped = 0;
        w->is_focused = 0;
        w->has_keyboard_focus = 0;
        if (srv->focused_window_id == w->window_id)
            srv->focused_window_id = 0;
        wm_send_configure(srv, w);
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

static void wm_alttab_build_list(vanilla_server_t *srv);

int wm_run_input_selftests(void)
{
    /* 1. Modifier bitmask state machine */
    vanilla_server_t srv;
    memset(&srv, 0, sizeof(srv));

    update_mod_state(&srv, MOD_LSHIFT, 0, 1);
    if (!(srv.mod_state & MOD_LSHIFT) || !(srv.mod_state & MOD_SHIFT)) {
        fprintf(stderr, "[input_selftest] FAIL: MOD_LSHIFT press\n");
        return -1;
    }
    update_mod_state(&srv, MOD_RSHIFT, 0, 1);
    if ((srv.mod_state & MOD_SHIFT) != (MOD_LSHIFT | MOD_RSHIFT)) {
        fprintf(stderr, "[input_selftest] FAIL: MOD_RSHIFT press\n");
        return -1;
    }
    update_mod_state(&srv, MOD_LSHIFT, 0, 0);
    if ((srv.mod_state & MOD_SHIFT) != MOD_RSHIFT) {
        fprintf(stderr, "[input_selftest] FAIL: MOD_LSHIFT release\n");
        return -1;
    }
    update_mod_state(&srv, MOD_RSHIFT, 0, 0);
    if (srv.mod_state & MOD_SHIFT) {
        fprintf(stderr, "[input_selftest] FAIL: MOD_RSHIFT release\n");
        return -1;
    }

    /* Alt tracking */
    update_mod_state(&srv, MOD_LALT, 0, 1);
    if (!(srv.mod_state & MOD_LALT) || !(srv.mod_state & MOD_ALT)) {
        fprintf(stderr, "[input_selftest] FAIL: MOD_LALT press\n");
        return -1;
    }
    update_mod_state(&srv, MOD_RALT, 0, 1);
    if ((srv.mod_state & MOD_ALT) != (MOD_LALT | MOD_RALT)) {
        fprintf(stderr, "[input_selftest] FAIL: MOD_RALT press\n");
        return -1;
    }
    update_mod_state(&srv, MOD_LALT, 0, 0);
    update_mod_state(&srv, MOD_RALT, 0, 0);
    if (srv.mod_state & MOD_ALT) {
        fprintf(stderr, "[input_selftest] FAIL: MOD_ALT release\n");
        return -1;
    }

    /* Ctrl tracking */
    update_mod_state(&srv, MOD_LCTRL, 0, 1);
    update_mod_state(&srv, MOD_RCTRL, 0, 1);
    if ((srv.mod_state & MOD_CTRL) != (MOD_LCTRL | MOD_RCTRL)) {
        fprintf(stderr, "[input_selftest] FAIL: MOD_CTRL press\n");
        return -1;
    }
    update_mod_state(&srv, MOD_LCTRL, 0, 0);
    update_mod_state(&srv, MOD_RCTRL, 0, 0);
    if (srv.mod_state & MOD_CTRL) {
        fprintf(stderr, "[input_selftest] FAIL: MOD_CTRL release\n");
        return -1;
    }

    /* CapsLock and NumLock toggle tracking */
    update_mod_state(&srv, MOD_CAPS_LOCK, 1, 1);
    if (!(srv.mod_state & MOD_CAPS_LOCK)) {
        fprintf(stderr, "[input_selftest] FAIL: MOD_CAPS_LOCK toggle ON\n");
        return -1;
    }
    update_mod_state(&srv, MOD_CAPS_LOCK, 1, 0);
    if (!(srv.mod_state & MOD_CAPS_LOCK)) {
        fprintf(stderr, "[input_selftest] FAIL: MOD_CAPS_LOCK state on release\n");
        return -1;
    }
    update_mod_state(&srv, MOD_CAPS_LOCK, 1, 1);
    if (srv.mod_state & MOD_CAPS_LOCK) {
        fprintf(stderr, "[input_selftest] FAIL: MOD_CAPS_LOCK toggle OFF\n");
        return -1;
    }

    update_mod_state(&srv, MOD_NUM_LOCK, 1, 1);
    if (!(srv.mod_state & MOD_NUM_LOCK)) {
        fprintf(stderr, "[input_selftest] FAIL: MOD_NUM_LOCK toggle ON\n");
        return -1;
    }
    update_mod_state(&srv, MOD_NUM_LOCK, 1, 1);
    if (srv.mod_state & MOD_NUM_LOCK) {
        fprintf(stderr, "[input_selftest] FAIL: MOD_NUM_LOCK toggle OFF\n");
        return -1;
    }

    /* Super/Meta */
    update_mod_state(&srv, MOD_SUPER, 0, 1);
    if (!(srv.mod_state & MOD_SUPER)) {
        fprintf(stderr, "[input_selftest] FAIL: MOD_SUPER press\n");
        return -1;
    }
    update_mod_state(&srv, MOD_SUPER, 0, 0);
    if (srv.mod_state & MOD_SUPER) {
        fprintf(stderr, "[input_selftest] FAIL: MOD_SUPER release\n");
        return -1;
    }

    /* 2. Alt+Tab MRU window list builder and sorting */
    memset(&srv, 0, sizeof(srv));
    srv.compositor.width = 1024;
    srv.compositor.height = 768;

    srv.windows[0].in_use = 1;
    srv.windows[0].is_mapped = 1;
    srv.windows[0].window_id = 10;
    srv.windows[0].z_index = 5;
    srv.windows[0].flags = WINDOW_FLAG_NONE;

    srv.windows[1].in_use = 1;
    srv.windows[1].is_mapped = 1;
    srv.windows[1].window_id = 20;
    srv.windows[1].z_index = 15;
    srv.windows[1].flags = WINDOW_FLAG_NONE;

    srv.windows[2].in_use = 1;
    srv.windows[2].is_mapped = 1;
    srv.windows[2].window_id = 30;
    srv.windows[2].z_index = 10;
    srv.windows[2].flags = WINDOW_FLAG_NONE;

    srv.windows[3].in_use = 1;
    srv.windows[3].is_mapped = 1;
    srv.windows[3].window_id = 40;
    srv.windows[3].z_index = 25;
    srv.windows[3].flags = WINDOW_FLAG_POPUP;

    srv.windows[4].in_use = 1;
    srv.windows[4].is_mapped = 1;
    srv.windows[4].window_id = 50;
    srv.windows[4].z_index = 30;
    srv.windows[4].flags = WINDOW_FLAG_ALWAYS_TOP;

    wm_alttab_build_list(&srv);
    if (srv.alttab_window_count != 3) {
        fprintf(stderr, "[input_selftest] FAIL: alttab window count (got %d, expected 3)\n", srv.alttab_window_count);
        return -1;
    }
    if (srv.alttab_window_ids[0] != 20 || srv.alttab_window_ids[1] != 30 || srv.alttab_window_ids[2] != 10) {
        fprintf(stderr, "[input_selftest] FAIL: alttab z-order sorting: [%u, %u, %u]\n",
                srv.alttab_window_ids[0], srv.alttab_window_ids[1], srv.alttab_window_ids[2]);
        return -1;
    }

    /* 3. Alt+Tab overlay geometry centering */
    vanilla_rect_t overlay;
    alttab_get_rect(&srv, &overlay);
    if (overlay.w != THEME_PX(360)) {
        fprintf(stderr, "[input_selftest] FAIL: alttab overlay width\n");
        return -1;
    }
    if (overlay.x != (1024 - THEME_PX(360)) / 2) {
        fprintf(stderr, "[input_selftest] FAIL: alttab overlay horizontal centering\n");
        return -1;
    }

    /* 4. Tab focus cycling without Alt (sequential z-order traversal) */
    for (int i = 0; i < 3; i++) {
        srv.windows[i].client_fd = -1;
        srv.windows[i].layer = LAYER_NORMAL;
    }
    srv.focused_window_id = 20;
    vanilla_server_window_t *w20 = vanilla_server_find_window(&srv, 20);
    if (w20) w20->is_focused = 1;

    struct input_event tab_ev;
    memset(&tab_ev, 0, sizeof(tab_ev));
    tab_ev.type = EV_KEY;
    tab_ev.code = KEY_TAB;
    tab_ev.value = 1;

    /* First Tab: moves 20 -> 30 */
    wm_handle_input_event(&srv, &tab_ev);
    if (srv.focused_window_id != 30) {
        fprintf(stderr, "[input_selftest] FAIL: Tab cycle 20->30 (got %u)\n", srv.focused_window_id);
        return -1;
    }
    vanilla_server_window_t *w30 = vanilla_server_find_window(&srv, 30);
    if (!w30 || !w30->has_keyboard_focus) {
        fprintf(stderr, "[input_selftest] FAIL: window 30 has_keyboard_focus\n");
        return -1;
    }

    /* Second Tab: moves 30 -> 10 */
    wm_handle_input_event(&srv, &tab_ev);
    if (srv.focused_window_id != 10) {
        fprintf(stderr, "[input_selftest] FAIL: Tab cycle 30->10 (got %u)\n", srv.focused_window_id);
        return -1;
    }

    /* Third Tab: wraps 10 -> 20 */
    wm_handle_input_event(&srv, &tab_ev);
    if (srv.focused_window_id != 20) {
        fprintf(stderr, "[input_selftest] FAIL: Tab cycle 10->20 wrap (got %u)\n", srv.focused_window_id);
        return -1;
    }

    /* Shift+Tab: reverses 20 -> 10 */
    srv.mod_state |= MOD_LSHIFT;
    wm_handle_input_event(&srv, &tab_ev);
    srv.mod_state &= ~MOD_LSHIFT;
    if (srv.focused_window_id != 10) {
        fprintf(stderr, "[input_selftest] FAIL: Shift+Tab reverse 20->10 (got %u)\n", srv.focused_window_id);
        return -1;
    }

    /* 5. Alt+Tab forward, reverse, and commit */
    struct input_event alt_down, alt_up;
    memset(&alt_down, 0, sizeof(alt_down));
    alt_down.type = EV_KEY;
    alt_down.code = KEY_LEFTALT;
    alt_down.value = 1;

    memset(&alt_up, 0, sizeof(alt_up));
    alt_up.type = EV_KEY;
    alt_up.code = KEY_LEFTALT;
    alt_up.value = 0;

    wm_handle_input_event(&srv, &alt_down);
    /* Tab while Alt held -> opens overlay, selection index 1 */
    wm_handle_input_event(&srv, &tab_ev);
    if (!srv.alttab_visible || srv.alttab_selection != 1) {
        fprintf(stderr, "[input_selftest] FAIL: Alt+Tab initial trigger (vis=%d sel=%d)\n",
                srv.alttab_visible, srv.alttab_selection);
        return -1;
    }
    /* Subsequent Tab -> index 2 */
    wm_handle_input_event(&srv, &tab_ev);
    if (srv.alttab_selection != 2) {
        fprintf(stderr, "[input_selftest] FAIL: Alt+Tab second Tab (sel=%d)\n", srv.alttab_selection);
        return -1;
    }
    /* Releasing Alt commits selection (index 2 -> window 10) */
    wm_handle_input_event(&srv, &alt_up);
    if (srv.alttab_visible || srv.focused_window_id != 10) {
        fprintf(stderr, "[input_selftest] FAIL: Alt release commit (vis=%d focus=%u)\n",
                srv.alttab_visible, srv.focused_window_id);
        return -1;
    }

    /* 6. Window hit testing layer priority */
    vanilla_server_window_t w_norm, w_over;
    memset(&w_norm, 0, sizeof(w_norm));
    memset(&w_over, 0, sizeof(w_over));
    w_norm.in_use = 1;
    w_norm.is_mapped = 1;
    w_norm.window_id = 1;
    w_norm.x = 100;
    w_norm.y = 100;
    w_norm.width = 200;
    w_norm.height = 200;
    w_norm.layer = LAYER_NORMAL;
    w_norm.z_index = 100;
    srv.windows[0] = w_norm;

    w_over.in_use = 1;
    w_over.is_mapped = 1;
    w_over.window_id = 2;
    w_over.x = 150;
    w_over.y = 150;
    w_over.width = 100;
    w_over.height = 100;
    w_over.layer = LAYER_OVERLAY;
    w_over.z_index = 1;
    srv.windows[1] = w_over;

    vanilla_server_window_t *h = wm_window_at(&srv, 160, 160);
    if (!h || h->window_id != 2) {
        fprintf(stderr, "[input_selftest] FAIL: LAYER_OVERLAY hit test priority\n");
        return -1;
    }

    printf("[vanilla] input completeness self-tests passed (6/6)\n");
    return 0;
}

static inline float anim_selftest_fabsf(float v)
{
    return v < 0.0f ? -v : v;
}

int wm_run_animation_selftests(void)
{
    /* 1. Easing functions: endpoints, clamping, midpoint formulas, monotonicity */
    if (anim_ease_out_cubic(-0.5f) != 0.0f || anim_ease_out_cubic(0.0f) != 0.0f) {
        fprintf(stderr, "[anim_selftest] FAIL: ease_out_cubic lower boundary\n");
        return -1;
    }
    if (anim_ease_out_cubic(1.5f) != 1.0f || anim_ease_out_cubic(1.0f) != 1.0f) {
        fprintf(stderr, "[anim_selftest] FAIL: ease_out_cubic upper boundary\n");
        return -1;
    }
    if (anim_selftest_fabsf(anim_ease_out_cubic(0.5f) - 0.875f) > 0.001f) {
        fprintf(stderr, "[anim_selftest] FAIL: ease_out_cubic midpoint\n");
        return -1;
    }

    if (anim_ease_out_quad(-0.5f) != 0.0f || anim_ease_out_quad(0.0f) != 0.0f) {
        fprintf(stderr, "[anim_selftest] FAIL: ease_out_quad lower boundary\n");
        return -1;
    }
    if (anim_ease_out_quad(1.5f) != 1.0f || anim_ease_out_quad(1.0f) != 1.0f) {
        fprintf(stderr, "[anim_selftest] FAIL: ease_out_quad upper boundary\n");
        return -1;
    }
    if (anim_selftest_fabsf(anim_ease_out_quad(0.5f) - 0.75f) > 0.001f) {
        fprintf(stderr, "[anim_selftest] FAIL: ease_out_quad midpoint\n");
        return -1;
    }

    if (anim_ease_in_out_quad(-0.5f) != 0.0f || anim_ease_in_out_quad(0.0f) != 0.0f) {
        fprintf(stderr, "[anim_selftest] FAIL: ease_in_out_quad lower boundary\n");
        return -1;
    }
    if (anim_ease_in_out_quad(1.5f) != 1.0f || anim_ease_in_out_quad(1.0f) != 1.0f) {
        fprintf(stderr, "[anim_selftest] FAIL: ease_in_out_quad upper boundary\n");
        return -1;
    }
    if (anim_selftest_fabsf(anim_ease_in_out_quad(0.5f) - 0.5f) > 0.001f) {
        fprintf(stderr, "[anim_selftest] FAIL: ease_in_out_quad midpoint\n");
        return -1;
    }

    if (anim_selftest_fabsf(anim_lerp(10.0f, 20.0f, 0.0f) - 10.0f) > 0.001f ||
        anim_selftest_fabsf(anim_lerp(10.0f, 20.0f, 0.5f) - 15.0f) > 0.001f ||
        anim_selftest_fabsf(anim_lerp(10.0f, 20.0f, 1.0f) - 20.0f) > 0.001f) {
        fprintf(stderr, "[anim_selftest] FAIL: anim_lerp\n");
        return -1;
    }

    float prev_cubic = -1.0f;
    float prev_quad = -1.0f;
    for (int i = 0; i <= 10; i++) {
        float t = (float)i / 10.0f;
        float c = anim_ease_out_cubic(t);
        float q = anim_ease_out_quad(t);
        if (c < prev_cubic || q < prev_quad) {
            fprintf(stderr, "[anim_selftest] FAIL: easing monotonicity violation\n");
            return -1;
        }
        prev_cubic = c;
        prev_quad = q;
    }

    /* 2. compositor_start_anim parameter initialization and active animations flag */
    vanilla_server_t srv;
    memset(&srv, 0, sizeof(srv));
    srv.compositor.width = 1024;
    srv.compositor.height = 768;

    vanilla_server_window_t *w0 = &srv.windows[0];
    w0->in_use = 1;
    w0->is_mapped = 1;
    w0->window_id = 100;
    w0->x = 100;
    w0->y = 100;
    w0->width = 400;
    w0->height = 300;

    compositor_start_anim(w0, ANIM_OPENING, 120, 0.85f, 1.0f, 0.0f, 1.0f, 300, 250, 0);
    if (w0->anim_state != ANIM_OPENING ||
        w0->anim_scale_start != 0.85f || w0->anim_scale_end != 1.0f ||
        w0->anim_alpha_start != 0.0f || w0->anim_alpha_end != 1.0f ||
        w0->anim_current_scale != 0.85f || w0->anim_current_alpha != 0.0f ||
        w0->anim_origin_x != 300 || w0->anim_origin_y != 250 ||
        w0->anim_dur_ticks == 0) {
        fprintf(stderr, "[anim_selftest] FAIL: compositor_start_anim parameter initialization\n");
        return -1;
    }

    if (!compositor_has_active_animations(&srv)) {
        fprintf(stderr, "[anim_selftest] FAIL: compositor_has_active_animations for opening window\n");
        return -1;
    }

    /* 3. compositor_animate_windows step update and completion to ANIM_IDLE */
    uint64_t now = pit_ticks();
    int retries = 0;
    while (now < 2 && retries++ < 50) {
        usleep(10000);
        now = pit_ticks();
    }

    uint32_t half = (uint32_t)(now / 2);
    if (half == 0)
        half = 1;
    w0->anim_start_ticks = now - half;
    w0->anim_dur_ticks = half * 2;
    compositor_animate_windows(&srv);

    if (w0->anim_state != ANIM_OPENING ||
        w0->anim_current_scale <= 0.85f || w0->anim_current_scale >= 1.0f ||
        w0->anim_current_alpha <= 0.0f || w0->anim_current_alpha >= 1.0f) {
        fprintf(stderr, "[anim_selftest] FAIL: animate_windows midpoint update\n");
        return -1;
    }

    now = pit_ticks();
    w0->anim_start_ticks = 0;
    w0->anim_dur_ticks = (now > 0) ? (uint32_t)now : 1;
    compositor_animate_windows(&srv);

    if (w0->anim_state != ANIM_IDLE ||
        w0->anim_current_scale != 1.0f || w0->anim_current_alpha != 1.0f) {
        fprintf(stderr, "[anim_selftest] FAIL: animate_windows completion to ANIM_IDLE\n");
        return -1;
    }

    /* 4. anim_destroy_on_done path (window close) */
    vanilla_server_window_t *w1 = &srv.windows[1];
    w1->in_use = 1;
    w1->is_mapped = 1;
    w1->window_id = 101;
    w1->z_index = 10;
    w1->x = 200;
    w1->y = 200;
    w1->width = 300;
    w1->height = 200;

    compositor_start_anim(w1, ANIM_CLOSING, 100, 1.0f, 0.85f, 1.0f, 0.0f, 350, 300, 1);
    if (vanilla_server_find_window(&srv, 101) != NULL) {
        fprintf(stderr, "[anim_selftest] FAIL: find_window did not filter ANIM_CLOSING window\n");
        return -1;
    }
    if (wm_window_at(&srv, 350, 300) == w1) {
        fprintf(stderr, "[anim_selftest] FAIL: wm_window_at did not filter ANIM_CLOSING window\n");
        return -1;
    }

    now = pit_ticks();
    w1->anim_start_ticks = 0;
    w1->anim_dur_ticks = (now > 0) ? (uint32_t)now : 1;
    compositor_animate_windows(&srv);

    if (w1->in_use != 0) {
        fprintf(stderr, "[anim_selftest] FAIL: anim_destroy_on_done did not free window record\n");
        return -1;
    }

    /* 5. anim_unmap_on_done path (minimize) */
    vanilla_server_window_t *w2 = &srv.windows[2];
    w2->in_use = 1;
    w2->is_mapped = 1;
    w2->is_focused = 1;
    w2->window_id = 102;
    w2->z_index = 20;
    w2->x = 50;
    w2->y = 50;
    w2->width = 200;
    w2->height = 150;

    compositor_start_anim(w2, ANIM_MINIMIZING, 150, 1.0f, 0.1f, 1.0f, 0.0f, 100, 700, 0);
    if (w2->anim_unmap_on_done != 1) {
        fprintf(stderr, "[anim_selftest] FAIL: anim_unmap_on_done not set for ANIM_MINIMIZING\n");
        return -1;
    }
    if (wm_window_at(&srv, 100, 100) == w2) {
        fprintf(stderr, "[anim_selftest] FAIL: wm_window_at did not filter ANIM_MINIMIZING window\n");
        return -1;
    }

    now = pit_ticks();
    w2->anim_start_ticks = 0;
    w2->anim_dur_ticks = (now > 0) ? (uint32_t)now : 1;
    compositor_animate_windows(&srv);

    if (w2->in_use == 0 || w2->is_mapped != 0 || w2->anim_state != ANIM_IDLE) {
        fprintf(stderr, "[anim_selftest] FAIL: anim_unmap_on_done completion state\n");
        return -1;
    }

    /* 6. Snap preview show, update, hide */
    vanilla_rect_t snap_zone = { 0, 0, 512, 768 };
    compositor_snap_preview_show(&srv.compositor, &snap_zone);
    if (!srv.compositor.snap_preview_visible ||
        srv.compositor.snap_preview_rect.w != 512 || srv.compositor.snap_preview_rect.h != 768) {
        fprintf(stderr, "[anim_selftest] FAIL: compositor_snap_preview_show\n");
        return -1;
    }
    if (!compositor_has_active_animations(&srv)) {
        fprintf(stderr, "[anim_selftest] FAIL: compositor_has_active_animations for snap preview\n");
        return -1;
    }

    compositor_snap_preview_hide(&srv.compositor);
    if (srv.compositor.snap_preview_visible) {
        fprintf(stderr, "[anim_selftest] FAIL: compositor_snap_preview_hide\n");
        return -1;
    }

    /* 7. reduce_motion bypass */
    vanilla_theme_t *m_theme = (vanilla_theme_t *)g_theme;
    int32_t saved_rm = m_theme->reduce_motion;
    m_theme->reduce_motion = 1;

    vanilla_server_window_t *w3 = &srv.windows[3];
    w3->in_use = 1;
    w3->is_mapped = 1;
    w3->window_id = 103;
    compositor_start_anim(w3, ANIM_OPENING, 120, 0.85f, 1.0f, 0.0f, 1.0f, 100, 100, 0);
    if (w3->anim_state != ANIM_IDLE || w3->anim_current_scale != 1.0f || w3->anim_current_alpha != 1.0f) {
        m_theme->reduce_motion = saved_rm;
        fprintf(stderr, "[anim_selftest] FAIL: reduce_motion did not bypass window animation\n");
        return -1;
    }

    compositor_snap_preview_show(&srv.compositor, &snap_zone);
    if (srv.compositor.snap_preview_visible) {
        m_theme->reduce_motion = saved_rm;
        fprintf(stderr, "[anim_selftest] FAIL: reduce_motion did not suppress snap preview\n");
        return -1;
    }

    if (compositor_has_active_animations(&srv)) {
        m_theme->reduce_motion = saved_rm;
        fprintf(stderr, "[anim_selftest] FAIL: compositor_has_active_animations with reduce_motion=1\n");
        return -1;
    }

    m_theme->reduce_motion = saved_rm;

    printf("[vanilla] window animation & easing self-tests passed (7/7)\n");
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

static void menu_compute_size(vanilla_compositor_t *comp, vanilla_context_menu_t *m, const vanilla_context_menu_t *parent)
{
    int row_h = 24;
    int pad_x = 8;
    int min_w = 160;

    int content_w = min_w;
    for (int i = 0; i < m->item_count; i++) {
        int tw = (int)strlen(m->items[i].label) * 7 + pad_x * 2;
        if (m->items[i].submenu_id != 0)
            tw += 16;
        if (m->items[i].flags & MENU_ITEM_CHECKED)
            tw += 12;
        if (tw > content_w)
            content_w = tw;
    }

    m->width = content_w + pad_x * 2;
    m->height = m->item_count * row_h + 4;

    int screen_w = comp ? (int)comp->width : 800;
    int screen_h = comp ? (int)comp->height : 600;

    /* If a submenu would go off-screen to the right, open it to the left of the parent item */
    if (parent && (m->x + m->width > screen_w)) {
        int new_x = parent->x - m->width;
        if (new_x >= 0)
            m->x = new_x;
    } else if (m->parent_id != 0 && (m->x + m->width > screen_w)) {
        int new_x = m->x - m->width;
        if (new_x >= 0)
            m->x = new_x;
    }

    /* Clamp to screen bounds */
    if (m->x + m->width > screen_w)
        m->x = screen_w - m->width;
    if (m->y + m->height > screen_h)
        m->y = screen_h - m->height;
    if (m->x < 0)
        m->x = 0;
    if (m->y < 0)
        m->y = 0;
}

static void wm_context_menu_damage(vanilla_server_t *srv, const vanilla_context_menu_t *m)
{
    if (!srv || !m || !m->active)
        return;

    int32_t sr = (g_theme && g_theme->shadow_radius > 0) ? g_theme->shadow_radius : 16;
    vanilla_rect_t mr = {
        m->x - sr,
        m->y - sr,
        m->width + sr * 2,
        m->height + sr * 2
    };
    compositor_add_damage(&srv->compositor, &mr);
}

static int wm_context_menu_open_submenu(vanilla_server_t *srv, vanilla_context_menu_t *parent, int item_idx)
{
    if (!srv || !parent || srv->context_menu_depth >= CONTEXT_MENU_MAX_DEPTH)
        return -1;

    if (item_idx < 0 || item_idx >= parent->item_count)
        return -1;

    vanilla_menu_item_t *it = &parent->items[item_idx];
    if (it->submenu_id == 0 || !(it->flags & MENU_ITEM_ENABLED))
        return -1;

    /* If this child submenu is already open, do not reopen */
    if (parent->open_submenu_idx == item_idx && srv->context_menu_depth > 1)
        return 0;

    int parent_depth = (int)(parent - srv->context_menu_stack);
    while (srv->context_menu_depth > parent_depth + 1) {
        wm_context_menu_pop(srv);
    }

    /* Look for submenu matching it->submenu_id in pool */
    vanilla_context_menu_t *sub = NULL;
    for (int p = 0; p < srv->context_menu_pool_count; p++) {
        if (srv->context_menu_pool[p].menu_id == it->submenu_id) {
            sub = &srv->context_menu_pool[p];
            break;
        }
    }

    vanilla_context_menu_t *dest = &srv->context_menu_stack[srv->context_menu_depth];
    memset(dest, 0, sizeof(*dest));

    if (sub) {
        *dest = *sub;
    } else {
        dest->menu_id = it->submenu_id;
        dest->parent_id = parent->menu_id;
        dest->owning_window_id = parent->owning_window_id;
        dest->client_idx = parent->client_idx;
        dest->item_count = 2;
        dest->items[0].item_id = it->submenu_id * 10 + 1;
        dest->items[0].submenu_id = 0;
        dest->items[0].flags = MENU_ITEM_ENABLED;
        strncpy(dest->items[0].label, "Option 1", MENU_ITEM_LABEL_MAX - 1);
        dest->items[1].item_id = it->submenu_id * 10 + 2;
        dest->items[1].submenu_id = 0;
        dest->items[1].flags = MENU_ITEM_ENABLED;
        strncpy(dest->items[1].label, "Option 2", MENU_ITEM_LABEL_MAX - 1);
    }

    dest->active = 1;
    dest->highlighted = -1;
    dest->open_ticks = pit_ticks();
    dest->anim_alpha = (g_theme->reduce_motion != 0) ? 1.0f : 0.0f;
    dest->hover_timer_start = 0;
    dest->hover_item_idx = -1;
    dest->open_submenu_idx = -1;

    int row_h = 24;
    dest->x = parent->x + parent->width;
    dest->y = parent->y + 2 + item_idx * row_h;

    menu_compute_size(&srv->compositor, dest, parent);

    parent->open_submenu_idx = item_idx;
    srv->context_menu_depth++;

    wm_context_menu_damage(srv, dest);
    return 0;
}

void wm_context_menu_pop(vanilla_server_t *srv)
{
    if (!srv || srv->context_menu_depth <= 1)
        return;

    vanilla_context_menu_t *m = &srv->context_menu_stack[srv->context_menu_depth - 1];
    wm_context_menu_damage(srv, m);
    m->active = 0;
    m->hover_timer_start = 0;
    m->hover_item_idx = -1;
    m->open_submenu_idx = -1;
    srv->context_menu_depth--;

    vanilla_context_menu_t *new_top = &srv->context_menu_stack[srv->context_menu_depth - 1];
    new_top->open_submenu_idx = -1;
    wm_context_menu_damage(srv, new_top);
}

static void wm_spawn_app(const char *path)
{
    char current_path[256];
    strncpy(current_path, path, sizeof(current_path) - 1);
    current_path[sizeof(current_path) - 1] = '\0';

    char *argv[] = { current_path, NULL };
    pid_t pid = spawn(current_path, argv, NULL);

    if (pid <= 0 && strncmp(current_path, "/bin/", 5) == 0) {
        char fat_path[320];
        snprintf(fat_path, sizeof(fat_path), "/fat12/%s", current_path + 5);
        char *fargv[] = { fat_path, NULL };
        pid = spawn(fat_path, fargv, NULL);
    }
    (void)pid;
}

void wm_context_menu_close_stack(vanilla_server_t *srv, uint32_t result_item_id)
{
    if (!srv || srv->context_menu_depth <= 0)
        return;

    for (int d = 0; d < srv->context_menu_depth; d++) {
        vanilla_context_menu_t *m = &srv->context_menu_stack[d];
        if (m->active) {
            wm_context_menu_damage(srv, m);
            m->active = 0;
            m->hover_timer_start = 0;
            m->hover_item_idx = -1;
            m->open_submenu_idx = -1;
        }
    }

    vanilla_context_menu_t *root = &srv->context_menu_stack[0];

    int target_fd = -1;
    if (root->client_idx >= 0 && root->client_idx < VANILLA_MAX_CLIENTS &&
        srv->clients[root->client_idx].in_use && srv->clients[root->client_idx].fd >= 0) {
        target_fd = srv->clients[root->client_idx].fd;
    } else if (root->owning_window_id != 0) {
        vanilla_server_window_t *w = vanilla_server_find_window(srv, root->owning_window_id);
        if (w && w->client_fd >= 0)
            target_fd = w->client_fd;
    }

    if (target_fd >= 0) {
        vanilla_msg_hdr_t r_hdr;
        vanilla_msg_context_menu_result_t res;
        r_hdr.magic = VANILLA_IPC_MAGIC;
        r_hdr.msg_type = MSG_CONTEXT_MENU_RESULT;
        r_hdr.payload_len = (uint16_t)sizeof(res);
        r_hdr.window_id = root->owning_window_id;
        res.window_id = root->owning_window_id;
        res.menu_id = root->menu_id;
        res.item_id = result_item_id;
        exact_write(target_fd, &r_hdr, sizeof(r_hdr));
        exact_write(target_fd, &res, sizeof(res));
    } else if (root->client_idx == -1) {
        if (result_item_id != 0) {
            if (root->menu_id == SERVER_MENU_DESKTOP) {
                if (result_item_id == 1) {
                    srv->compositor.has_wallpaper = !srv->compositor.has_wallpaper;
                    compositor_damage_all(&srv->compositor);
                } else if (result_item_id == 2) {
                    wm_spawn_app("/bin/filemgr.elf");
                } else if (result_item_id == 3) {
                    wm_spawn_app("/bin/terminal.elf");
                }
            } else if (root->menu_id == SERVER_MENU_TASKBAR) {
                if (result_item_id == 1) {
                    if (root->owning_window_id != 0)
                        vanilla_server_close_request(srv, root->owning_window_id);
                } else if (result_item_id == 2) {
                    if (root->owning_window_id != 0) {
                        vanilla_server_window_t *w = vanilla_server_find_window(srv, root->owning_window_id);
                        if (w) {
                            w->is_mapped = 1;
                            vanilla_server_focus_window(srv, w->window_id);
                            wm_raise_window(srv, w->window_id);
                        }
                    }
                }
            }
        }
    }

    srv->context_menu_depth = 0;
    srv->context_menu_pool_count = 0;
}

int handle_msg_show_context_menu(vanilla_server_t *srv, int client_idx, const vanilla_msg_hdr_t *hdr, const uint8_t *payload)
{
    if (!srv || !hdr || !payload)
        return -1;

    if (hdr->payload_len < sizeof(vanilla_msg_show_context_menu_t)) {
        fprintf(stderr, "[vanilla] context menu: payload too short (%u bytes)\n", (unsigned)hdr->payload_len);
        return -1;
    }

    const vanilla_msg_show_context_menu_t *req = (const vanilla_msg_show_context_menu_t *)payload;
    size_t expected_len = sizeof(vanilla_msg_show_context_menu_t) + (size_t)req->item_count * sizeof(vanilla_menu_item_t);

    if (hdr->payload_len < expected_len) {
        fprintf(stderr, "[vanilla] context menu: payload size mismatch: expected >= %zu, got %u\n",
                expected_len, (unsigned)hdr->payload_len);
        vanilla_msg_hdr_t r_hdr;
        vanilla_msg_context_menu_result_t res;
        r_hdr.magic = VANILLA_IPC_MAGIC;
        r_hdr.msg_type = MSG_CONTEXT_MENU_RESULT;
        r_hdr.payload_len = (uint16_t)sizeof(res);
        r_hdr.window_id = req->window_id;
        res.window_id = req->window_id;
        res.menu_id = req->menu_id;
        res.item_id = 0;
        if (client_idx >= 0 && client_idx < VANILLA_MAX_CLIENTS && srv->clients[client_idx].in_use) {
            exact_write(srv->clients[client_idx].fd, &r_hdr, sizeof(r_hdr));
            exact_write(srv->clients[client_idx].fd, &res, sizeof(res));
        }
        return -1;
    }

    if (req->item_count == 0 || req->item_count > CONTEXT_MENU_MAX_ITEMS) {
        fprintf(stderr, "[vanilla] context menu: invalid item count %u (max %d)\n",
                (unsigned)req->item_count, CONTEXT_MENU_MAX_ITEMS);
        vanilla_msg_hdr_t r_hdr;
        vanilla_msg_context_menu_result_t res;
        r_hdr.magic = VANILLA_IPC_MAGIC;
        r_hdr.msg_type = MSG_CONTEXT_MENU_RESULT;
        r_hdr.payload_len = (uint16_t)sizeof(res);
        r_hdr.window_id = req->window_id;
        res.window_id = req->window_id;
        res.menu_id = req->menu_id;
        res.item_id = 0;
        if (client_idx >= 0 && client_idx < VANILLA_MAX_CLIENTS && srv->clients[client_idx].in_use) {
            exact_write(srv->clients[client_idx].fd, &r_hdr, sizeof(r_hdr));
            exact_write(srv->clients[client_idx].fd, &res, sizeof(res));
        }
        return -1;
    }

    const vanilla_menu_item_t *items = (const vanilla_menu_item_t *)(payload + sizeof(vanilla_msg_show_context_menu_t));

    if (req->parent_id == 0) {
        if (srv->context_menu_depth > 0)
            wm_context_menu_close_stack(srv, 0);
        srv->context_menu_pool_count = 0;

        vanilla_context_menu_t *m = &srv->context_menu_stack[0];
        memset(m, 0, sizeof(*m));
        m->active = 1;
        m->owning_window_id = req->window_id;
        m->client_idx = client_idx;
        m->menu_id = req->menu_id;
        m->parent_id = 0;
        m->x = req->x;
        m->y = req->y;
        m->item_count = (int)req->item_count;
        memcpy(m->items, items, (size_t)req->item_count * sizeof(vanilla_menu_item_t));
        m->highlighted = -1;
        m->open_ticks = pit_ticks();
        m->anim_alpha = (g_theme->reduce_motion != 0) ? 1.0f : 0.0f;
        m->hover_timer_start = 0;
        m->hover_item_idx = -1;
        m->open_submenu_idx = -1;

        menu_compute_size(&srv->compositor, m, NULL);

        srv->context_menu_depth = 1;

        srv->context_menu_pool[0] = *m;
        srv->context_menu_pool_count = 1;

        wm_context_menu_damage(srv, m);
    } else {
        int found = -1;
        for (int p = 0; p < srv->context_menu_pool_count; p++) {
            if (srv->context_menu_pool[p].menu_id == req->menu_id) {
                found = p;
                break;
            }
        }
        int p_idx = (found >= 0) ? found : (srv->context_menu_pool_count < CONTEXT_MENU_POOL_SIZE ? srv->context_menu_pool_count++ : -1);
        if (p_idx >= 0) {
            vanilla_context_menu_t *pm = &srv->context_menu_pool[p_idx];
            memset(pm, 0, sizeof(*pm));
            pm->active = 1;
            pm->owning_window_id = req->window_id;
            pm->client_idx = client_idx;
            pm->menu_id = req->menu_id;
            pm->parent_id = req->parent_id;
            pm->x = req->x;
            pm->y = req->y;
            pm->item_count = (int)req->item_count;
            memcpy(pm->items, items, (size_t)req->item_count * sizeof(vanilla_menu_item_t));
            pm->highlighted = -1;
            pm->open_ticks = pit_ticks();
            pm->anim_alpha = (g_theme->reduce_motion != 0) ? 1.0f : 0.0f;
            pm->hover_timer_start = 0;
            pm->hover_item_idx = -1;
            pm->open_submenu_idx = -1;
            menu_compute_size(&srv->compositor, pm, NULL);
        }
    }

    return 0;
}

int wm_context_menu_open_desktop(vanilla_server_t *srv, int32_t x, int32_t y)
{
    if (!srv)
        return -1;

    if (srv->context_menu_depth > 0)
        wm_context_menu_close_stack(srv, 0);

    srv->context_menu_pool_count = 0;

    vanilla_context_menu_t *m = &srv->context_menu_stack[0];
    memset(m, 0, sizeof(*m));
    m->active = 1;
    m->owning_window_id = 0;
    m->client_idx = -1;
    m->menu_id = SERVER_MENU_DESKTOP;
    m->parent_id = 0;
    m->x = x;
    m->y = y;
    m->item_count = 3;

    m->items[0].item_id = 1;
    m->items[0].submenu_id = 0;
    m->items[0].flags = MENU_ITEM_ENABLED;
    strncpy(m->items[0].label, "Change Wallpaper", MENU_ITEM_LABEL_MAX - 1);

    m->items[1].item_id = 2;
    m->items[1].submenu_id = 0;
    m->items[1].flags = MENU_ITEM_ENABLED;
    strncpy(m->items[1].label, "Open File Manager", MENU_ITEM_LABEL_MAX - 1);

    m->items[2].item_id = 3;
    m->items[2].submenu_id = 0;
    m->items[2].flags = MENU_ITEM_ENABLED;
    strncpy(m->items[2].label, "Terminal Here", MENU_ITEM_LABEL_MAX - 1);

    m->highlighted = -1;
    m->open_ticks = pit_ticks();
    m->anim_alpha = (g_theme->reduce_motion != 0) ? 1.0f : 0.0f;
    m->hover_timer_start = 0;
    m->hover_item_idx = -1;
    m->open_submenu_idx = -1;

    menu_compute_size(&srv->compositor, m, NULL);
    srv->context_menu_depth = 1;

    srv->context_menu_pool[0] = *m;
    srv->context_menu_pool_count = 1;

    wm_context_menu_damage(srv, m);
    return 0;
}

int wm_context_menu_open_taskbar(vanilla_server_t *srv, int32_t x, int32_t y, uint32_t window_id)
{
    if (!srv)
        return -1;

    if (srv->context_menu_depth > 0)
        wm_context_menu_close_stack(srv, 0);

    srv->context_menu_pool_count = 0;

    vanilla_context_menu_t *m = &srv->context_menu_stack[0];
    memset(m, 0, sizeof(*m));
    m->active = 1;
    m->owning_window_id = window_id;
    m->client_idx = -1;
    m->menu_id = SERVER_MENU_TASKBAR;
    m->parent_id = 0;
    m->x = x;
    m->y = y;
    m->item_count = 2;

    m->items[0].item_id = 1;
    m->items[0].submenu_id = 0;
    m->items[0].flags = MENU_ITEM_ENABLED;
    strncpy(m->items[0].label, "Close", MENU_ITEM_LABEL_MAX - 1);

    m->items[1].item_id = 2;
    m->items[1].submenu_id = 0;
    m->items[1].flags = MENU_ITEM_ENABLED;
    strncpy(m->items[1].label, "Move to Front", MENU_ITEM_LABEL_MAX - 1);

    m->highlighted = -1;
    m->open_ticks = pit_ticks();
    m->anim_alpha = (g_theme->reduce_motion != 0) ? 1.0f : 0.0f;
    m->hover_timer_start = 0;
    m->hover_item_idx = -1;
    m->open_submenu_idx = -1;

    menu_compute_size(&srv->compositor, m, NULL);
    srv->context_menu_depth = 1;

    srv->context_menu_pool[0] = *m;
    srv->context_menu_pool_count = 1;

    wm_context_menu_damage(srv, m);
    return 0;
}

int wm_context_menu_handle_click(vanilla_server_t *srv, int32_t x, int32_t y, uint32_t button)
{
    if (!srv || srv->context_menu_depth <= 0)
        return 0;

    if (button != BTN_LEFT)
        return 0;

    for (int d = srv->context_menu_depth - 1; d >= 0; d--) {
        vanilla_context_menu_t *m = &srv->context_menu_stack[d];
        if (!m->active)
            continue;

        if (x >= m->x && x < m->x + m->width &&
            y >= m->y && y < m->y + m->height) {

            while (srv->context_menu_depth > d + 1) {
                wm_context_menu_pop(srv);
            }

            int row_h = 24;
            int rel_y = y - (m->y + 2);
            int idx = (rel_y >= 0 && rel_y < m->height) ? (rel_y / row_h) : -1;

            if (idx >= 0 && idx < m->item_count) {
                vanilla_menu_item_t *it = &m->items[idx];
                if ((it->flags & MENU_ITEM_ENABLED) && !(it->flags & MENU_ITEM_SEPARATOR)) {
                    if (it->submenu_id != 0) {
                        wm_context_menu_open_submenu(srv, m, idx);
                        return 1;
                    } else {
                        wm_context_menu_close_stack(srv, it->item_id);
                        return 1;
                    }
                }
            }
            return 1;
        }
    }

    wm_context_menu_close_stack(srv, 0);
    return 1;
}

int wm_context_menu_handle_motion(vanilla_server_t *srv, int32_t x, int32_t y)
{
    if (!srv || srv->context_menu_depth <= 0)
        return 0;

    for (int d = srv->context_menu_depth - 1; d >= 0; d--) {
        vanilla_context_menu_t *m = &srv->context_menu_stack[d];
        if (!m->active)
            continue;

        if (x >= m->x && x < m->x + m->width &&
            y >= m->y && y < m->y + m->height) {

            int row_h = 24;
            int rel_y = y - (m->y + 2);
            int idx = (rel_y >= 0 && rel_y < m->height) ? (rel_y / row_h) : -1;

            if (idx >= 0 && idx < m->item_count) {
                if (m->open_submenu_idx >= 0 && idx != m->open_submenu_idx) {
                    while (srv->context_menu_depth > d + 1) {
                        wm_context_menu_pop(srv);
                    }
                }

                if (idx != m->highlighted) {
                    m->highlighted = idx;
                    wm_context_menu_damage(srv, m);
                }

                if (m->items[idx].submenu_id != 0 && (m->items[idx].flags & MENU_ITEM_ENABLED)) {
                    if (m->open_submenu_idx == idx) {
                        m->hover_item_idx = -1;
                        m->hover_timer_start = 0;
                    } else if (m->hover_item_idx != idx) {
                        m->hover_item_idx = idx;
                        m->hover_timer_start = pit_ticks();
                    }
                } else {
                    m->hover_item_idx = -1;
                    m->hover_timer_start = 0;
                }
            }
            return 1;
        }
    }

    vanilla_context_menu_t *top = &srv->context_menu_stack[srv->context_menu_depth - 1];
    if (top->hover_item_idx >= 0) {
        top->hover_item_idx = -1;
        top->hover_timer_start = 0;
    }

    return 0;
}

void wm_context_menu_check_timers(vanilla_server_t *srv)
{
    if (!srv || srv->context_menu_depth <= 0)
        return;

    vanilla_context_menu_t *m = &srv->context_menu_stack[srv->context_menu_depth - 1];
    if (m->hover_item_idx >= 0 && m->hover_timer_start > 0) {
        uint64_t now = pit_ticks();
        uint64_t freq = pit_frequency();
        uint64_t timeout_ticks = (freq * 300) / 1000;
        if (now >= m->hover_timer_start && (now - m->hover_timer_start) >= timeout_ticks) {
            int h_idx = m->hover_item_idx;
            m->hover_timer_start = 0;
            m->hover_item_idx = -1;
            if (h_idx < m->item_count && m->items[h_idx].submenu_id != 0 &&
                (m->items[h_idx].flags & MENU_ITEM_ENABLED)) {
                wm_context_menu_open_submenu(srv, m, h_idx);
            }
        }
    }
}

int wm_context_menu_handle_key(vanilla_server_t *srv, uint16_t code, int pressed)
{
    if (!srv || srv->context_menu_depth <= 0)
        return 0;

    if (!pressed)
        return 1;

    vanilla_context_menu_t *m = &srv->context_menu_stack[srv->context_menu_depth - 1];

    if (code == KEY_ESC) {
        if (srv->context_menu_depth > 1) {
            wm_context_menu_pop(srv);
        } else {
            wm_context_menu_close_stack(srv, 0);
        }
        return 1;
    }

    if (code == KEY_UP) {
        if (m->item_count <= 0)
            return 1;
        int start = (m->highlighted >= 0) ? m->highlighted : 0;
        int cur = start;
        for (int step = 0; step < m->item_count; step++) {
            cur = (cur - 1 + m->item_count) % m->item_count;
            if ((m->items[cur].flags & MENU_ITEM_ENABLED) && !(m->items[cur].flags & MENU_ITEM_SEPARATOR)) {
                m->highlighted = cur;
                m->hover_item_idx = -1;
                m->hover_timer_start = 0;
                wm_context_menu_damage(srv, m);
                break;
            }
        }
        return 1;
    }

    if (code == KEY_DOWN) {
        if (m->item_count <= 0)
            return 1;
        int start = (m->highlighted >= 0) ? m->highlighted : -1;
        int cur = start;
        for (int step = 0; step < m->item_count; step++) {
            cur = (cur + 1) % m->item_count;
            if ((m->items[cur].flags & MENU_ITEM_ENABLED) && !(m->items[cur].flags & MENU_ITEM_SEPARATOR)) {
                m->highlighted = cur;
                m->hover_item_idx = -1;
                m->hover_timer_start = 0;
                wm_context_menu_damage(srv, m);
                break;
            }
        }
        return 1;
    }

    if (code == KEY_RIGHT) {
        if (m->highlighted >= 0 && m->highlighted < m->item_count) {
            vanilla_menu_item_t *it = &m->items[m->highlighted];
            if (it->submenu_id != 0 && (it->flags & MENU_ITEM_ENABLED)) {
                wm_context_menu_open_submenu(srv, m, m->highlighted);
            }
        }
        return 1;
    }

    if (code == KEY_LEFT) {
        if (srv->context_menu_depth > 1) {
            wm_context_menu_pop(srv);
        }
        return 1;
    }

    if (code == KEY_ENTER || code == KEY_KPENTER) {
        if (m->highlighted >= 0 && m->highlighted < m->item_count) {
            vanilla_menu_item_t *it = &m->items[m->highlighted];
            if ((it->flags & MENU_ITEM_ENABLED) && !(it->flags & MENU_ITEM_SEPARATOR)) {
                if (it->submenu_id != 0) {
                    wm_context_menu_open_submenu(srv, m, m->highlighted);
                } else {
                    wm_context_menu_close_stack(srv, it->item_id);
                }
            }
        }
        return 1;
    }

    return 1;
}

int wm_run_context_menu_selftests(void)
{
    static vanilla_server_t srv;
    memset(&srv, 0, sizeof(srv));
    srv.compositor.width = 800;
    srv.compositor.height = 600;

    /* 1. Geometry computation and boundary clamping */
    vanilla_context_menu_t m;
    memset(&m, 0, sizeof(m));
    m.x = 750;
    m.y = 550;
    m.item_count = 5;
    strncpy(m.items[0].label, "File", MENU_ITEM_LABEL_MAX - 1);
    menu_compute_size(&srv.compositor, &m, NULL);
    if (m.width < 160 || m.x + m.width > 800 || m.y + m.height > 600) {
        fprintf(stderr, "[context_menu_selftest] FAIL: geometry clamping\n");
        return -1;
    }

    /* 2. Submenu positioning flip to left when overflow */
    vanilla_context_menu_t parent_box;
    memset(&parent_box, 0, sizeof(parent_box));
    parent_box.x = 650;
    parent_box.y = 100;
    parent_box.width = 160;
    parent_box.height = 100;

    vanilla_context_menu_t sub;
    memset(&sub, 0, sizeof(sub));
    sub.parent_id = 1;
    sub.x = parent_box.x + parent_box.width;
    sub.y = 100;
    sub.item_count = 3;
    strncpy(sub.items[0].label, "Submenu Item", MENU_ITEM_LABEL_MAX - 1);
    menu_compute_size(&srv.compositor, &sub, &parent_box);
    if (sub.x + sub.width > 800 || sub.x != parent_box.x - sub.width) {
        fprintf(stderr, "[context_menu_selftest] FAIL: submenu flip\n");
        return -1;
    }

    /* 3. Stack depth limit */
    srv.context_menu_depth = CONTEXT_MENU_MAX_DEPTH;
    vanilla_context_menu_t parent_m;
    memset(&parent_m, 0, sizeof(parent_m));
    parent_m.item_count = 1;
    parent_m.items[0].flags = MENU_ITEM_ENABLED;
    parent_m.items[0].submenu_id = 99;
    if (wm_context_menu_open_submenu(&srv, &parent_m, 0) == 0) {
        fprintf(stderr, "[context_menu_selftest] FAIL: stack depth limit exceeded\n");
        return -1;
    }
    srv.context_menu_depth = 0;

    /* 4. Keyboard navigation Up/Down wrapping and item skipping */
    srv.context_menu_depth = 1;
    vanilla_context_menu_t *top = &srv.context_menu_stack[0];
    memset(top, 0, sizeof(*top));
    top->active = 1;
    top->item_count = 4;
    top->highlighted = 0;
    top->items[0].flags = MENU_ITEM_ENABLED;
    top->items[1].flags = MENU_ITEM_SEPARATOR;
    top->items[2].flags = 0; /* disabled */
    top->items[3].flags = MENU_ITEM_ENABLED;
    top->items[3].submenu_id = 5;

    /* Pre-register submenu 5 in pool */
    srv.context_menu_pool_count = 1;
    vanilla_context_menu_t *pool_sub = &srv.context_menu_pool[0];
    memset(pool_sub, 0, sizeof(*pool_sub));
    pool_sub->active = 1;
    pool_sub->menu_id = 5;
    pool_sub->item_count = 2;
    pool_sub->items[0].item_id = 51;
    pool_sub->items[0].flags = MENU_ITEM_ENABLED;
    strncpy(pool_sub->items[0].label, "Sub Item 1", MENU_ITEM_LABEL_MAX - 1);
    pool_sub->items[1].item_id = 52;
    pool_sub->items[1].flags = MENU_ITEM_ENABLED;
    strncpy(pool_sub->items[1].label, "Sub Item 2", MENU_ITEM_LABEL_MAX - 1);

    wm_context_menu_handle_key(&srv, KEY_DOWN, 1);
    if (top->highlighted != 3) {
        fprintf(stderr, "[context_menu_selftest] FAIL: down skip separator/disabled (got %d)\n", top->highlighted);
        return -1;
    }

    wm_context_menu_handle_key(&srv, KEY_DOWN, 1);
    if (top->highlighted != 0) {
        fprintf(stderr, "[context_menu_selftest] FAIL: down wraparound (got %d)\n", top->highlighted);
        return -1;
    }

    wm_context_menu_handle_key(&srv, KEY_UP, 1);
    if (top->highlighted != 3) {
        fprintf(stderr, "[context_menu_selftest] FAIL: up wraparound (got %d)\n", top->highlighted);
        return -1;
    }

    /* 5. Submenu open on Right arrow, pop on Left arrow */
    wm_context_menu_handle_key(&srv, KEY_RIGHT, 1);
    if (srv.context_menu_depth != 2 || !srv.context_menu_stack[1].active) {
        fprintf(stderr, "[context_menu_selftest] FAIL: open submenu via right arrow\n");
        return -1;
    }

    wm_context_menu_handle_key(&srv, KEY_LEFT, 1);
    if (srv.context_menu_depth != 1 || srv.context_menu_stack[1].active) {
        fprintf(stderr, "[context_menu_selftest] FAIL: pop submenu via left arrow\n");
        return -1;
    }

    /* 6. Submenu pop and root dismiss via Escape */
    wm_context_menu_handle_key(&srv, KEY_RIGHT, 1);
    if (srv.context_menu_depth != 2) {
        fprintf(stderr, "[context_menu_selftest] FAIL: reopen submenu\n");
        return -1;
    }

    wm_context_menu_handle_key(&srv, KEY_ESC, 1);
    if (srv.context_menu_depth != 1) {
        fprintf(stderr, "[context_menu_selftest] FAIL: pop submenu via Escape\n");
        return -1;
    }

    wm_context_menu_handle_key(&srv, KEY_ESC, 1);
    if (srv.context_menu_depth != 0) {
        fprintf(stderr, "[context_menu_selftest] FAIL: dismiss root via Escape\n");
        return -1;
    }

    /* 7. Click outside dismisses stack */
    wm_context_menu_open_desktop(&srv, 100, 100);
    if (srv.context_menu_depth != 1) {
        fprintf(stderr, "[context_menu_selftest] FAIL: open desktop menu\n");
        return -1;
    }
    wm_context_menu_handle_click(&srv, 500, 500, BTN_LEFT);
    if (srv.context_menu_depth != 0) {
        fprintf(stderr, "[context_menu_selftest] FAIL: click outside dismiss\n");
        return -1;
    }

    /* 8. Server-internal taskbar menu open */
    wm_context_menu_open_taskbar(&srv, 50, 580, 42);
    if (srv.context_menu_depth != 1 || srv.context_menu_stack[0].owning_window_id != 42) {
        fprintf(stderr, "[context_menu_selftest] FAIL: open taskbar menu\n");
        return -1;
    }
    wm_context_menu_close_stack(&srv, 0);
    if (srv.context_menu_depth != 0) {
        fprintf(stderr, "[context_menu_selftest] FAIL: close taskbar menu\n");
        return -1;
    }

    /* 9. Motion routing preserves child submenu on parent item, pops on other item */
    wm_context_menu_open_desktop(&srv, 100, 100);
    srv.context_menu_stack[0].items[1].submenu_id = 5;
    wm_context_menu_open_submenu(&srv, &srv.context_menu_stack[0], 1);
    if (srv.context_menu_depth != 2) {
        fprintf(stderr, "[context_menu_selftest] FAIL: open child for motion test\n");
        return -1;
    }
    /* Move cursor on row 1 of parent menu (item that opened child) -> child stays open */
    wm_context_menu_handle_motion(&srv, srv.context_menu_stack[0].x + 10, srv.context_menu_stack[0].y + 2 + 1 * 24 + 4);
    if (srv.context_menu_depth != 2) {
        fprintf(stderr, "[context_menu_selftest] FAIL: motion on parent item prematurely closed submenu\n");
        return -1;
    }
    /* Move cursor on row 0 of parent menu (different item) -> child closes */
    wm_context_menu_handle_motion(&srv, srv.context_menu_stack[0].x + 10, srv.context_menu_stack[0].y + 2 + 0 * 24 + 4);
    if (srv.context_menu_depth != 1) {
        fprintf(stderr, "[context_menu_selftest] FAIL: motion to different item did not close child\n");
        return -1;
    }
    wm_context_menu_close_stack(&srv, 0);

    printf("[vanilla] context menu & submenu self-tests passed (9/9)\n");
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
    int client_map[2 + VANILLA_MAX_CLIENTS];
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

    wm_context_menu_check_timers(srv);

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

    /* Check long-press timers (500ms motionless button down) */
    uint64_t now_ticks = pit_ticks();
    uint64_t long_press_ticks = pit_frequency() / 2;
    for (int b = 0; b < 3; b++) {
        if ((srv->mouse_buttons >> b) & 1) {
            uint64_t held = now_ticks - srv->btn_down_ticks[b];
            int dx = srv->cursor_x - srv->btn_down_x[b];
            int dy = srv->cursor_y - srv->btn_down_y[b];
            int chebyshev = (dx < 0 ? -dx : dx) > (dy < 0 ? -dy : dy) ? (dx < 0 ? -dx : dx) : (dy < 0 ? -dy : dy);
            if (chebyshev <= 4 && held >= long_press_ticks && !srv->long_press_fired[b]) {
                srv->long_press_fired[b] = 1;
                if (srv->focused_window_id != 0) {
                    vanilla_server_window_t *w = vanilla_server_find_window(srv, srv->focused_window_id);
                    if (w) {
                        int lx = srv->cursor_x - w->x;
                        int ly = srv->cursor_y - w->y;
                        struct input_event lp_ev;
                        memset(&lp_ev, 0, sizeof(lp_ev));
                        lp_ev.type = EV_KEY;
                        lp_ev.code = (b == 0) ? BTN_LEFT : ((b == 1) ? BTN_RIGHT : BTN_MIDDLE);
                        lp_ev.value = 4;
                        lp_ev.pad1 = (uint16_t)(lx < 0 ? 0 : lx);
                        lp_ev.pad2 = (uint32_t)(ly < 0 ? 0 : ly);
                        vanilla_server_send_input(srv, srv->focused_window_id, &lp_ev);
                    }
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

    (void)timeout_ms;
    int frame_ms = (int)(1000 / COMPOSITOR_TARGET_FPS);
    ret = poll(fds, (nfds_t)nfds, frame_ms);

    wm_context_menu_check_timers(srv);

    if (ret <= 0) {
        if (srv->compositor.dirty_count > 0) {
            if (compositor_frame_due(&srv->compositor)) {
                vanilla_server_send_frame_begin(srv);
                compositor_render_frame(srv);
                compositor_frame_rendered(&srv->compositor);
            }
        } else {
            vanilla_server_release_buffers(srv);
        }
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

    wm_context_menu_check_timers(srv);

    if (srv->compositor.dirty_count > 0) {
        if (compositor_frame_due(&srv->compositor)) {
            vanilla_server_send_frame_begin(srv);
            compositor_render_frame(srv);
            compositor_frame_rendered(&srv->compositor);
        }
    } else {
        vanilla_server_release_buffers(srv);
    }

    return 0;
}

int vanilla_server_send_input(vanilla_server_t *srv, uint32_t window_id, const struct input_event *ev)
{
    vanilla_server_window_t *w;
    vanilla_msg_hdr_t hdr;

    if (!srv || !ev)
        return -1;

    w = vanilla_server_find_window(srv, window_id);
    if (!w || !w->in_use || w->client_fd < 0)
        return -1;

    vanilla_client_conn_t *c = find_client_by_fd(srv, w->client_fd);
    if (c && c->negotiated_version >= 2) {
        vanilla_msg_input_event_v2_t msg2;
        hdr.magic = VANILLA_IPC_MAGIC;
        hdr.msg_type = MSG_INPUT_EVENT_V2;
        hdr.payload_len = (uint16_t)sizeof(msg2);
        hdr.window_id = window_id;
        msg2.event = *ev;
        msg2.mod_state = srv->mod_state;

        if (exact_write(w->client_fd, &hdr, sizeof(hdr)) < 0 ||
            exact_write(w->client_fd, &msg2, sizeof(msg2)) < 0)
            return -1;
    } else {
        vanilla_msg_input_event_t msg;
        hdr.magic = VANILLA_IPC_MAGIC;
        hdr.msg_type = MSG_INPUT_EVENT;
        hdr.payload_len = (uint16_t)sizeof(msg);
        hdr.window_id = window_id;
        msg.event = *ev;

        if (exact_write(w->client_fd, &hdr, sizeof(hdr)) < 0 ||
            exact_write(w->client_fd, &msg, sizeof(msg)) < 0)
            return -1;
    }

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
        old_w->has_keyboard_focus = 0;
        if (old_w->client_fd >= 0) {
            hdr.magic = VANILLA_IPC_MAGIC;
            hdr.msg_type = MSG_WINDOW_FOCUS;
            hdr.payload_len = (uint16_t)sizeof(msg);
            hdr.window_id = old_w->window_id;
            msg.focused = 0;
            exact_write(old_w->client_fd, &hdr, sizeof(hdr));
            exact_write(old_w->client_fd, &msg, sizeof(msg));
        }
        for (int i = 0; i < VANILLA_MAX_CLIENTS; i++) {
            if (srv->clients[i].in_use && srv->clients[i].fd != old_w->client_fd) {
                hdr.magic = VANILLA_IPC_MAGIC;
                hdr.msg_type = MSG_WINDOW_FOCUS;
                hdr.payload_len = (uint16_t)sizeof(msg);
                hdr.window_id = old_w->window_id;
                msg.focused = 0;
                exact_write(srv->clients[i].fd, &hdr, sizeof(hdr));
                exact_write(srv->clients[i].fd, &msg, sizeof(msg));
            }
        }
        wm_invalidate_window(srv, old_w);
    }

    new_w = vanilla_server_find_window(srv, window_id);
    if (new_w && new_w->in_use) {
        new_w->is_focused = 1;
        new_w->has_keyboard_focus = 0;
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
        for (int i = 0; i < VANILLA_MAX_CLIENTS; i++) {
            if (srv->clients[i].in_use && srv->clients[i].fd != new_w->client_fd) {
                hdr.magic = VANILLA_IPC_MAGIC;
                hdr.msg_type = MSG_WINDOW_FOCUS;
                hdr.payload_len = (uint16_t)sizeof(msg);
                hdr.window_id = new_w->window_id;
                msg.focused = 1;
                exact_write(srv->clients[i].fd, &hdr, sizeof(hdr));
                exact_write(srv->clients[i].fd, &msg, sizeof(msg));
            }
        }
        wm_invalidate_window(srv, new_w);
        if (g_theme->reduce_motion == 0 && new_w->anim_state == ANIM_IDLE) {
            vanilla_rect_t fr;
            wm_get_frame_rect(new_w, &fr);
            int32_t cx = fr.x + fr.w / 2;
            int32_t cy = fr.y + fr.h / 2;
            compositor_start_anim(new_w, ANIM_FOCUS, 80, 1.0f, 1.0f, 1.2f, 1.0f, cx, cy, 0);
        }
    } else {
        srv->focused_window_id = 0;
    }

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
        if (g_theme->reduce_motion != 0 || !w->is_mapped) {
            vanilla_server_destroy_window_record(srv, w);
        } else {
            vanilla_rect_t fr;
            wm_get_frame_rect(w, &fr);
            int32_t cx = fr.x + fr.w / 2;
            int32_t cy = fr.y + fr.h / 2;
            compositor_start_anim(w, ANIM_CLOSING, 100, 1.0f, 0.85f, 1.0f, 0.0f, cx, cy, 1);
            wm_invalidate_window(srv, w);
        }
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

            if (g_theme->reduce_motion == 0) {
                int32_t cx = target_x + (int32_t)target_w / 2;
                int32_t cy = target_y + (int32_t)target_h / 2;
                compositor_start_anim(w, ANIM_RESTORING, 150, 0.85f, 1.0f, 0.8f, 1.0f, cx, cy, 0);
            }
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

static void wm_alttab_build_list(vanilla_server_t *srv)
{
    srv->alttab_window_count = 0;
    for (int i = 0; i < VANILLA_MAX_WINDOWS; i++) {
        vanilla_server_window_t *w = &srv->windows[i];
        if (w->in_use && w->is_mapped &&
            !(w->flags & WINDOW_FLAG_POPUP) &&
            !(w->flags & WINDOW_FLAG_ALWAYS_TOP)) {
            srv->alttab_window_ids[srv->alttab_window_count++] = w->window_id;
        }
    }

    for (int i = 0; i < srv->alttab_window_count - 1; i++) {
        for (int j = i + 1; j < srv->alttab_window_count; j++) {
            vanilla_server_window_t *w_i = vanilla_server_find_window(srv, srv->alttab_window_ids[i]);
            vanilla_server_window_t *w_j = vanilla_server_find_window(srv, srv->alttab_window_ids[j]);
            int32_t z_i = w_i ? w_i->z_index : 0;
            int32_t z_j = w_j ? w_j->z_index : 0;
            if (z_j > z_i) {
                uint32_t tmp = srv->alttab_window_ids[i];
                srv->alttab_window_ids[i] = srv->alttab_window_ids[j];
                srv->alttab_window_ids[j] = tmp;
            }
        }
    }

    if (srv->alttab_window_count > 16)
        srv->alttab_window_count = 16;
}

int wm_handle_input_event(vanilla_server_t *srv, const struct input_event *ev)
{
    if (!srv || !ev)
        return -1;

    int32_t screen_w = (int32_t)srv->compositor.width;
    int32_t screen_h = (int32_t)srv->compositor.height;

    if (ev->type == EV_REL) {
        if (ev->code == REL_WHEEL) {
            printf("[wm] scroll: REL_WHEEL delta=%d\n", ev->value);
            uint32_t target_win_id = srv->focused_window_id;
            if (target_win_id == 0) {
                vanilla_server_window_t *under = wm_window_at(srv, srv->cursor_x, srv->cursor_y);
                if (under)
                    target_win_id = under->window_id;
            }
            if (target_win_id != 0) {
                vanilla_server_window_t *w = vanilla_server_find_window(srv, target_win_id);
                if (w) {
                    int lx = srv->cursor_x - w->x;
                    int ly = srv->cursor_y - w->y;
                    struct input_event client_ev = *ev;
                    client_ev.pad1 = (uint16_t)(lx < 0 ? 0 : lx);
                    client_ev.pad2 = (uint32_t)(ly < 0 ? 0 : ly);
                    vanilla_server_send_input(srv, target_win_id, &client_ev);
                }
            }
            return 0;
        }

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

            if (srv->context_menu_depth > 0) {
                cursor_set_active(CURSOR_ARROW);
            } else if (srv->is_resizing) {
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

            if (srv->context_menu_depth > 0) {
                wm_context_menu_handle_motion(srv, srv->cursor_x, srv->cursor_y);
            }

            if (srv->is_dragging) {
                vanilla_server_window_t *w = vanilla_server_find_window(srv, srv->drag_window_id);
                if (w) {
                    wm_invalidate_window(srv, w);
                    w->x = srv->cursor_x - srv->drag_offset_x;
                    w->y = srv->cursor_y - srv->drag_offset_y;
                    wm_invalidate_window(srv, w);

                    if (g_theme->reduce_motion == 0) {
                        vanilla_rect_t preview_zone;
                        int in_snap_zone = 0;
                        int32_t work_h = screen_h - TASKBAR_HEIGHT;
                        if (work_h < 100)
                            work_h = screen_h;

                        if (srv->cursor_y <= 16) {
                            preview_zone.x = 0;
                            preview_zone.y = 0;
                            preview_zone.w = screen_w;
                            preview_zone.h = work_h;
                            in_snap_zone = 1;
                        } else if (srv->cursor_x <= 16) {
                            preview_zone.x = 0;
                            preview_zone.y = 0;
                            preview_zone.w = screen_w / 2;
                            preview_zone.h = work_h;
                            in_snap_zone = 1;
                        } else if (srv->cursor_x >= screen_w - 16) {
                            preview_zone.x = screen_w / 2;
                            preview_zone.y = 0;
                            preview_zone.w = screen_w - (screen_w / 2);
                            preview_zone.h = work_h;
                            in_snap_zone = 1;
                        }

                        if (in_snap_zone) {
                            compositor_snap_preview_show(&srv->compositor, &preview_zone);
                        } else {
                            compositor_snap_preview_hide(&srv->compositor);
                        }
                    }
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
                        }
                    }
                }
            }
        }
        return 0;
    }

    if (ev->type == EV_KEY) {
        /* Update modifier keys */
        int is_mod = 0;
        switch (ev->code) {
        case KEY_LEFTSHIFT:   update_mod_state(srv, MOD_LSHIFT,    0, ev->value); is_mod = 1; break;
        case KEY_RIGHTSHIFT:  update_mod_state(srv, MOD_RSHIFT,    0, ev->value); is_mod = 1; break;
        case KEY_LEFTCTRL:    update_mod_state(srv, MOD_LCTRL,     0, ev->value); is_mod = 1; break;
        case KEY_RIGHTCTRL:   update_mod_state(srv, MOD_RCTRL,     0, ev->value); is_mod = 1; break;
        case KEY_LEFTALT:     update_mod_state(srv, MOD_LALT,      0, ev->value); is_mod = 1; break;
        case KEY_RIGHTALT:    update_mod_state(srv, MOD_RALT,      0, ev->value); is_mod = 1; break;
        case KEY_CAPSLOCK:    update_mod_state(srv, MOD_CAPS_LOCK, 1, ev->value); is_mod = 1; break;
        case KEY_NUMLOCK:     update_mod_state(srv, MOD_NUM_LOCK,  1, ev->value); is_mod = 1; break;
        case KEY_LEFTMETA:
        case KEY_RIGHTMETA:
            update_mod_state(srv, MOD_SUPER, 0, ev->value);
            for (int i = 0; i < VANILLA_MAX_WINDOWS; i++) {
                vanilla_server_window_t *sw = &srv->windows[i];
                if (sw->in_use && (sw->flags & WINDOW_FLAG_ALWAYS_TOP) && (sw->flags & WINDOW_FLAG_BORDERLESS) && !(sw->flags & WINDOW_FLAG_POPUP)) {
                    vanilla_server_send_input(srv, sw->window_id, ev);
                    break;
                }
            }
            return 0;
        default: break;
        }

        /* Alt release: commit Alt+Tab switcher selection */
        if (ev->code == KEY_LEFTALT || ev->code == KEY_RIGHTALT) {
            if (ev->value == 0 && !(srv->mod_state & MOD_ALT)) {
                if (srv->alttab_visible) {
                    if (srv->alttab_window_count > 0 && srv->alttab_selection >= 0 &&
                        srv->alttab_selection < srv->alttab_window_count) {
                        uint32_t target_win_id = srv->alttab_window_ids[srv->alttab_selection];
                        vanilla_server_focus_window(srv, target_win_id);
                        wm_raise_window(srv, target_win_id);
                    }
                    srv->alttab_visible = 0;
                    vanilla_rect_t r;
                    alttab_get_rect(srv, &r);
                    compositor_add_damage(&srv->compositor, &r);
                    return 0;
                }
            }
        }

        if (is_mod) {
            if (srv->focused_window_id != 0) {
                vanilla_server_send_input(srv, srv->focused_window_id, ev);
            }
            return 0;
        }

        /* Mouse buttons */
        if (ev->code == BTN_LEFT) {
            if (ev->value == 1) {
                srv->mouse_buttons |= (1u << 0);

                uint64_t now = pit_ticks();
                uint64_t click_timeout = pit_frequency() * 3 / 10;
                int same_pos = (abs(srv->cursor_x - srv->last_click_x[0]) <= 4 &&
                                abs(srv->cursor_y - srv->last_click_y[0]) <= 4);
                if (same_pos && (now - srv->last_click_ticks[0]) < click_timeout) {
                    srv->click_count[0]++;
                    if (srv->click_count[0] > 3)
                        srv->click_count[0] = 3;
                } else {
                    srv->click_count[0] = 1;
                }
                srv->last_click_ticks[0] = now;
                srv->last_click_x[0] = srv->cursor_x;
                srv->last_click_y[0] = srv->cursor_y;
                srv->btn_down_ticks[0] = now;
                srv->btn_down_x[0] = srv->cursor_x;
                srv->btn_down_y[0] = srv->cursor_y;
                srv->long_press_fired[0] = 0;

                /* 0. If Context Menu is open, dispatch to it */
                if (srv->context_menu_depth > 0) {
                    if (wm_context_menu_handle_click(srv, srv->cursor_x, srv->cursor_y, BTN_LEFT)) {
                        return 0;
                    }
                }

                /* 1. Window interaction: hit test top-to-bottom */
                vanilla_server_window_t *hit = wm_window_at(srv, srv->cursor_x, srv->cursor_y);

                if (hit) {
                    hit->has_keyboard_focus = 0;
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
                            if (g_theme->reduce_motion != 0) {
                                hit->is_mapped = 0;
                                hit->is_focused = 0;
                                if (srv->focused_window_id == hit->window_id)
                                    srv->focused_window_id = 0;
                                wm_invalidate_window(srv, hit);
                            } else {
                                vanilla_rect_t fr;
                                wm_get_frame_rect(hit, &fr);
                                int32_t cx = fr.x + fr.w / 2;
                                int32_t cy = fr.y + fr.h / 2;
                                compositor_start_anim(hit, ANIM_MINIMIZING, 150, 1.0f, 0.1f, 1.0f, 0.0f, cx, cy, 0);
                                hit->anim_unmap_on_done = 1;
                                wm_invalidate_window(srv, hit);
                            }
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

                    /* Click on titlebar body: initiate window drag with threshold or toggle maximize on double-click */
                    if (!(hit->flags & WINDOW_FLAG_BORDERLESS) &&
                        srv->cursor_y < frame.y + g_theme->titlebar_height + g_theme->border_width) {
                        if (srv->click_count[0] == 2) {
                            if (hit->is_snapped == SNAP_MAXIMIZE)
                                wm_unsnap_window(srv, hit->window_id);
                            else
                                wm_snap_window(srv, hit->window_id, SNAP_MAXIMIZE);
                            return 0;
                        }
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
                        client_ev.value = srv->click_count[0];
                        client_ev.pad1 = (uint16_t)(lx < 0 ? 0 : lx);
                        client_ev.pad2 = (uint32_t)(ly < 0 ? 0 : ly);
                        vanilla_server_send_input(srv, hit->window_id, &client_ev);
                        return 0;
                    }
                } else {
                    /* Clicked empty desktop: unfocus windows */
                    if (srv->focused_window_id != 0) {
                        vanilla_server_focus_window(srv, 0);
                    }
                }
                return 0;
            } else {
                srv->mouse_buttons &= ~(1u << 0);
                srv->drag_threshold_pending = 0;
                srv->long_press_fired[0] = 0;

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
                    compositor_snap_preview_hide(&srv->compositor);
                    /* Evaluate Aero-snap boundary triggers upon release matching preview zone */
                    if (srv->cursor_y <= 16) {
                        wm_snap_window(srv, srv->drag_window_id, SNAP_MAXIMIZE);
                    } else if (srv->cursor_x <= 16) {
                        wm_snap_window(srv, srv->drag_window_id, SNAP_LEFT);
                    } else if (srv->cursor_x >= screen_w - 16) {
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
            if (ev->value == 1) {
                srv->mouse_buttons |= (1u << 1);

                /* Close existing context menu stack first */
                if (srv->context_menu_depth > 0) {
                    wm_context_menu_close_stack(srv, 0);
                }

                uint64_t now = pit_ticks();
                uint64_t click_timeout = pit_frequency() * 3 / 10;
                int same_pos = (abs(srv->cursor_x - srv->last_click_x[1]) <= 4 &&
                                abs(srv->cursor_y - srv->last_click_y[1]) <= 4);
                if (same_pos && (now - srv->last_click_ticks[1]) < click_timeout) {
                    srv->click_count[1]++;
                    if (srv->click_count[1] > 3)
                        srv->click_count[1] = 3;
                } else {
                    srv->click_count[1] = 1;
                }
                srv->last_click_ticks[1] = now;
                srv->last_click_x[1] = srv->cursor_x;
                srv->last_click_y[1] = srv->cursor_y;
                srv->btn_down_ticks[1] = now;
                srv->btn_down_x[1] = srv->cursor_x;
                srv->btn_down_y[1] = srv->cursor_y;
                srv->long_press_fired[1] = 0;

                /* 1. Window right-click: hit-test top-to-bottom */
                vanilla_server_window_t *hit = wm_window_at(srv, srv->cursor_x, srv->cursor_y);
                if (hit) {
                    vanilla_server_focus_window(srv, hit->window_id);
                    wm_raise_window(srv, hit->window_id);
                    int lx = srv->cursor_x - hit->x;
                    int ly = srv->cursor_y - hit->y;
                    struct input_event client_ev = *ev;
                    client_ev.value = srv->click_count[1];
                    client_ev.pad1 = (uint16_t)(lx < 0 ? 0 : lx);
                    client_ev.pad2 = (uint32_t)(ly < 0 ? 0 : ly);
                    vanilla_server_send_input(srv, hit->window_id, &client_ev);
                    return 0;
                }

                /* 3. Empty desktop right-click */
                wm_context_menu_open_desktop(srv, srv->cursor_x, srv->cursor_y);
                return 0;
            } else {
                srv->mouse_buttons &= ~(1u << 1);
                srv->long_press_fired[1] = 0;
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

        if (ev->code == BTN_MIDDLE) {
            if (ev->value == 1) {
                srv->mouse_buttons |= (1u << 2);

                uint64_t now = pit_ticks();
                uint64_t click_timeout = pit_frequency() * 3 / 10;
                int same_pos = (abs(srv->cursor_x - srv->last_click_x[2]) <= 4 &&
                                abs(srv->cursor_y - srv->last_click_y[2]) <= 4);
                if (same_pos && (now - srv->last_click_ticks[2]) < click_timeout) {
                    srv->click_count[2]++;
                    if (srv->click_count[2] > 3)
                        srv->click_count[2] = 3;
                } else {
                    srv->click_count[2] = 1;
                }
                srv->last_click_ticks[2] = now;
                srv->last_click_x[2] = srv->cursor_x;
                srv->last_click_y[2] = srv->cursor_y;
                srv->btn_down_ticks[2] = now;
                srv->btn_down_x[2] = srv->cursor_x;
                srv->btn_down_y[2] = srv->cursor_y;
                srv->long_press_fired[2] = 0;
            } else {
                srv->mouse_buttons &= ~(1u << 2);
                srv->long_press_fired[2] = 0;
            }

            if (srv->focused_window_id != 0) {
                vanilla_server_window_t *w = vanilla_server_find_window(srv, srv->focused_window_id);
                if (w) {
                    int lx = srv->cursor_x - w->x;
                    int ly = srv->cursor_y - w->y;
                    struct input_event client_ev = *ev;
                    if (ev->value == 1)
                        client_ev.value = srv->click_count[2];
                    client_ev.pad1 = (uint16_t)(lx < 0 ? 0 : lx);
                    client_ev.pad2 = (uint32_t)(ly < 0 ? 0 : ly);
                    vanilla_server_send_input(srv, srv->focused_window_id, &client_ev);
                }
            }
            return 0;
        }

        /* Context menu keyboard navigation: Up, Down, Left, Right, Enter, Escape */
        if (srv->context_menu_depth > 0) {
            if (ev->code == KEY_UP || ev->code == KEY_DOWN ||
                ev->code == KEY_LEFT || ev->code == KEY_RIGHT ||
                ev->code == KEY_ENTER || ev->code == KEY_KPENTER ||
                ev->code == KEY_ESC) {
                wm_context_menu_handle_key(srv, ev->code, ev->value);
                return 0;
            }
        }

        /* Hotkey: Alt+Space forwarded to shell client window to toggle Quick Launcher */
        if ((srv->mod_state & MOD_ALT) && ev->code == KEY_SPACE && ev->value == 1) {
            for (int i = 0; i < VANILLA_MAX_WINDOWS; i++) {
                vanilla_server_window_t *sw = &srv->windows[i];
                if (sw->in_use && (sw->flags & WINDOW_FLAG_ALWAYS_TOP) && (sw->flags & WINDOW_FLAG_BORDERLESS) && !(sw->flags & WINDOW_FLAG_POPUP)) {
                    vanilla_server_send_input(srv, sw->window_id, ev);
                    return 0;
                }
            }
            return 0;
        }

        /* Alt+Tab and Tab focus cycling */
        if (ev->code == KEY_TAB && ev->value == 1) {
            if (srv->mod_state & MOD_ALT) {
                if (!srv->alttab_visible) {
                    wm_alttab_build_list(srv);
                    if (srv->alttab_window_count > 0) {
                        srv->alttab_visible = 1;
                        if (srv->mod_state & MOD_SHIFT)
                            srv->alttab_selection = srv->alttab_window_count - 1;
                        else
                            srv->alttab_selection = (srv->alttab_window_count > 1) ? 1 : 0;
                        vanilla_rect_t r;
                        alttab_get_rect(srv, &r);
                        compositor_add_damage(&srv->compositor, &r);
                    }
                } else {
                    if (srv->alttab_window_count > 0) {
                        if (srv->mod_state & MOD_SHIFT)
                            srv->alttab_selection = (srv->alttab_selection - 1 + srv->alttab_window_count) % srv->alttab_window_count;
                        else
                            srv->alttab_selection = (srv->alttab_selection + 1) % srv->alttab_window_count;
                        vanilla_rect_t r;
                        alttab_get_rect(srv, &r);
                        compositor_add_damage(&srv->compositor, &r);
                    }
                }
                return 0;
            } else if (!srv->alttab_visible) {
                uint32_t normal_wins[VANILLA_MAX_WINDOWS];
                int normal_count = 0;
                for (int i = 0; i < VANILLA_MAX_WINDOWS; i++) {
                    vanilla_server_window_t *w = &srv->windows[i];
                    if (w->in_use && w->is_mapped && w->layer == LAYER_NORMAL &&
                        !(w->flags & WINDOW_FLAG_POPUP) &&
                        !(w->flags & WINDOW_FLAG_ALWAYS_TOP)) {
                        normal_wins[normal_count++] = w->window_id;
                    }
                }
                for (int i = 0; i < normal_count - 1; i++) {
                    for (int j = i + 1; j < normal_count; j++) {
                        vanilla_server_window_t *w_i = vanilla_server_find_window(srv, normal_wins[i]);
                        vanilla_server_window_t *w_j = vanilla_server_find_window(srv, normal_wins[j]);
                        int32_t z_i = w_i ? w_i->z_index : 0;
                        int32_t z_j = w_j ? w_j->z_index : 0;
                        if (z_j > z_i) {
                            uint32_t tmp = normal_wins[i];
                            normal_wins[i] = normal_wins[j];
                            normal_wins[j] = tmp;
                        }
                    }
                }
                if (normal_count > 0) {
                    int cur_idx = -1;
                    for (int i = 0; i < normal_count; i++) {
                        if (normal_wins[i] == srv->focused_window_id) {
                            cur_idx = i;
                            break;
                        }
                    }
                    int next_idx;
                    if (srv->mod_state & MOD_SHIFT) {
                        if (cur_idx == -1)
                            next_idx = normal_count - 1;
                        else
                            next_idx = (cur_idx - 1 + normal_count) % normal_count;
                    } else {
                        if (cur_idx == -1)
                            next_idx = 0;
                        else
                            next_idx = (cur_idx + 1) % normal_count;
                    }
                    uint32_t new_id = normal_wins[next_idx];
                    vanilla_server_focus_window(srv, new_id);
                    vanilla_server_window_t *target = vanilla_server_find_window(srv, new_id);
                    if (target) {
                        target->has_keyboard_focus = 1;
                        wm_invalidate_window(srv, target);
                    }
                    return 0;
                }
            }
        }

        /* ESC closes overlays */
        if (ev->code == KEY_ESC && ev->value == 1) {
            if (srv->alttab_visible) {
                srv->alttab_visible = 0;
                vanilla_rect_t r;
                alttab_get_rect(srv, &r);
                compositor_add_damage(&srv->compositor, &r);
                return 0;
            }
        }

        /* Forward key event to focused window */
        if (srv->focused_window_id != 0) {
            vanilla_server_send_input(srv, srv->focused_window_id, ev);
            return 0;
        }
    }

    return 0;
}
