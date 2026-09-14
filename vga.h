/*
 * Project Tsukasa — VGA text mode constants and safe output helpers
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

#ifndef VGA_H
#define VGA_H

#include <stdint.h>

#define VGA_WIDTH  80
#define VGA_HEIGHT 25

/* VGA text buffer base (physical address, identity-mapped by bootloader). */
#define VGA_BUFFER ((volatile unsigned short *)0xB8000)

#define VGA_ATTR 0x07

/* Write string to a given row, bounded to VGA_WIDTH. */
unsigned int vga_puts_row(unsigned int row, const char *str);

#endif /* VGA_H */
