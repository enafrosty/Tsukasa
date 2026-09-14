/*
 * Project Tsukasa — Task Control Block and process model
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

#ifndef TASK_H
#define TASK_H

#include <stdint.h>

/* Task states. */
#define TASK_READY   0
#define TASK_RUNNING 1
#define TASK_BLOCKED 2
#define TASK_DEAD    3

/* Task is user-mode (Ring 3). */
#define TASK_FLAG_USER (1u << 8)

#define TASK_STACK_SIZE 8192

/* Task Control Block. */
struct task {
    uint32_t pid;
    uint32_t esp;
    uintptr_t stack_base;
    uintptr_t page_dir;
    uint32_t flags;
    uint32_t user_eip;
    uint32_t user_esp;
    uint8_t state;
    struct task *next;
};

typedef struct task task_t;

void task_init(void);

/* Create a new kernel task. @param entry Entry point (function pointer). @return New task, or NULL on failure. */
task_t *task_create(void (*entry)(void));

/* Create a user-mode (Ring 3) task. */
task_t *task_create_user(uint32_t entry_addr, uint32_t stack_addr);

/* Get the currently running task. @return Current task, or NULL if none. */
task_t *task_current(void);

void task_set_current(task_t *t);

/* Add task to the ready queue. */
void task_ready(task_t *t);

/* Get the next ready task (round-robin). @return Next task, or NULL if none. */
task_t *task_next_ready(void);

/* Yield to the next ready task. */
void task_yield(void);

#endif /* TASK_H */
