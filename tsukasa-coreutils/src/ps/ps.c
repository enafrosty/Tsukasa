/*
 * Project Tsukasa — ps Process Status Viewer Utility
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
#include <stdlib.h>
#include <string.h>
#include <errno.h>

static int opt_long = 0;

static void print_usage(const char *prog)
{
    fprintf(stderr, "Usage: %s [-a] [-e] [-l]\n", prog);
}

int main(int argc, char **argv)
{
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-l") == 0) {
            opt_long = 1;
        } else if (strcmp(argv[i], "-a") == 0 || strcmp(argv[i], "-e") == 0) {
            /* Standard POSIX show-all flags */
        } else {
            fprintf(stderr, "ps: invalid option '%s'\n", argv[i]);
            print_usage(argv[0]);
            return 1;
        }
    }

    FILE *fp = fopen("/proc/processes", "r");
    if (!fp) {
        fprintf(stderr, "ps: /proc/processes: %s\n", strerror(errno));
        return 1;
    }

    char line[512];
    int is_header = 1;

    if (opt_long)
        printf("%5s %5s %-10s %-16s %s\n", "PID", "PPID", "STATE", "CMD", "CWD");
    else
        printf("%5s %5s %-10s %s\n", "PID", "PPID", "STATE", "CMD");

    while (fgets(line, sizeof(line), fp)) {
        if (is_header) {
            is_header = 0;
            continue;
        }

        /* Parse: pid ppid state name cwd */
        long pid = 0;
        long ppid = 0;
        char state[32];
        char name[64];
        char cwd[256];

        state[0] = '\0';
        name[0] = '\0';
        cwd[0] = '\0';

        char *p = line;
        while (*p == ' ') p++;
        pid = strtol(p, &p, 10);

        while (*p == ' ') p++;
        ppid = strtol(p, &p, 10);

        while (*p == ' ') p++;
        char *s_start = p;
        while (*p && *p != ' ' && *p != '\n') p++;
        size_t s_len = (size_t)(p - s_start);
        if (s_len >= sizeof(state)) s_len = sizeof(state) - 1;
        memcpy(state, s_start, s_len);
        state[s_len] = '\0';

        while (*p == ' ') p++;
        char *n_start = p;
        while (*p && *p != ' ' && *p != '\n') p++;
        size_t n_len = (size_t)(p - n_start);
        if (n_len >= sizeof(name)) n_len = sizeof(name) - 1;
        memcpy(name, n_start, n_len);
        name[n_len] = '\0';

        while (*p == ' ') p++;
        char *c_start = p;
        while (*p && *p != '\n' && *p != '\r') p++;
        size_t c_len = (size_t)(p - c_start);
        if (c_len >= sizeof(cwd)) c_len = sizeof(cwd) - 1;
        memcpy(cwd, c_start, c_len);
        cwd[c_len] = '\0';

        if (opt_long)
            printf("%5ld %5ld %-10s %-16s %s\n", pid, ppid, state, name, cwd);
        else
            printf("%5ld %5ld %-10s %s\n", pid, ppid, state, name);
    }

    fclose(fp);
    return 0;
}
