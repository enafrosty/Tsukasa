/*
 * Project Tsukasa — Display Server Drag and Drop Implementation
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

#include "dnd.h"
#include "server.h"
#include "blitter.h"
#include "cursor.h"
#include <string.h>
#include <stdio.h>
#include <unistd.h>
#include <errno.h>

static dnd_session_t g_dnd;

static int dnd_write_exact(int fd, const void *buf, size_t count)
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
                usleep(1000);
                continue;
            }
            return -1;
        }
    }
    return 0;
}

static void dnd_generate_default_ghost(uint32_t *out_ghost)
{
    for (int y = 0; y < 32; y++) {
        for (int x = 0; x < 32; x++) {
            out_ghost[y * 32 + x] = 0x00000000;
        }
    }

    /* Draw a 24x28 document icon with folded top-right corner */
    for (int y = 2; y < 30; y++) {
        for (int x = 4; x < 28; x++) {
            /* Folded corner check */
            if (x >= 20 && y <= (x - 20 + 2)) {
                continue;
            }

            int is_border = (x == 4 || x == 27 || y == 2 || y == 29 ||
                             (x >= 20 && y == (x - 20 + 2)));

            if (is_border) {
                out_ghost[y * 32 + x] = 0xFF2A75D3;
            } else {
                /* Inner text lines */
                if ((y == 10 || y == 14 || y == 18 || y == 22) && x >= 8 && x <= 23) {
                    out_ghost[y * 32 + x] = 0xFF6C757D;
                } else {
                    out_ghost[y * 32 + x] = 0xD8F8FAFC;
                }
            }
        }
    }
}

static int dnd_send_enter(vanilla_server_t *srv, uint32_t target_window_id,
                          int32_t local_x, int32_t local_y, const char *mime)
{
    if (!srv || target_window_id == 0)
        return -1;
    vanilla_server_window_t *w = vanilla_server_find_window(srv, target_window_id);
    if (!w || w->client_fd < 0)
        return -1;

    vanilla_msg_hdr_t hdr;
    vanilla_msg_dnd_enter_t enter;
    memset(&hdr, 0, sizeof(hdr));
    memset(&enter, 0, sizeof(enter));

    hdr.magic = VANILLA_IPC_MAGIC;
    hdr.msg_type = MSG_DND_ENTER;
    hdr.payload_len = (uint16_t)sizeof(enter);
    hdr.window_id = target_window_id;

    enter.local_x = local_x;
    enter.local_y = local_y;
    if (mime)
        snprintf(enter.mime, sizeof(enter.mime), "%s", mime);

    if (dnd_write_exact(w->client_fd, &hdr, sizeof(hdr)) < 0 ||
        dnd_write_exact(w->client_fd, &enter, sizeof(enter)) < 0)
        return -1;

    return 0;
}

static int dnd_send_leave(vanilla_server_t *srv, uint32_t target_window_id)
{
    if (!srv || target_window_id == 0)
        return -1;
    vanilla_server_window_t *w = vanilla_server_find_window(srv, target_window_id);
    if (!w || w->client_fd < 0)
        return -1;

    vanilla_msg_hdr_t hdr;
    vanilla_msg_dnd_leave_t leave;
    memset(&hdr, 0, sizeof(hdr));
    memset(&leave, 0, sizeof(leave));

    hdr.magic = VANILLA_IPC_MAGIC;
    hdr.msg_type = MSG_DND_LEAVE;
    hdr.payload_len = (uint16_t)sizeof(leave);
    hdr.window_id = target_window_id;

    if (dnd_write_exact(w->client_fd, &hdr, sizeof(hdr)) < 0 ||
        dnd_write_exact(w->client_fd, &leave, sizeof(leave)) < 0)
        return -1;

    return 0;
}

static int dnd_send_drop(vanilla_server_t *srv, uint32_t target_window_id,
                         int32_t local_x, int32_t local_y,
                         const char *mime, const char *data, uint32_t data_len)
{
    if (!srv || target_window_id == 0)
        return -1;
    vanilla_server_window_t *w = vanilla_server_find_window(srv, target_window_id);
    if (!w || w->client_fd < 0)
        return -1;

    vanilla_msg_hdr_t hdr;
    vanilla_msg_dnd_drop_t drop;
    memset(&hdr, 0, sizeof(hdr));
    memset(&drop, 0, sizeof(drop));

    hdr.magic = VANILLA_IPC_MAGIC;
    hdr.msg_type = MSG_DND_DROP;
    hdr.payload_len = (uint16_t)sizeof(drop);
    hdr.window_id = target_window_id;

    drop.local_x = local_x;
    drop.local_y = local_y;
    if (mime)
        snprintf(drop.mime, sizeof(drop.mime), "%s", mime);
    if (data) {
        size_t cpy = data_len < sizeof(drop.data) - 1 ? data_len : sizeof(drop.data) - 1;
        memcpy(drop.data, data, cpy);
        drop.data[cpy] = '\0';
        drop.data_len = (uint32_t)cpy;
    }

    if (dnd_write_exact(w->client_fd, &hdr, sizeof(hdr)) < 0 ||
        dnd_write_exact(w->client_fd, &drop, sizeof(drop)) < 0)
        return -1;

    return 0;
}

