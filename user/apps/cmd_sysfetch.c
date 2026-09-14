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

static void print_file_head(const char *path, int max_lines)
{
    int fd = open(path, O_RDONLY, 0);
    char buf[512];
    ssize_t n;
    int lines = 0;
    if (fd < 0) {
        dprintf(2, "sysfetch: missing %s\n", path);
        return;
    }
    while ((n = read(fd, buf, sizeof(buf))) > 0 && lines < max_lines) {
        for (ssize_t i = 0; i < n && lines < max_lines; i++) {
            char c = buf[i];
            putchar(c);
            if (c == '\n')
                lines++;
        }
    }
    close(fd);
}

static int cmd_sysfetch_main(int argc, char **argv)
{
    time_t now = time(0);
    struct tm t;
    char ts[64];
    (void)argc;
    (void)argv;

    if (now >= 0 && gmtime_r(&now, &t) &&
        strftime(ts, sizeof(ts), "%Y-%m-%d %H:%M:%S UTC", &t)) {
        dprintf(1, "Tsukasa Userspace\nTime: %s\n\n", ts);
    } else {
        dprintf(1, "Tsukasa Userspace\n\n");
    }

    dprintf(1, "-- /proc/self/status --\n");
    print_file_head("/proc/self/status", 16);
    dprintf(1, "\n-- /sys/memory --\n");
    print_file_head("/sys/memory", 18);
    dprintf(1, "\n-- /sys/net/status --\n");
    print_file_head("/sys/net/status", 12);
    return 0;
}

void app_cmd_sysfetch_entry(void)
{
    _exit(app_run_main(cmd_sysfetch_main));
}
