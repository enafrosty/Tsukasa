/*
 * Project Tsukasa — Scheduler interface
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

#ifndef SCHEDULER_H
#define SCHEDULER_H

#include <stdint.h>

/* Enter the scheduler and start multitasking. Never returns. */
void scheduler_run(void);

#ifdef __x86_64__
uint64_t scheduler_tick(uint64_t current_rsp);
void scheduler_init(void);
#endif

#endif /* SCHEDULER_H */
