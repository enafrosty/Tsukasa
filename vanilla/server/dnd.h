/*
 * Project Tsukasa — Display Server Drag and Drop Subsystem
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

#ifndef _VANILLA_SERVER_DND_H
#define _VANILLA_SERVER_DND_H

#include <stdint.h>
#include <stddef.h>
#include "../include/protocol.h"
#include "../include/surface.h"

#define DND_THRESHOLD_PX  8

typedef enum {
    DND_IDLE      = 0,
    DND_PENDING,      /* button held, below drag threshold */
    DND_ACTIVE,       /* threshold crossed, session live */
    DND_DROPPING,     /* button released, awaiting accept/reject */
    DND_CANCELLED,
} dnd_state_t;

typedef struct {
    dnd_state_t state;
    uint32_t    source_window_id;
    int32_t     start_x, start_y;    /* pointer position at button press */
    int32_t     cur_x, cur_y;        /* current pointer position */
    uint32_t    over_window_id;       /* window under cursor, or 0 */
    int         over_window_accepted; /* 1 if MSG_DND_ACCEPT received */

    /* Offered data: for Phase 3, a single NUL-terminated file path */
    char        offer_path[256];
    char        offer_mime[64];       /* e.g. "text/uri-list" */

    /* Ghost image: a 32x32 ARGB bitmap copied from the drag source */
    uint32_t    ghost[32 * 32];
} dnd_session_t;

typedef struct vanilla_server vanilla_server_t;

void        dnd_init(void);
dnd_state_t dnd_get_state(void);
int         dnd_is_active(void);
void        dnd_get_ghost_rect(int32_t cx, int32_t cy, vanilla_rect_t *out_rect);
void        dnd_handle_mouse_button(vanilla_server_t *srv, uint32_t window_id, int pressed, int32_t x, int32_t y);
void        dnd_handle_mouse_move(vanilla_server_t *srv, int32_t x, int32_t y);
int         dnd_handle_key_escape(vanilla_server_t *srv);
int         dnd_handle_offer(vanilla_server_t *srv, uint32_t window_id, const vanilla_msg_dnd_offer_t *offer);
int         dnd_handle_accept(vanilla_server_t *srv, uint32_t window_id, const vanilla_msg_dnd_accept_t *accept);
void        dnd_render_ghost(vanilla_server_t *srv, const vanilla_rect_t *dirty);
void        dnd_cancel(vanilla_server_t *srv);
void        dnd_window_destroyed(vanilla_server_t *srv, uint32_t window_id);
int         dnd_run_selftests(void);

#endif /* _VANILLA_SERVER_DND_H */
