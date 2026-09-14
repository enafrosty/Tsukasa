/*
 * Project Tsukasa — argvchk: guide-05 milestone-3 acceptance binary
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

static int str_eq(const char *a, const char *b)
{
    if (!a || !b)
        return 0;
    while (*a && *a == *b) {
        a++;
        b++;
    }
    return *a == *b;
}

int main(int argc, char **argv)
{
    if (argc != 4)
        return 13;
    if (!argv || argv[argc] != 0)
        return 13;
    if (!str_eq(argv[0], "argvchk"))
        return 13;
    if (!str_eq(argv[1], "alpha"))
        return 13;
    if (!str_eq(argv[2], "beta"))
        return 13;
    if (!str_eq(argv[3], "gamma"))
        return 13;
    return 42;
}
