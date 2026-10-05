/*
 * Project Tsukasa — Drag and Drop Client Implementation
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
#include "../include/vanilla.h"
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>

#define MAX_DROP_TARGETS 32

typedef struct {
    uint32_t    window_id;
    dnd_drop_fn fn;
    void        *userdata;
} dnd_drop_target_entry_t;

static dnd_drop_target_entry_t g_drop_targets[MAX_DROP_TARGETS];
static int                      g_drop_target_count = 0;

static int dnd_exact_write(int fd, const void *buf, size_t count)
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

int dnd_start_drag(vanilla_window_t *win, const char *mime,
                   const char *data, size_t data_len,
                   const uint32_t *ghost_32x32)
{
    if (!win || !win->client || !mime || !data)
        return -EINVAL;

    int fd = vanilla_client_get_fd(win->client);
    if (fd < 0)
        return -EBADF;

    vanilla_msg_hdr_t hdr;
    vanilla_msg_dnd_offer_t offer;
    memset(&hdr, 0, sizeof(hdr));
    memset(&offer, 0, sizeof(offer));

    hdr.magic = VANILLA_IPC_MAGIC;
    hdr.msg_type = MSG_DND_OFFER;
    hdr.payload_len = (uint16_t)sizeof(offer);
    hdr.window_id = win->window_id;

    snprintf(offer.mime, sizeof(offer.mime), "%s", mime);
    size_t copy_len = data_len < sizeof(offer.data) - 1 ? data_len : sizeof(offer.data) - 1;
    memcpy(offer.data, data, copy_len);
    offer.data[copy_len] = '\0';
    offer.data_len = (uint32_t)copy_len;

    if (ghost_32x32) {
        offer.has_ghost = 1;
        memcpy(offer.ghost, ghost_32x32, sizeof(offer.ghost));
    } else {
        offer.has_ghost = 0;
    }

    if (dnd_exact_write(fd, &hdr, sizeof(hdr)) < 0 ||
        dnd_exact_write(fd, &offer, sizeof(offer)) < 0)
        return -EIO;

    return 0;
}

int dnd_set_accept(vanilla_window_t *win, int accepted)
{
    if (!win || !win->client)
        return -EINVAL;

    int fd = vanilla_client_get_fd(win->client);
    if (fd < 0)
        return -EBADF;

    vanilla_msg_hdr_t hdr;
    vanilla_msg_dnd_accept_t acc;
    memset(&hdr, 0, sizeof(hdr));
    memset(&acc, 0, sizeof(acc));

    hdr.magic = VANILLA_IPC_MAGIC;
    hdr.msg_type = MSG_DND_ACCEPT;
    hdr.payload_len = (uint16_t)sizeof(acc);
    hdr.window_id = win->window_id;
    acc.accepted = accepted ? 1 : 0;

    if (dnd_exact_write(fd, &hdr, sizeof(hdr)) < 0 ||
        dnd_exact_write(fd, &acc, sizeof(acc)) < 0)
        return -EIO;

    return 0;
}

int dnd_register_drop_target(vanilla_window_t *win, dnd_drop_fn fn, void *userdata)
{
    if (!win)
        return -EINVAL;

    for (int i = 0; i < g_drop_target_count; i++) {
        if (g_drop_targets[i].window_id == win->window_id) {
            g_drop_targets[i].fn = fn;
            g_drop_targets[i].userdata = userdata;
            return 0;
        }
    }

    if (g_drop_target_count >= MAX_DROP_TARGETS)
        return -ENOMEM;

    g_drop_targets[g_drop_target_count].window_id = win->window_id;
    g_drop_targets[g_drop_target_count].fn = fn;
    g_drop_targets[g_drop_target_count].userdata = userdata;
    g_drop_target_count++;
    return 0;
}

int dnd_handle_event(vanilla_window_t *win, const vanilla_event_t *ev)
{
    if (!win || !ev)
        return 0;

    dnd_drop_target_entry_t *entry = NULL;
    for (int i = 0; i < g_drop_target_count; i++) {
        if (g_drop_targets[i].window_id == win->window_id) {
            entry = &g_drop_targets[i];
            break;
        }
    }
    if (!entry)
        return 0;

    if (ev->type == VANILLA_EVENT_DND_ENTER) {
        dnd_set_accept(win, 1);
        return 1;
    }

    if (ev->type == VANILLA_EVENT_DND_DROP) {
        if (entry->fn)
            entry->fn(ev->dnd_drop.mime, ev->dnd_drop.data, ev->dnd_drop.data_len, entry->userdata);
        dnd_set_accept(win, 1);
        return 1;
    }

    return 0;
}
