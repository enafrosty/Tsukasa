/*
 * Project Tsukasa — Kernel wait queue and poll implementation
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

#include <stddef.h>
#include <stdint.h>
#include "sys/wait_queue.h"
#include "proc/process.h"
#include "include/kprintf.h"

void wait_queue_init(wait_queue_head_t *h) {
    if (!h) return;
    h->head = NULL;
    h->lock = SPINLOCK_INIT;
}

void wait_queue_add(wait_queue_head_t *h, wait_queue_entry_t *entry) {
    if (!h || !entry) return;
    unsigned long flags = spin_lock_irqsave(&h->lock);

    wait_queue_entry_t *curr = h->head;
    while (curr) {
        if (curr == entry) {
            spin_unlock_irqrestore(&h->lock, flags);
            return;
        }
        curr = curr->next;
    }

    entry->next = h->head;
    h->head = entry;
    spin_unlock_irqrestore(&h->lock, flags);
}

void wait_queue_remove(wait_queue_head_t *h, wait_queue_entry_t *entry) {
    if (!h || !entry) return;
    unsigned long flags = spin_lock_irqsave(&h->lock);

    wait_queue_entry_t *prev = NULL;
    wait_queue_entry_t *curr = h->head;

    while (curr) {
        if (curr == entry) {
            if (prev) prev->next = curr->next;
            else h->head = curr->next;
            curr->next = NULL;
            break;
        }
        prev = curr;
        curr = curr->next;
    }

    spin_unlock_irqrestore(&h->lock, flags);
}

void wait_queue_wake_all(wait_queue_head_t *h) {
    process_t *to_wake[32];
    int count = 0;

    if (!h) return;
    unsigned long flags = spin_lock_irqsave(&h->lock);

    wait_queue_entry_t *curr = h->head;
    while (curr && count < 32) {
        if (curr->proc &&
            (curr->proc->state == PROCESS_BLOCKED ||
             curr->proc->state == PROCESS_SLEEPING)) {
            to_wake[count++] = curr->proc;
        }
        curr = curr->next;
    }

    spin_unlock_irqrestore(&h->lock, flags);

    for (int i = 0; i < count; i++) {
        process_wake_blocked(to_wake[i]);
    }
}

void poll_wtable_queue(wait_queue_head_t *h, poll_table_t *pt) {
    poll_wtable_t *wt = (poll_wtable_t *)pt;
    if (wt->count >= MAX_POLL_ENTRIES) return;
    poll_entry_t *pe = &wt->entries[wt->count++];
    pe->h = h;
    pe->entry.proc = process_current();
    pe->entry.next = NULL;
    wait_queue_add(h, &pe->entry);
}

void poll_wtable_init(poll_wtable_t *wt, struct process *proc) {
    (void)proc;
    wt->pt.qproc = poll_wtable_queue;
    wt->count = 0;
}

void poll_wtable_unregister_all(poll_wtable_t *wt) {
    for (int i = 0; i < wt->count; i++) {
        poll_entry_t *pe = &wt->entries[i];
        if (pe->h)
            wait_queue_remove(pe->h, &pe->entry);
    }
    wt->count = 0;
}

static volatile int wq_test_woken __attribute__((unused)) = 0;

static void __attribute__((unused)) wq_writer_task(void) {
    for (int i = 0; i < 4; i++) process_yield();
    wq_test_woken = 1;
    process_exit(0);
}

void wait_queue_run_selftests(void) {
    // Test 1: init / add / remove count invariant
    wait_queue_head_t q;
    wait_queue_init(&q);

    wait_queue_entry_t entries[MAX_POLL_ENTRIES];
    for (int i = 0; i < MAX_POLL_ENTRIES; i++) {
        entries[i].proc = process_current();
        entries[i].next = NULL;
        wait_queue_add(&q, &entries[i]);
    }

    int cnt = 0;
    wait_queue_entry_t *cur = q.head;
    while (cur) { cnt++; cur = cur->next; }
    if (cnt == MAX_POLL_ENTRIES)
        kprintf("[parity][K01] 32-entry add PASS\n");
    else
        kprintf("[parity][K01] 32-entry add FAIL (cnt=%d)\n", cnt);

    for (int i = 0; i < MAX_POLL_ENTRIES; i++)
        wait_queue_remove(&q, &entries[i]);

    cnt = 0;
    cur = q.head;
    while (cur) { cnt++; cur = cur->next; }
    if (cnt == 0)
        kprintf("[parity][K01] 32-entry remove PASS\n");
    else
        kprintf("[parity][K01] 32-entry remove FAIL (cnt=%d)\n", cnt);

    poll_wtable_t wt;
    poll_wtable_init(&wt, process_current());

    wait_queue_head_t queues[MAX_POLL_ENTRIES];
    for (int i = 0; i < MAX_POLL_ENTRIES; i++) {
        wait_queue_init(&queues[i]);
        wt.pt.qproc(&queues[i], &wt.pt);
    }

    int pre = wt.count;
    poll_wtable_unregister_all(&wt);

    int leaked = 0;
    for (int i = 0; i < MAX_POLL_ENTRIES; i++) {
        if (queues[i].head != NULL) leaked++;
    }

    if (pre == MAX_POLL_ENTRIES && wt.count == 0 && leaked == 0)
        kprintf("[parity][K01] poll_wtable 32-fd PASS\n");
    else
        kprintf("[parity][K01] poll_wtable 32-fd FAIL (pre=%d count=%d leaked=%d)\n",
                pre, wt.count, leaked);

    wait_queue_head_t wq2;
    wait_queue_init(&wq2);
    process_t *self = process_current();
    wait_queue_entry_t we = { .proc = self, .next = NULL };
    wait_queue_add(&wq2, &we);
    self->state = PROCESS_BLOCKED;
    wait_queue_wake_all(&wq2);
    if (self->state == PROCESS_READY)
        kprintf("[parity][K01] wake_all PASS\n");
    else
        kprintf("[parity][K01] wake_all FAIL\n");
    self->state = PROCESS_RUNNING;
    wait_queue_remove(&wq2, &we);
}
