/*
 * Project Tsukasa — Kernel Symbol Lookup Implementation
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

#include "include/ksymbols.h"

const char *ksym_lookup(unsigned long addr, unsigned long *off_out)
{
    if (ksym_count == 0 || addr < ksym_table[0].addr) {
        if (off_out)
            *off_out = 0;
        return NULL;
    }

    unsigned long low = 0;
    unsigned long high = ksym_count - 1;
    unsigned long best = 0;

    while (low <= high) {
        unsigned long mid = low + (high - low) / 2;
        if (ksym_table[mid].addr <= addr) {
            best = mid;
            low = mid + 1;
        } else {
            if (mid == 0)
                break;
            high = mid - 1;
        }
    }

    if (off_out)
        *off_out = addr - ksym_table[best].addr;

    return ksym_table[best].name;
}
