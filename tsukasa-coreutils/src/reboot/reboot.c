/*
 * Project Tsukasa — reboot System Restart Utility
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

#include <stdio.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>

#define REBOOT_CMD_RESTART 0x01234567

int main(int argc, char **argv)
{
    (void)argc;
    (void)argv;

    printf("reboot: restarting system...\n");
    reboot(REBOOT_CMD_RESTART);

    fprintf(stderr, "reboot: failed to trigger restart: %s\n", strerror(errno));
    return 1;
}
