/*
 * Project Tsukasa — include "../include/app_runtime.h"
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

static int cmd_date_main(int argc, char **argv)
{
    time_t now;
    struct tm tmv;
    char out[64];
    (void)argc;
    (void)argv;

    now = time(0);
    if (now < 0) {
        dprintf(2, "date: failed to read RTC time\n");
        return 1;
    }
    if (!gmtime_r(&now, &tmv)) {
        dprintf(2, "date: failed to convert time\n");
        return 1;
    }
    if (!strftime(out, sizeof(out), "%Y-%m-%d %H:%M:%S UTC", &tmv)) {
        dprintf(2, "date: formatting failed\n");
        return 1;
    }
    dprintf(1, "%s\n", out);
    return 0;
}

void app_cmd_date_entry(void)
{
    _exit(app_run_main(cmd_date_main));
}
