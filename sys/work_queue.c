/*
 * Project Tsukasa — Kernel work queue implementation
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
#include <stdbool.h>
#include "sys/work_queue.h"
#include "include/kprintf.h"
#include "include/smp.h"
#include "proc/process.h"

#define WORK_QUEUE_SIZE 256

static work_item_t work_queue[WORK_QUEUE_SIZE];
static volatile int wq_head = 0;
static volatile int wq_tail = 0;
static spinlock_t wq_lock = SPINLOCK_INIT;

void work_queue_submit(work_fn_t fn, void *arg) {
    if (!fn) return;

    spin_lock(&wq_lock);
    int next_tail = (wq_tail + 1) % WORK_QUEUE_SIZE;
    if (next_tail == wq_head) {
        spin_unlock(&wq_lock);
        kprintf("[work_queue] overflow, item dropped\n");
        return;
    }
    work_queue[wq_tail].fn = fn;
    work_queue[wq_tail].arg = arg;
    wq_tail = next_tail;
    spin_unlock(&wq_lock);
}

bool work_queue_drain_one(void) {
    spin_lock(&wq_lock);
    if (wq_head == wq_tail) {
        spin_unlock(&wq_lock);
        return false;
    }
    work_item_t item = work_queue[wq_head];
    wq_head = (wq_head + 1) % WORK_QUEUE_SIZE;
    spin_unlock(&wq_lock);

    if (item.fn) {
        item.fn(item.arg);
    }
    return true;
}

void work_queue_drain_loop(void) {
    while (1) {
        process_idle_loop_note();

        while (work_queue_drain_one()) {
        }

        asm volatile("sti; hlt");
    }
}

typedef struct {
    volatile int counter;
    volatile uint32_t last_cpu;
} wq_test_state_t;

static void wq_test_increment(void *arg) {
    wq_test_state_t *st = (wq_test_state_t *)arg;
    __atomic_fetch_add(&st->counter, 1, __ATOMIC_SEQ_CST);
    st->last_cpu = smp_this_cpu_id();
}

void work_queue_run_selftests(void) {
    static wq_test_state_t st;
    st.counter = 0;
    st.last_cpu = 0;

    for (int i = 0; i < 1000; i++) {
        work_queue_submit(wq_test_increment, &st);
        if ((i % 128) == 127) {
            while (work_queue_drain_one())
                ;
        }
    }
    while (work_queue_drain_one())
        ;
    for (uint64_t spins = 0; st.counter < 1000 && spins < 100000000ULL; spins++)
        __asm__ volatile ("pause");

    if (st.counter == 1000)
        kprintf("[parity][K02] work_queue 1000 items PASS (last_cpu=%u ncpu=%u)\n",
                st.last_cpu, smp_cpu_count());
    else
        kprintf("[parity][K02] work_queue 1000 items FAIL (counter=%d)\n", st.counter);
}
