/*
 * Project Tsukasa — poweroff System ACPI Poweroff Utility
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
#include <string.h>
#include <unistd.h>
#include <errno.h>

#define REBOOT_CMD_POWER_OFF 0x4321fedc
#define REBOOT_CMD_RESTART   0x01234567

int main(int argc, char **argv)
{
    int do_reboot = 0;
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-r") == 0 || strcmp(argv[i], "--reboot") == 0) {
            do_reboot = 1;
        } else {
            fprintf(stderr, "poweroff: unrecognized option '%s'\n", argv[i]);
            return 1;
        }
    }

    if (do_reboot) {
        printf("poweroff: restarting system...\n");
        reboot(REBOOT_CMD_RESTART);
    } else {
        printf("poweroff: powering off system...\n");
        reboot(REBOOT_CMD_POWER_OFF);
    }

    fprintf(stderr, "poweroff: failed to trigger shutdown: %s\n", strerror(errno));
    return 1;
}
