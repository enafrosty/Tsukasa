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

static int cmd_man_main(int argc, char **argv)
{
    if (argc < 2) {
        dprintf(2, "man: usage: man <command>\n");
        return 1;
    }
    for (int i = 0; i < man_page_count(); i++) {
        if (strcmp(argv[1], g_man_pages[i].name) == 0) {
            dprintf(1, "%s", g_man_pages[i].text);
            return 0;
        }
    }
    dprintf(2, "man: no manual entry for %s\n", argv[1]);
    return 1;
}

void app_cmd_man_entry(void)
{
    _exit(app_run_main(cmd_man_main));
}
