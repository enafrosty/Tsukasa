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

static int cmd_pwd_main(int argc, char **argv)
{
    char cwd[256];
    (void)argc;
    (void)argv;
    if (!getcwd(cwd, sizeof(cwd))) {
        dprintf(2, "pwd: failed to get cwd\n");
        return 1;
    }
    dprintf(1, "%s\n", cwd);
    return 0;
}

void app_cmd_pwd_entry(void)
{
    _exit(app_run_main(cmd_pwd_main));
}