static int dnd_send_cancel(vanilla_server_t *srv, uint32_t source_window_id)
{
    if (!srv || source_window_id == 0)
        return -1;
    vanilla_server_window_t *w = vanilla_server_find_window(srv, source_window_id);
    if (!w || w->client_fd < 0)
        return -1;

    vanilla_msg_hdr_t hdr;
    vanilla_msg_dnd_cancel_t cancel;
    memset(&hdr, 0, sizeof(hdr));
    memset(&cancel, 0, sizeof(cancel));

    hdr.magic = VANILLA_IPC_MAGIC;
    hdr.msg_type = MSG_DND_CANCEL;
    hdr.payload_len = (uint16_t)sizeof(cancel);
    hdr.window_id = source_window_id;

    if (dnd_write_exact(w->client_fd, &hdr, sizeof(hdr)) < 0 ||
        dnd_write_exact(w->client_fd, &cancel, sizeof(cancel)) < 0)
        return -1;

    return 0;
}

void dnd_init(void)
{
    memset(&g_dnd, 0, sizeof(g_dnd));
    g_dnd.state = DND_IDLE;
    dnd_generate_default_ghost(g_dnd.ghost);
}

dnd_state_t dnd_get_state(void)
{
    return g_dnd.state;
}

int dnd_is_active(void)
{
    return g_dnd.state == DND_ACTIVE;
}

void dnd_get_ghost_rect(int32_t cx, int32_t cy, vanilla_rect_t *out_rect)
{
    if (!out_rect)
        return;
    out_rect->x = cx + 8;
    out_rect->y = cy + 8;
    out_rect->w = 32;
    out_rect->h = 32;
}

static void dnd_dirty_ghost(vanilla_server_t *srv, int32_t cx, int32_t cy)
{
    if (!srv)
        return;
    vanilla_rect_t r;
    dnd_get_ghost_rect(cx, cy, &r);
    compositor_add_damage(&srv->compositor, &r);
}

void dnd_handle_mouse_button(vanilla_server_t *srv, uint32_t window_id,
                             int pressed, int32_t x, int32_t y)
{
    if (!srv)
        return;

    if (pressed) {
        if (g_dnd.state == DND_IDLE) {
            g_dnd.state = DND_PENDING;
            g_dnd.source_window_id = window_id;
            g_dnd.start_x = x;
            g_dnd.start_y = y;
            g_dnd.cur_x = x;
            g_dnd.cur_y = y;
            g_dnd.over_window_id = 0;
            g_dnd.over_window_accepted = 0;
        }
    } else {
        if (g_dnd.state == DND_PENDING) {
            g_dnd.state = DND_IDLE;
            g_dnd.source_window_id = 0;
        } else if (g_dnd.state == DND_ACTIVE) {
            if (g_dnd.over_window_id != 0 && g_dnd.over_window_accepted) {
                g_dnd.state = DND_DROPPING;
                vanilla_server_window_t *w = vanilla_server_find_window(srv, g_dnd.over_window_id);
                int32_t lx = w ? (x - w->x) : 0;
                int32_t ly = w ? (y - w->y) : 0;
                dnd_send_drop(srv, g_dnd.over_window_id, lx, ly,
                              g_dnd.offer_mime, g_dnd.offer_path,
                              (uint32_t)strlen(g_dnd.offer_path));
                dnd_dirty_ghost(srv, x, y);
                cursor_set_active(CURSOR_ARROW);
            } else {
                if (g_dnd.over_window_id != 0)
                    dnd_send_leave(srv, g_dnd.over_window_id);
                if (g_dnd.source_window_id != 0)
                    dnd_send_cancel(srv, g_dnd.source_window_id);
                dnd_dirty_ghost(srv, x, y);
                cursor_set_active(CURSOR_ARROW);
                memset(&g_dnd, 0, sizeof(g_dnd));
                g_dnd.state = DND_IDLE;
            }
        }
    }
}

