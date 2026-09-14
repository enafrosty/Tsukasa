/*
 * Project Tsukasa - Kernel wait queue and poll infrastructure
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

#ifndef WAIT_QUEUE_H
#define WAIT_QUEUE_H

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
#include "include/spinlock.h"

struct process;

typedef struct wait_queue_entry {
    struct process *proc;
    struct wait_queue_entry *next;
} wait_queue_entry_t;

struct pollfd {
    int fd;
    short events;
    short revents;
};

typedef struct {
    wait_queue_entry_t *head;
    spinlock_t lock;
} wait_queue_head_t;

// Forward declaration of poll_table
struct poll_table;

typedef void (*poll_queue_proc)(wait_queue_head_t *h, struct poll_table *pt);

typedef struct poll_table {
    poll_queue_proc qproc;
} poll_table_t;

void wait_queue_init(wait_queue_head_t *h);
void wait_queue_add(wait_queue_head_t *h, wait_queue_entry_t *entry);
void wait_queue_remove(wait_queue_head_t *h, wait_queue_entry_t *entry);
void wait_queue_wake_all(wait_queue_head_t *h);
void wait_queue_prepare_to_wait(wait_queue_head_t *h, wait_queue_entry_t *entry);
void wait_queue_finish_wait(wait_queue_head_t *h, wait_queue_entry_t *entry);

#define POLLIN      0x0001
#define POLLOUT     0x0004
#define POLLERR     0x0008
#define POLLHUP     0x0010
#define POLLNVAL    0x0020

#define MAX_POLL_ENTRIES 32

typedef struct {
    wait_queue_head_t *h;
    wait_queue_entry_t entry;
} poll_entry_t;

typedef struct {
    poll_table_t pt;
    poll_entry_t entries[MAX_POLL_ENTRIES];
    int count;
} poll_wtable_t;

void poll_wtable_init(poll_wtable_t *wt, struct process *proc);
void poll_wtable_unregister_all(poll_wtable_t *wt);
void poll_wtable_queue(wait_queue_head_t *h, poll_table_t *pt);

void wait_queue_run_selftests(void);

#endif
