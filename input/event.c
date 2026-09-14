/*
 * Project Tsukasa — Input event ring buffer with overflow-aware fairness policy
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

#include "event.h"

#include "../include/spinlock.h"

static struct gui_event event_buf[EVENT_BUF_SIZE];
static unsigned int event_head;
static unsigned int event_tail;
static unsigned int event_count;
static spinlock_t event_lock = SPINLOCK_INIT;

static unsigned int queue_idx_from_head(unsigned int rel)
{
    return (event_head + rel) % EVENT_BUF_SIZE;
}

static int is_low_priority_event(const struct gui_event *e)
{
    if (!e)
        return 0;
    return (e->event_id == INPUT_EVENT_MOUSE_MOVE ||
            e->event_id == INPUT_EVENT_PAINT) ? 1 : 0;
}

static int try_drop_low_priority_locked(void)
{
    for (unsigned int rel = 0; rel < event_count; rel++) {
        unsigned int idx = queue_idx_from_head(rel);
        if (!is_low_priority_event(&event_buf[idx]))
            continue;

        for (unsigned int i = rel; i + 1 < event_count; i++) {
            unsigned int dst = queue_idx_from_head(i);
            unsigned int src = queue_idx_from_head(i + 1);
            event_buf[dst] = event_buf[src];
        }
        event_tail = (event_tail + EVENT_BUF_SIZE - 1u) % EVENT_BUF_SIZE;
        event_count--;
        return 1;
    }
    return 0;
}

static int try_coalesce_tail_locked(const struct gui_event *e)
{
    unsigned int tail_idx;
    struct gui_event *tail_ev;

    if (!e || event_count == 0)
        return 0;
    if (!is_low_priority_event(e))
        return 0;

    tail_idx = (event_tail + EVENT_BUF_SIZE - 1u) % EVENT_BUF_SIZE;
    tail_ev = &event_buf[tail_idx];
    if (tail_ev->event_id != e->event_id)
        return 0;
    if (tail_ev->window_id != e->window_id)
        return 0;

    *tail_ev = *e;
    return 1;
}

void event_init(void)
{
    unsigned long flags = spin_lock_irqsave(&event_lock);
    event_head = 0;
    event_tail = 0;
    event_count = 0;
    spin_unlock_irqrestore(&event_lock, flags);
}

int event_enqueue(const struct gui_event *e)
{
    if (!e)
        return -1;

    unsigned long flags = spin_lock_irqsave(&event_lock);

    if (try_coalesce_tail_locked(e)) {
        spin_unlock_irqrestore(&event_lock, flags);
        return 0;
    }

    if (event_count >= EVENT_BUF_SIZE) {
        if (!try_drop_low_priority_locked()) {
            event_head = (event_head + 1u) % EVENT_BUF_SIZE;
            event_count--;
        }
    }

    event_buf[event_tail] = *e;
    event_tail = (event_tail + 1u) % EVENT_BUF_SIZE;
    event_count++;

    spin_unlock_irqrestore(&event_lock, flags);
    return 0;
}

int event_dequeue(struct gui_event *e)
{
    if (!e)
        return 0;

    unsigned long flags = spin_lock_irqsave(&event_lock);
    if (event_count == 0) {
        spin_unlock_irqrestore(&event_lock, flags);
        return 0;
    }

    *e = event_buf[event_head];
    event_head = (event_head + 1u) % EVENT_BUF_SIZE;
    event_count--;
    spin_unlock_irqrestore(&event_lock, flags);
    return 1;
}
