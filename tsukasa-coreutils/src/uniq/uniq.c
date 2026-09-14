/*
 * Project Tsukasa — uniq Duplicate Line Filter Utility
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
#include <errno.h>

#define LINE_BUF_SIZE 4096

static int opt_count = 0;
static int opt_duplicates_only = 0;
static int opt_unique_only = 0;
static int opt_ignore_case = 0;

static int read_line(FILE *fp, char *buf, size_t max)
{
    size_t i = 0;
    int c;
    if (max == 0)
        return -1;
    while ((c = fgetc(fp)) != EOF) {
        if (c == '\r')
            continue;
        if (c == '\n') {
            buf[i] = '\0';
            return (int)i;
        }
        if (i + 1 < max)
            buf[i++] = (char)c;
    }
    if (i > 0) {
        buf[i] = '\0';
        return (int)i;
    }
    return -1;
}

static int lines_equal(const char *s1, const char *s2)
{
    if (opt_ignore_case)
        return (strcasecmp(s1, s2) == 0);
    return (strcmp(s1, s2) == 0);
}

static void output_line(FILE *out, const char *line, size_t count)
{
    if (opt_duplicates_only && count < 2)
        return;
    if (opt_unique_only && count != 1)
        return;

    if (opt_count)
        fprintf(out, "%7zu %s\n", count, line);
    else
        fprintf(out, "%s\n", line);
}

static void print_usage(const char *prog)
{
    fprintf(stderr, "Usage: %s [-c] [-d] [-u] [-i] [input [output]]\n", prog);
}

int main(int argc, char **argv)
{
    int arg_idx = 1;
    while (arg_idx < argc && argv[arg_idx][0] == '-' && argv[arg_idx][1] != '\0') {
        if (strcmp(argv[arg_idx], "--") == 0) {
            arg_idx++;
            break;
        }
        if (strcmp(argv[arg_idx], "-") == 0)
            break;

        char *flag = argv[arg_idx] + 1;
        while (*flag) {
            switch (*flag) {
            case 'c': opt_count = 1; break;
            case 'd': opt_duplicates_only = 1; break;
            case 'u': opt_unique_only = 1; break;
            case 'i': opt_ignore_case = 1; break;
            default:
                fprintf(stderr, "uniq: invalid option '-%c'\n", *flag);
                print_usage(argv[0]);
                return 1;
            }
            flag++;
        }
        arg_idx++;
    }

    FILE *in = stdin;
    FILE *out = stdout;

    if (arg_idx < argc && strcmp(argv[arg_idx], "-") != 0) {
        in = fopen(argv[arg_idx], "r");
        if (!in) {
            fprintf(stderr, "uniq: %s: %s\n", argv[arg_idx], strerror(errno));
            return 1;
        }
    }
    arg_idx++;

    if (arg_idx < argc && strcmp(argv[arg_idx], "-") != 0) {
        out = fopen(argv[arg_idx], "w");
        if (!out) {
            fprintf(stderr, "uniq: %s: %s\n", argv[arg_idx], strerror(errno));
            if (in != stdin)
                fclose(in);
            return 1;
        }
    }

    char prev_line[LINE_BUF_SIZE];
    char curr_line[LINE_BUF_SIZE];
    size_t count = 0;
    int has_prev = 0;

    while (read_line(in, curr_line, sizeof(curr_line)) >= 0) {
        if (!has_prev) {
            strcpy(prev_line, curr_line);
            count = 1;
            has_prev = 1;
            continue;
        }

        if (lines_equal(prev_line, curr_line)) {
            count++;
        } else {
            output_line(out, prev_line, count);
            strcpy(prev_line, curr_line);
            count = 1;
        }
    }

    if (has_prev)
        output_line(out, prev_line, count);

    if (in != stdin)
        fclose(in);
    if (out != stdout)
        fclose(out);

    return 0;
}
