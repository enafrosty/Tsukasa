/*
 * event.c - Input event ring buffer with overflow-aware fairness policy.
 */

#include "event.h"

#include "../include/spinlock.h"

static struct input_event event_buf[EVENT_BUF_SIZE];
static unsigned int event_head;
static unsigned int event_tail;
static unsigned int event_count;
static spinlock_t event_lock = SPINLOCK_INIT;

static unsigned int queue_idx_from_head(unsigned int rel)
{
    return (event_head + rel) % EVENT_BUF_SIZE;
}

static int is_low_priority_event(const struct input_event *e)
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

        /* Remove queue slot by shifting all following retained events. */
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

static int try_coalesce_tail_locked(const struct input_event *e)
{
    unsigned int tail_idx;
    struct input_event *tail_ev;

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
    spin_lock(&event_lock);
    event_head = 0;
    event_tail = 0;
    event_count = 0;
    spin_unlock(&event_lock);
}

int event_enqueue(const struct input_event *e)
{
    if (!e)
        return -1;

    spin_lock(&event_lock);

    if (try_coalesce_tail_locked(e)) {
        spin_unlock(&event_lock);
        return 0;
    }

    if (event_count >= EVENT_BUF_SIZE) {
        if (!try_drop_low_priority_locked()) {
            /*
             * Hard overflow fallback: drop the oldest event so producers keep
             * making forward progress under sustained bursts.
             */
            event_head = (event_head + 1u) % EVENT_BUF_SIZE;
            event_count--;
        }
    }

    event_buf[event_tail] = *e;
    event_tail = (event_tail + 1u) % EVENT_BUF_SIZE;
    event_count++;

    spin_unlock(&event_lock);
    return 0;
}

int event_dequeue(struct input_event *e)
{
    if (!e)
        return 0;

    spin_lock(&event_lock);
    if (event_count == 0) {
        spin_unlock(&event_lock);
        return 0;
    }

    *e = event_buf[event_head];
    event_head = (event_head + 1u) % EVENT_BUF_SIZE;
    event_count--;
    spin_unlock(&event_lock);
    return 1;
}
