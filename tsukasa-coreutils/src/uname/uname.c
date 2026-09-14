/*
 * Project Tsukasa — uname System Identification Utility
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

#define SYS_NAME    "Tsukasa"
#define NODE_NAME   "localhost"
#define RELEASE     "1.0"
#define VERSION     "Tsukasa 1.0-alpha"
#define MACHINE     "x86_64"
#define OS_NAME     "GNU/Tsukasa"

static void print_usage(const char *prog)
{
    fprintf(stderr, "Usage: %s [-a] [-s] [-n] [-r] [-v] [-m] [-o]\n", prog);
}

int main(int argc, char **argv)
{
    int opt_s = 0;
    int opt_n = 0;
    int opt_r = 0;
    int opt_v = 0;
    int opt_m = 0;
    int opt_o = 0;

    for (int i = 1; i < argc; i++) {
        if (argv[i][0] != '-') {
            print_usage(argv[0]);
            return 1;
        }
        if (strcmp(argv[i], "--") == 0)
            break;

        char *p = argv[i] + 1;
        while (*p) {
            switch (*p) {
            case 'a':
                opt_s = opt_n = opt_r = opt_v = opt_m = opt_o = 1;
                break;
            case 's': opt_s = 1; break;
            case 'n': opt_n = 1; break;
            case 'r': opt_r = 1; break;
            case 'v': opt_v = 1; break;
            case 'm': opt_m = 1; break;
            case 'o': opt_o = 1; break;
            default:
                fprintf(stderr, "uname: invalid option '-%c'\n", *p);
                print_usage(argv[0]);
                return 1;
            }
            p++;
        }
    }

    if (!opt_s && !opt_n && !opt_r && !opt_v && !opt_m && !opt_o)
        opt_s = 1;

    int printed = 0;
    if (opt_s) {
        if (printed) putchar(' ');
        fputs(SYS_NAME, stdout);
        printed = 1;
    }
    if (opt_n) {
        if (printed) putchar(' ');
        fputs(NODE_NAME, stdout);
        printed = 1;
    }
    if (opt_r) {
        if (printed) putchar(' ');
        fputs(RELEASE, stdout);
        printed = 1;
    }
    if (opt_v) {
        if (printed) putchar(' ');
        fputs(VERSION, stdout);
        printed = 1;
    }
    if (opt_m) {
        if (printed) putchar(' ');
        fputs(MACHINE, stdout);
        printed = 1;
    }
    if (opt_o) {
        if (printed) putchar(' ');
        fputs(OS_NAME, stdout);
        printed = 1;
    }

    putchar('\n');
    return 0;
}
