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

#include "man_pages.h"

static int cmd_help_main(int argc, char **argv)
{
    (void)argc;
    (void)argv;
    printf("Tsukasa userspace commands:\n");
    for (int i = 0; i < man_page_count(); i++)
        printf("  %s\n", g_man_pages[i].name);
    printf("Use 'man <command>' for details.\n");
    return 0;
}

void app_cmd_help_entry(void)
{
    _exit(app_run_main(cmd_help_main));
}
