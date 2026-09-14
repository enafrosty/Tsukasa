/*
 * Project Tsukasa — kill Process Signal Transmission Utility
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
#include <ctype.h>
#include <unistd.h>
#include <errno.h>

struct sig_entry {
    int num;
    const char *name;
};

static const struct sig_entry s_signals[] = {
    { 1,  "HUP" },
    { 2,  "INT" },
    { 3,  "QUIT" },
    { 6,  "ABRT" },
    { 9,  "KILL" },
    { 11, "SEGV" },
    { 14, "ALRM" },
    { 15, "TERM" },
    { 17, "CHLD" },
    { 18, "CONT" },
    { 19, "STOP" },
    { 20, "TSTP" },
};

static int parse_signal(const char *s)
{
    if (strncmp(s, "SIG", 3) == 0)
        s += 3;
    if (isdigit((unsigned char)*s))
        return atoi(s);

    for (size_t i = 0; i < sizeof(s_signals) / sizeof(s_signals[0]); i++) {
        if (strcasecmp(s, s_signals[i].name) == 0)
            return s_signals[i].num;
    }
    return -1;
}

static void list_signals(void)
{
    for (size_t i = 0; i < sizeof(s_signals) / sizeof(s_signals[0]); i++) {
        printf("%2d) SIG%-5s%s", s_signals[i].num, s_signals[i].name,
               ((i + 1) % 4 == 0 || i + 1 == sizeof(s_signals) / sizeof(s_signals[0])) ? "\n" : "  ");
    }
}

static void print_usage(const char *prog)
{
    fprintf(stderr, "Usage: %s [-s sig | -sig] <pid>...\n", prog);
    fprintf(stderr, "       %s -l\n", prog);
}

int main(int argc, char **argv)
{
    if (argc < 2) {
        print_usage(argv[0]);
        return 1;
    }

    int sig = 15; /* SIGTERM by default */
    int arg_idx = 1;

    while (arg_idx < argc && argv[arg_idx][0] == '-') {
        if (strcmp(argv[arg_idx], "--") == 0) {
            arg_idx++;
            break;
        }
        if (strcmp(argv[arg_idx], "-l") == 0) {
            list_signals();
            return 0;
        }
        if (strcmp(argv[arg_idx], "-s") == 0) {
            arg_idx++;
            if (arg_idx >= argc) {
                fprintf(stderr, "kill: option '-s' requires an argument\n");
                return 1;
            }
            sig = parse_signal(argv[arg_idx]);
            if (sig <= 0) {
                fprintf(stderr, "kill: unknown signal '%s'\n", argv[arg_idx]);
                return 1;
            }
            arg_idx++;
            continue;
        }

        /* Support -<num> or -<name> */
        sig = parse_signal(argv[arg_idx] + 1);
        if (sig <= 0) {
            fprintf(stderr, "kill: invalid signal '%s'\n", argv[arg_idx]);
            return 1;
        }
        arg_idx++;
        break;
    }

    if (arg_idx >= argc) {
        print_usage(argv[0]);
        return 1;
    }

    int had_error = 0;
    for (int i = arg_idx; i < argc; i++) {
        pid_t pid = (pid_t)atoi(argv[i]);
        if (pid <= 0 && strcmp(argv[i], "0") != 0) {
            fprintf(stderr, "kill: invalid process id '%s'\n", argv[i]);
            had_error = 1;
            continue;
        }

        if (kill(pid, sig) < 0) {
            fprintf(stderr, "kill: (%d): %s\n", (int)pid, strerror(errno));
            had_error = 1;
        }
    }

    return had_error ? 1 : 0;
}