void dnd_handle_mouse_move(vanilla_server_t *srv, int32_t x, int32_t y)
{
    if (!srv)
        return;

    int32_t old_x = g_dnd.cur_x;
    int32_t old_y = g_dnd.cur_y;
    g_dnd.cur_x = x;
    g_dnd.cur_y = y;

    if (g_dnd.state == DND_PENDING) {
        int32_t dx = x - g_dnd.start_x;
        int32_t dy = y - g_dnd.start_y;
        if (dx * dx + dy * dy >= DND_THRESHOLD_PX * DND_THRESHOLD_PX) {
            /* Ready for offer from client */
        }
        return;
    }

    if (g_dnd.state == DND_ACTIVE) {
        vanilla_rect_t prev_r, new_r;
        dnd_get_ghost_rect(old_x, old_y, &prev_r);
        dnd_get_ghost_rect(x, y, &new_r);
        compositor_add_damage(&srv->compositor, &prev_r);
        compositor_add_damage(&srv->compositor, &new_r);

        vanilla_server_window_t *under = wm_window_at(srv, x, y);
        if (under && under->window_id != g_dnd.source_window_id &&
            !(under->flags & WINDOW_FLAG_POPUP)) {
            if (under->window_id != g_dnd.over_window_id) {
                if (g_dnd.over_window_id != 0)
                    dnd_send_leave(srv, g_dnd.over_window_id);
                g_dnd.over_window_id = under->window_id;
                g_dnd.over_window_accepted = 0;
                cursor_set_active(CURSOR_ARROW);
                dnd_send_enter(srv, under->window_id, x - under->x, y - under->y, g_dnd.offer_mime);
            }
        } else {
            if (g_dnd.over_window_id != 0) {
                dnd_send_leave(srv, g_dnd.over_window_id);
                g_dnd.over_window_id = 0;
                g_dnd.over_window_accepted = 0;
                cursor_set_active(CURSOR_ARROW);
            }
        }
    }
}

int dnd_handle_key_escape(vanilla_server_t *srv)
{
    if (!srv)
        return 0;

    if (g_dnd.state == DND_ACTIVE || g_dnd.state == DND_DROPPING) {
        if (g_dnd.source_window_id != 0)
            dnd_send_cancel(srv, g_dnd.source_window_id);
        if (g_dnd.over_window_id != 0)
            dnd_send_leave(srv, g_dnd.over_window_id);
        dnd_dirty_ghost(srv, srv->cursor_x, srv->cursor_y);
        cursor_set_active(CURSOR_ARROW);
        memset(&g_dnd, 0, sizeof(g_dnd));
        g_dnd.state = DND_IDLE;
        return 1;
    }
    return 0;
}

int dnd_handle_offer(vanilla_server_t *srv, uint32_t window_id,
                     const vanilla_msg_dnd_offer_t *offer)
{
    if (!srv || !offer)
        return -1;

    g_dnd.source_window_id = window_id ? window_id : g_dnd.source_window_id;
    snprintf(g_dnd.offer_mime, sizeof(g_dnd.offer_mime), "%s", offer->mime);
    snprintf(g_dnd.offer_path, sizeof(g_dnd.offer_path), "%s", offer->data);

    if (offer->has_ghost) {
        memcpy(g_dnd.ghost, offer->ghost, sizeof(g_dnd.ghost));
    } else {
        dnd_generate_default_ghost(g_dnd.ghost);
    }

    g_dnd.state = DND_ACTIVE;
    dnd_dirty_ghost(srv, srv->cursor_x, srv->cursor_y);
    return 0;
}

int dnd_handle_accept(vanilla_server_t *srv, uint32_t window_id,
                      const vanilla_msg_dnd_accept_t *accept)
{
    if (!srv || !accept)
        return -1;

    if (g_dnd.state == DND_ACTIVE) {
        if (window_id == g_dnd.over_window_id) {
            g_dnd.over_window_accepted = accept->accepted ? 1 : 0;
            if (g_dnd.over_window_accepted) {
                cursor_set_active(CURSOR_HAND);
            } else {
                cursor_set_active(CURSOR_ARROW);
            }
            vanilla_rect_t cbox;
            cursor_get_rect(srv->cursor_x, srv->cursor_y, &cbox);
            compositor_add_damage(&srv->compositor, &cbox);
        }
    } else if (g_dnd.state == DND_DROPPING) {
        if (accept->accepted) {
            dnd_dirty_ghost(srv, srv->cursor_x, srv->cursor_y);
            cursor_set_active(CURSOR_ARROW);
            memset(&g_dnd, 0, sizeof(g_dnd));
            g_dnd.state = DND_IDLE;
        } else {
            if (g_dnd.source_window_id != 0)
                dnd_send_cancel(srv, g_dnd.source_window_id);
            dnd_dirty_ghost(srv, srv->cursor_x, srv->cursor_y);
            cursor_set_active(CURSOR_ARROW);
            memset(&g_dnd, 0, sizeof(g_dnd));
            g_dnd.state = DND_IDLE;
        }
    }

    return 0;
}

