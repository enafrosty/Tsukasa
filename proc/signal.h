/*
 * Project Tsukasa — Minimal signal interface for Process Phase 2
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

#ifndef TSUKASA_SIGNAL_H
#define TSUKASA_SIGNAL_H

#include <stdint.h>

#include "process.h"

#define SIG_BLOCK   0
#define SIG_UNBLOCK 1
#define SIG_SETMASK 2

int signal_register(int sig, process_signal_handler_t handler);
int signal_send(int pid, int sig);
int signal_mask(int how, uint64_t set, uint64_t *old_set);
int signal_pending(uint64_t *pending_out);

#endif /* TSUKASA_SIGNAL_H */
