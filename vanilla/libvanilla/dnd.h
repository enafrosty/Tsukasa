/*
 * Project Tsukasa — Drag and Drop Client API
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

#ifndef _LIBVANILLA_DND_H
#define _LIBVANILLA_DND_H

#include <stddef.h>
#include <stdint.h>

#define DND_THRESHOLD_PX  8

struct vanilla_window;
struct vanilla_event;
#ifndef _VANILLA_H
typedef struct vanilla_window vanilla_window_t;
typedef struct vanilla_event vanilla_event_t;
#endif

/*
 * Called by a drag source after button-press-and-move crosses the threshold.
 * mime: MIME type of the offered data (e.g. "text/uri-list").
 * data: inline data (NUL-terminated file path for Phase 3).
 * ghost: optional 32x32 ARGB bitmap for the drag icon; may be NULL.
 * Returns 0 on success, negative errno on failure.
 */
int dnd_start_drag(vanilla_window_t *win, const char *mime,
                   const char *data, size_t data_len,
                   const uint32_t *ghost_32x32);

/*
 * Called by a drop target window from within its VANILLA_EVENT_DND_ENTER handler.
 * accepted: non-zero to accept the current drag type, zero to reject.
 */
int dnd_set_accept(vanilla_window_t *win, int accepted);

/*
 * Toolkit helper: register a drop target callback for a widget.
 * The callback receives (mime, data, data_len) when a drop occurs.
 * This is the P1-5 toolkit integration point.
 */
typedef void (*dnd_drop_fn)(const char *mime, const char *data,
                            size_t data_len, void *userdata);
int dnd_register_drop_target(vanilla_window_t *win, dnd_drop_fn fn, void *userdata);

/*
 * Unregister a drop target previously registered on win.
 */
int dnd_unregister_drop_target(vanilla_window_t *win);

/*
 * Helper to dispatch drop target callback if registered on win.
 */
int dnd_handle_event(vanilla_window_t *win, const vanilla_event_t *ev);

#endif /* _LIBVANILLA_DND_H */
