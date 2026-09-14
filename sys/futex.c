/*
 * Project Tsukasa — futex: address-keyed block/wake primitive (guide 10)
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

#include "futex.h"

#include <stddef.h>

#include "../include/errno.h"
#include "../include/spinlock.h"
#include "../mm/heap.h"
#include "../proc/process.h"

#define FUTEX_BUCKETS 64

typedef struct futex_waiter {
    volatile uint32_t *uaddr;
    process_t *proc;
    struct futex_waiter *next;
} futex_waiter_t;

typedef struct futex_bucket {
    futex_waiter_t *head;
    spinlock_t lock;
} futex_bucket_t;

/* Zero-BSS is the fully initialized state (head == NULL, lock == SPINLOCK_INIT == 0), so — like the guide-09... */
static futex_bucket_t g_futex_buckets[FUTEX_BUCKETS];

static futex_bucket_t *futex_bucket(const volatile uint32_t *uaddr)
{
    uintptr_t key = (uintptr_t)uaddr >> 2;
    return &g_futex_buckets[key & (FUTEX_BUCKETS - 1)];
}

long kernel_futex_wait(volatile uint32_t *uaddr, uint32_t expected)
{
    futex_bucket_t *b;
    futex_waiter_t *node;
    futex_waiter_t **pp;
    unsigned long irqf;
    process_t *self = process_current();

    if (!uaddr || !self)
        return -EINVAL;

    /* Allocate BEFORE taking the bucket lock (adaptation note 3): the allocator takes its own locks (slab ->... */
    node = kmalloc(sizeof(*node));
    if (!node)
        return -ENOMEM;
    node->uaddr = uaddr;
    node->proc = self;
    node->next = NULL;

    b = futex_bucket(uaddr);
    irqf = spin_lock_irqsave(&b->lock);

    /* The race-closing check: a waker publishes the new value BEFORE calling wake, and wake takes this same... */
    if (*uaddr != expected) {
        spin_unlock_irqrestore(&b->lock, irqf);
        kfree(node);
        return -EAGAIN;
    }

    node->next = b->head;
    b->head = node;

    /* Mark ourselves BLOCKED (both state fields, nested g_sched_lock) while STILL holding the bucket lock: a... */
    process_block_current();

    spin_unlock_irqrestore(&b->lock, irqf);

    /* Deschedule. */
    process_yield();

    /* Resumed. Ownership protocol (adaptation note 4): if the waker unlinked our node, the waker frees it —... */
    irqf = spin_lock_irqsave(&b->lock);
    for (pp = &b->head; *pp; pp = &(*pp)->next) {
        if (*pp == node && (*pp)->proc == self) {
            *pp = node->next;
            spin_unlock_irqrestore(&b->lock, irqf);
            kfree(node);
            return 0;
        }
    }
    spin_unlock_irqrestore(&b->lock, irqf);
    return 0;
}

long kernel_futex_wake(volatile uint32_t *uaddr, int count)
{
    futex_bucket_t *b;
    futex_waiter_t *collected = NULL;
    futex_waiter_t **pp;
    futex_waiter_t *n;
    unsigned long irqf;
    long woken = 0;

    if (!uaddr)
        return -EINVAL;
    if (count <= 0)
        return 0;

    b = futex_bucket(uaddr);
    irqf = spin_lock_irqsave(&b->lock);

    pp = &b->head;
    while (*pp && woken < count) {
        n = *pp;
        if (n->uaddr != uaddr) {
            pp = &n->next;
            continue;
        }
        /* Unlink, then wake under the nested g_sched_lock (lock order bucket -> sched, identical to the wait side). */
        *pp = n->next;
        if (process_wake_blocked(n->proc))
            woken++;
        n->next = collected;
        collected = n;
    }

    spin_unlock_irqrestore(&b->lock, irqf);

    while (collected) {
        n = collected;
        collected = collected->next;
        kfree(n);
    }
    return woken;
}

int futex_pending_waiters(const volatile uint32_t *uaddr)
{
    futex_bucket_t *b;
    futex_waiter_t *n;
    unsigned long irqf;
    int cnt = 0;

    if (!uaddr)
        return 0;

    b = futex_bucket(uaddr);
    irqf = spin_lock_irqsave(&b->lock);
    for (n = b->head; n; n = n->next) {
        if (n->uaddr == uaddr)
            cnt++;
    }
    spin_unlock_irqrestore(&b->lock, irqf);
    return cnt;
}
