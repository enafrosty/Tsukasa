/*
 * Project Tsukasa — date Calendar Date and Time Utility
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
#include <time.h>
#include <errno.h>

static void print_usage(const char *prog)
{
    fprintf(stderr, "Usage: %s [-u] [+format]\n", prog);
}

int main(int argc, char **argv)
{
    const char *fmt = "%a %b %e %H:%M:%S UTC %Y";

    for (int i = 1; i < argc; i++) {
        if (argv[i][0] == '+') {
            fmt = argv[i] + 1;
        } else if (strcmp(argv[i], "-u") == 0 || strcmp(argv[i], "--utc") == 0) {
            /* UTC is default */
        } else {
            fprintf(stderr, "date: invalid option '%s'\n", argv[i]);
            print_usage(argv[0]);
            return 1;
        }
    }

    time_t now = time(NULL);
    if (now < 0) {
        perror("date");
        return 1;
    }

    struct tm tmv;
    if (!gmtime_r(&now, &tmv)) {
        fprintf(stderr, "date: failed to convert calendar time\n");
        return 1;
    }

    char out[256];
    if (strftime(out, sizeof(out), fmt, &tmv) == 0) {
        fprintf(stderr, "date: failed to format date\n");
        return 1;
    }

    printf("%s\n", out);
    return 0;
}
