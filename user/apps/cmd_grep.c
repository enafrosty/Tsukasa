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

static int contains(const char *line, const char *needle)
{
    size_t ln;
    size_t nn;
    if (!line || !needle)
        return 0;
    nn = strlen(needle);
    if (nn == 0)
        return 1;
    ln = strlen(line);
    if (nn > ln)
        return 0;
    for (size_t i = 0; i + nn <= ln; i++) {
        if (strncmp(line + i, needle, nn) == 0)
            return 1;
    }
    return 0;
}

static int cmd_grep_main(int argc, char **argv)
{
    char buf[4096];
    char *p;
    size_t len = 0;
    ssize_t n;
    const char *pattern = (argc > 1) ? argv[1] : "";

    while (len + 1 < sizeof(buf)) {
        n = read(0, buf + len, sizeof(buf) - len - 1);
        if (n <= 0)
            break;
        len += (size_t)n;
    }
    buf[len] = '\0';

    p = buf;
    while (*p) {
        char *line = p;
        while (*p && *p != '\n')
            p++;
        if (*p == '\n')
            *p++ = '\0';
        if (contains(line, pattern))
            dprintf(1, "%s\n", line);
    }
    return 0;
}

void app_cmd_grep_entry(void)
{
    _exit(app_run_main(cmd_grep_main));
}
