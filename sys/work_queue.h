/*
 * Project Tsukasa — Kernel work queue for task offloading
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

#ifndef WORK_QUEUE_H
#define WORK_QUEUE_H

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
#include "include/spinlock.h"

// A simple work queue for offloading tasks to idle AP cores.
// Producer (BSP or any core) calls work_queue_submit().
// Consumer (AP idle loops) calls work_queue_drain_loop().

typedef void (*work_fn_t)(void *arg);

typedef struct {
    work_fn_t fn;
    void *arg;
} work_item_t;

// Submit a work item. Thread-safe (uses spinlock internally).
void work_queue_submit(work_fn_t fn, void *arg);

// Drain and execute all pending work items, then hlt until more arrive.
// Called from AP idle loops. Never returns.
void work_queue_drain_loop(void);

// Drain one item (if available). Returns true if work was done.
// Useful for BSP to optionally help if idle.
bool work_queue_drain_one(void);

void work_queue_run_selftests(void);

#endif
