/*
 * Project Tsukasa — Bounds-checked VGA output. All writes are capped to current row size
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

#include "vga.h"

unsigned int vga_puts_row(unsigned int row, const char *str)
{
    unsigned int i = 0;
    unsigned int offset;

    if (row >= VGA_HEIGHT)
        return 0;

    offset = row * VGA_WIDTH;

    while (str[i] != '\0' && i < VGA_WIDTH) {
        VGA_BUFFER[offset + i] = (VGA_ATTR << 8) | (unsigned char)str[i];
        i++;
    }
    return i;
}
