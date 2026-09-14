/*
 * Project Tsukasa — Freestanding kernel printf
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

#ifndef KPRINTF_H
#define KPRINTF_H

#include <stddef.h>
#include <stdarg.h>

/* Print a formatted string to the serial port. Returns the number of characters written. */
int kprintf(const char *fmt, ...);

/* Format a string into buf (at most n bytes including NUL terminator). */
int ksprintf(char *buf, size_t n, const char *fmt, ...);

/* Bare string output to serial (no formatting). */
void kputs(const char *s);

#endif /* KPRINTF_H */
