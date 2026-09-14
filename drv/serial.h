/*
 * Project Tsukasa — COM1 serial port driver (x86 port I/O)
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

#ifndef SERIAL_H
#define SERIAL_H

#include <stdint.h>

/* Initialise COM1 at 115200 baud, 8N1.  Must be called before serial_putc. */
void serial_init(void);

/* Transmit one character (blocks until THR is empty). */
void serial_putc(char c);

/* Transmit a NUL-terminated string. */
void serial_puts(const char *s);

#endif /* SERIAL_H */
