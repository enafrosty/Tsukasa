/*
 * Project Tsukasa — Notification Client Library API
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

#ifndef _LIBVANILLA_NOTIFY_H
#define _LIBVANILLA_NOTIFY_H

#include <stdint.h>
#include <stddef.h>
#include "../services/notifyd.h"

/*
 * notify_send: Post a desktop notification.
 *   title: Short summary (max VNOTIF_TITLE_MAX chars).
 *   body: Longer description (max VNOTIF_BODY_MAX chars); may be NULL.
 *   icon_name: Icon hint, e.g. "info", "warning", "error"; may be NULL.
 *   timeout_ms: Auto-dismiss after this many milliseconds; 0 = persistent.
 *   actions: Array of (label, action_id) pairs; may be NULL.
 *   action_count: Number of entries in actions; 0 if no actions.
 * Returns a notification ID on success, or negative errno on failure.
 */
int32_t notify_send(const char *title, const char *body, const char *icon_name,
                    uint32_t timeout_ms,
                    const vnotif_action_t *actions, int action_count);

/*
 * notify_close: Explicitly dismiss a notification by ID.
 * Returns 0 on success, negative errno on failure.
 */
int notify_close(uint32_t notif_id);

/*
 * notify_wait_action: Block (busy-poll until P4-1) for an action event
 *   on the given notification. Fills out_action_id with the selected action
 *   (0 = dismissed). Returns 0 on success, negative errno on failure.
 */
int notify_wait_action(uint32_t notif_id, uint32_t *out_action_id);

#endif /* _LIBVANILLA_NOTIFY_H */
