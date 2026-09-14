/*
 * Project Tsukasa — SDK example: the first program built entirely outside
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
    static const char banner[] = "hello from the Tsukasa SDK (crt0 + clang/lld)\n";
    int ok = 1;

    int fd = open("/dev/tty0", TSK_O_WRONLY);
    if (fd < 0)
        ok = 0;
    else if (dup2(fd, 1) != 1)
        ok = 0;
    else if (write(1, banner, sizeof(banner) - 1) != (long)(sizeof(banner) - 1))
        ok = 0;

    if (argc != 1)
        ok = 0;
    if (!argv || !argv[0] || argv[0][0] != '/')
        ok = 0;
    if (argv[argc] != 0)
        ok = 0;

    return ok ? 42 : 13;
}
