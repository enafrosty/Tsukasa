/*
 * Project Tsukasa — echo: the first coreutil migrated out of the kernel
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

#include "tsukasa_sdk.h"

int main(int argc, char **argv)
{
    for (int i = 1; i < argc; i++) {
        if (i > 1)
            putchar(' ');
        dprintf(1, "%s", argv[i]);
    }
    putchar('\n');
    return 0;
}
