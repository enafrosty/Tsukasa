/*
 * Project Tsukasa — Kernel Serial GDB Stub Interface
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

#ifndef TSUKASA_GDBSTUB_H
#define TSUKASA_GDBSTUB_H

#include <stdint.h>
#include <stdbool.h>
#include "sys/panic.h"

#define GDB_MAX_BREAKPOINTS 16

struct gdb_breakpoint {
    unsigned long addr;
    unsigned char orig_byte;
    int           active;
};

/* Initialise the stub on the given UART base. Does not stop execution. */
void gdbstub_init(uint16_t uart_base);

/* Enter the stub's packet loop. Called from the exception path.
 * Blocks, servicing packets, until GDB says continue or step. */
void gdbstub_trap(interrupt_frame_t *regs, int signo);

/* Force a trap at the next opportunity (for an explicit breakpoint in code). */
void gdbstub_breakpoint(void);

/* Check if the stub has been initialized. */
bool gdbstub_is_enabled(void);

/* Check if an active GDB software breakpoint is installed at addr. */
bool gdbstub_has_breakpoint_at(unsigned long addr);

#endif /* TSUKASA_GDBSTUB_H */
