/*
 * Project Tsukasa — /dev/keyboard and /dev/mouse device ring buffers
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

#include "input_dev.h"
#include "pit.h"
#include "../include/spinlock.h"
#include "../include/errno.h"
#include "../sys/wait_queue.h"
#include "../proc/process.h"
#include "../fs/vfs.h"
#include <stddef.h>
#include <stdint.h>

#define INPUT_RING_SIZE 256

static struct input_event g_unified_ring[INPUT_RING_SIZE];
static uint32_t g_unified_head = 0;
static uint32_t g_unified_tail = 0;
static uint32_t g_unified_count = 0;
static spinlock_t g_unified_lock = SPINLOCK_INIT;
static wait_queue_head_t g_unified_waitq;
static int g_unified_open_refs = 0;

/* Legacy device ring buffers */
static struct input_event g_kbd_ring[INPUT_RING_SIZE];
static uint32_t g_kbd_head = 0, g_kbd_tail = 0, g_kbd_count = 0;
static int g_kbd_open_refs = 0;

static struct input_event g_mouse_ring[INPUT_RING_SIZE];
static uint32_t g_mouse_head = 0, g_mouse_tail = 0, g_mouse_count = 0;
static int g_mouse_open_refs = 0;

void input_dev_init(void)
{
    wait_queue_init(&g_unified_waitq);
    g_unified_head = g_unified_tail = g_unified_count = 0;
    g_kbd_head = g_kbd_tail = g_kbd_count = 0;
    g_mouse_head = g_mouse_tail = g_mouse_count = 0;
}

void input_dev_open(int dev)
{
    unsigned long flags = spin_lock_irqsave(&g_unified_lock);
    if (dev == INPUT_DEV_UNIFIED)
        g_unified_open_refs++;
    else if (dev == INPUT_DEV_KEYBOARD)
        g_kbd_open_refs++;
    else if (dev == INPUT_DEV_MOUSE)
        g_mouse_open_refs++;
    spin_unlock_irqrestore(&g_unified_lock, flags);
}

void input_dev_close(int dev)
{
    unsigned long flags = spin_lock_irqsave(&g_unified_lock);
    if (dev == INPUT_DEV_UNIFIED && g_unified_open_refs > 0)
        g_unified_open_refs--;
    else if (dev == INPUT_DEV_KEYBOARD && g_kbd_open_refs > 0)
        g_kbd_open_refs--;
    else if (dev == INPUT_DEV_MOUSE && g_mouse_open_refs > 0)
        g_mouse_open_refs--;
    spin_unlock_irqrestore(&g_unified_lock, flags);
}

void input_dev_push_event(const struct input_event *ev)
{
    if (!ev)
        return;

    unsigned long flags = spin_lock_irqsave(&g_unified_lock);

    /* Push into unified ring buffer */
    if (g_unified_count >= INPUT_RING_SIZE) {
        g_unified_head = (g_unified_head + 1u) % INPUT_RING_SIZE;
        g_unified_count--;
    }
    g_unified_ring[g_unified_tail] = *ev;
    g_unified_tail = (g_unified_tail + 1u) % INPUT_RING_SIZE;
    g_unified_count++;

    /* Also feed legacy rings if opened */
    if (ev->type == EV_KEY && ev->code < BTN_LEFT) {
        if (g_kbd_count >= INPUT_RING_SIZE) {
            g_kbd_head = (g_kbd_head + 1u) % INPUT_RING_SIZE;
            g_kbd_count--;
        }
        g_kbd_ring[g_kbd_tail] = *ev;
        g_kbd_tail = (g_kbd_tail + 1u) % INPUT_RING_SIZE;
        g_kbd_count++;
    } else if (ev->type == EV_REL || (ev->type == EV_KEY && ev->code >= BTN_LEFT)) {
        if (g_mouse_count >= INPUT_RING_SIZE) {
            g_mouse_head = (g_mouse_head + 1u) % INPUT_RING_SIZE;
            g_mouse_count--;
        }
        g_mouse_ring[g_mouse_tail] = *ev;
        g_mouse_tail = (g_mouse_tail + 1u) % INPUT_RING_SIZE;
        g_mouse_count++;
    }

    spin_unlock_irqrestore(&g_unified_lock, flags);

    /* Wake blocking readers on wait queue */
    wait_queue_wake_all(&g_unified_waitq);
}