void dnd_render_ghost(vanilla_server_t *srv, const vanilla_rect_t *dirty)
{
    if (!srv || !dirty || g_dnd.state != DND_ACTIVE)
        return;

    vanilla_rect_t ghost_rect;
    dnd_get_ghost_rect(srv->cursor_x, srv->cursor_y, &ghost_rect);
    vanilla_rect_t vis;
    if (vanilla_rect_intersect(&ghost_rect, dirty, &vis)) {
        int32_t sx = vis.x - ghost_rect.x;
        int32_t sy = vis.y - ghost_rect.y;
        blt_blend_subrect(srv->compositor.backbuffer, srv->compositor.pitch_px,
                          vis.x, vis.y,
                          g_dnd.ghost, 32,
                          sx, sy, vis.w, vis.h, 200);
    }
}

void dnd_cancel(vanilla_server_t *srv)
{
    if (!srv)
        return;

    if (g_dnd.state != DND_IDLE) {
        if (g_dnd.source_window_id != 0)
            dnd_send_cancel(srv, g_dnd.source_window_id);
        if (g_dnd.over_window_id != 0)
            dnd_send_leave(srv, g_dnd.over_window_id);
        dnd_dirty_ghost(srv, srv->cursor_x, srv->cursor_y);
        cursor_set_active(CURSOR_ARROW);
        memset(&g_dnd, 0, sizeof(g_dnd));
        g_dnd.state = DND_IDLE;
    }
}

void dnd_window_destroyed(vanilla_server_t *srv, uint32_t window_id)
{
    if (!srv || window_id == 0)
        return;

    if (window_id == g_dnd.source_window_id) {
        dnd_cancel(srv);
    } else if (window_id == g_dnd.over_window_id) {
        g_dnd.over_window_id = 0;
        g_dnd.over_window_accepted = 0;
        cursor_set_active(CURSOR_ARROW);
    }
}

/* Selftest covering state transitions without live socket IO */
int dnd_run_selftests(void)
{
    dnd_init();
    if (dnd_get_state() != DND_IDLE)
        return -1;

    vanilla_server_t srv;
    memset(&srv, 0, sizeof(srv));
    srv.cursor_x = 100;
    srv.cursor_y = 100;

    /* 1. Mouse down inside window -> PENDING */
    dnd_handle_mouse_button(&srv, 10, 1, 100, 100);
    if (dnd_get_state() != DND_PENDING)
        return -2;

    /* 2. Mouse move below threshold -> still PENDING */
    dnd_handle_mouse_move(&srv, 103, 103);
    if (dnd_get_state() != DND_PENDING)
        return -3;

    /* 3. Mouse release below threshold -> clean return to IDLE */
    dnd_handle_mouse_button(&srv, 0, 0, 103, 103);
    if (dnd_get_state() != DND_IDLE)
        return -4;

    /* 4. Mouse down + move beyond threshold + offer -> ACTIVE */
    dnd_handle_mouse_button(&srv, 10, 1, 100, 100);
    dnd_handle_mouse_move(&srv, 120, 120);

    vanilla_msg_dnd_offer_t offer;
    memset(&offer, 0, sizeof(offer));
    snprintf(offer.mime, sizeof(offer.mime), "%s", "text/uri-list");
    snprintf(offer.data, sizeof(offer.data), "%s", "file:///disk/test.txt");
    offer.data_len = (uint32_t)strlen(offer.data);
    offer.has_ghost = 0;

    dnd_handle_offer(&srv, 10, &offer);
    if (dnd_get_state() != DND_ACTIVE)
        return -5;
    if (!dnd_is_active())
        return -6;

    /* 5. Escape while ACTIVE -> clean cancellation to IDLE */
    if (!dnd_handle_key_escape(&srv))
        return -7;
    if (dnd_get_state() != DND_IDLE)
        return -8;

    /* 6. Offer + accept + drop flow */
    dnd_handle_mouse_button(&srv, 10, 1, 100, 100);
    dnd_handle_offer(&srv, 10, &offer);
    g_dnd.over_window_id = 20;

    vanilla_msg_dnd_accept_t acc;
    acc.accepted = 1;
    dnd_handle_accept(&srv, 20, &acc);
    if (!g_dnd.over_window_accepted)
        return -9;

    /* Mouse release -> DROPPING */
    dnd_handle_mouse_button(&srv, 0, 0, 120, 120);
    if (dnd_get_state() != DND_DROPPING)
        return -10;

    /* Target sends drop acceptance ack -> IDLE */
    dnd_handle_accept(&srv, 20, &acc);
    if (dnd_get_state() != DND_IDLE)
        return -11;

    printf("[vanilla] drag and drop state machine self-tests passed (6/6)\n");
    return 0;
}
