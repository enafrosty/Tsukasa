/*
 * Project Tsukasa — /bin/poweroff: ACPI S5 shutdown command (guide 07)
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

#include "user/include/app_runtime.h"

#include "../include/app_runtime.h"
#include "../include/stdio.h"
#include "../include/stdlib.h"

void acpi_power_off(void) __attribute__((noreturn));

static int cmd_poweroff_main(int argc, char **argv)
{
    (void)argc;
    (void)argv;
    dprintf(1, "powering off via ACPI _S5_...\n");
    acpi_power_off();
}

void app_cmd_poweroff_entry(void)
{
    _exit(app_run_main(cmd_poweroff_main));
}