void input_dev_push(uint32_t type, uint16_t code, int32_t value)
{
    struct input_event ev;
    uint32_t hz = pit_frequency();
    uint64_t now_ms = (hz > 0) ? (pit_ticks() * 1000ULL / hz) : pit_ticks();

    ev.type = type;
    ev.code = code;
    ev.value = value;
    ev.timestamp_ms = now_ms;

    input_dev_push_event(&ev);
}

size_t input_dev_read_events(void *buf, size_t count, int flags)
{
    size_t rec = sizeof(struct input_event);
    uint8_t *out = (uint8_t *)buf;
    size_t written = 0;

    if (!buf || count < rec)
        return 0;

    for (;;) {
        unsigned long irqf = spin_lock_irqsave(&g_unified_lock);
        if (g_unified_count > 0) {
            while (g_unified_count > 0 && (count - written) >= rec) {
                struct input_event *src = &g_unified_ring[g_unified_head];
                for (size_t i = 0; i < rec; i++)
                    out[written + i] = ((const uint8_t *)src)[i];
                g_unified_head = (g_unified_head + 1u) % INPUT_RING_SIZE;
                g_unified_count--;
                written += rec;
            }
            spin_unlock_irqrestore(&g_unified_lock, irqf);
            return written;
        }

        /* Buffer is empty */
        if (flags & VFS_O_NONBLOCK) {
            spin_unlock_irqrestore(&g_unified_lock, irqf);
            return 0;
        }

        /* Blocking read: wait on wait queue */
        process_t *self = process_current();
        if (!self) {
            spin_unlock_irqrestore(&g_unified_lock, irqf);
            return 0;
        }

        wait_queue_entry_t entry;
        entry.proc = self;
        entry.next = NULL;
        wait_queue_add(&g_unified_waitq, &entry);
        process_block_current();
        spin_unlock_irqrestore(&g_unified_lock, irqf);

        process_yield();

        wait_queue_remove(&g_unified_waitq, &entry);
    }
}

int input_dev_poll_events(int events)
{
    int revents = events & VFS_POLLOUT;
    unsigned long flags = spin_lock_irqsave(&g_unified_lock);
    if (g_unified_count > 0)
        revents |= (events & VFS_POLLIN);
    spin_unlock_irqrestore(&g_unified_lock, flags);
    return revents;
}

size_t input_dev_read(int dev, void *buf, size_t count)
{
    size_t rec = sizeof(struct input_event);
    uint8_t *out = (uint8_t *)buf;
    size_t written = 0;

    if (!buf || count < rec)
        return 0;

    unsigned long flags = spin_lock_irqsave(&g_unified_lock);
    if (dev == INPUT_DEV_KEYBOARD) {
        while (g_kbd_count > 0 && (count - written) >= rec) {
            struct input_event *src = &g_kbd_ring[g_kbd_head];
            for (size_t i = 0; i < rec; i++)
                out[written + i] = ((const uint8_t *)src)[i];
            g_kbd_head = (g_kbd_head + 1u) % INPUT_RING_SIZE;
            g_kbd_count--;
            written += rec;
        }
    } else if (dev == INPUT_DEV_MOUSE) {
        while (g_mouse_count > 0 && (count - written) >= rec) {
            struct input_event *src = &g_mouse_ring[g_mouse_head];
            for (size_t i = 0; i < rec; i++)
                out[written + i] = ((const uint8_t *)src)[i];
            g_mouse_head = (g_mouse_head + 1u) % INPUT_RING_SIZE;
            g_mouse_count--;
            written += rec;
        }
    }
    spin_unlock_irqrestore(&g_unified_lock, flags);
    return written;
}

int input_dev_poll_readable(int dev)
{
    int ready = 0;
    unsigned long flags = spin_lock_irqsave(&g_unified_lock);
    if (dev == INPUT_DEV_KEYBOARD)
        ready = (g_kbd_count > 0);
    else if (dev == INPUT_DEV_MOUSE)
        ready = (g_mouse_count > 0);
    else if (dev == INPUT_DEV_UNIFIED)
        ready = (g_unified_count > 0);
    spin_unlock_irqrestore(&g_unified_lock, flags);
    return ready;
}
