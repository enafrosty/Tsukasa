/*
 * Project Tsukasa — grep Pattern Matching Utility
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

static int opt_ignore_case = 0;
static int opt_line_number = 0;
static int opt_invert_match = 0;
static int opt_count_only = 0;
static int opt_list_files = 0;

static int match_here(const char *line, const char *pat, int ignore_case)
{
    if (*pat == '\0')
        return 1;
    if (*pat == '$' && *(pat + 1) == '\0')
        return (*line == '\0');
    if (*line == '\0')
        return 0;
    if (*pat == '.')
        return match_here(line + 1, pat + 1, ignore_case);

    char c1 = *line;
    char c2 = *pat;
    if (ignore_case) {
        c1 = (char)tolower((unsigned char)c1);
        c2 = (char)tolower((unsigned char)c2);
    }
    if (c1 == c2)
        return match_here(line + 1, pat + 1, ignore_case);

    return 0;
}

static int line_matches(const char *line, const char *pat, int ignore_case)
{
    if (*pat == '^')
        return match_here(line, pat + 1, ignore_case);

    const char *p = line;
    do {
        if (match_here(p, pat, ignore_case))
            return 1;
    } while (*p++);

    return 0;
}

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

static size_t grep_file(FILE *fp, const char *filename, const char *pattern, int print_filename)
{
    char buf[LINE_BUF_SIZE];
    size_t line_no = 0;
    size_t match_count = 0;

    while (read_line(fp, buf, sizeof(buf)) >= 0) {
        line_no++;
        int m = line_matches(buf, pattern, opt_ignore_case);
        if (opt_invert_match)
            m = !m;

        if (m) {
            match_count++;
            if (opt_list_files) {
                printf("%s\n", filename);
                return match_count;
            }
            if (!opt_count_only) {
                if (print_filename)
                    printf("%s:", filename);
                if (opt_line_number)
                    printf("%zu:", line_no);
                printf("%s\n", buf);
            }
        }
    }

    if (opt_count_only) {
        if (print_filename)
            printf("%s:%zu\n", filename, match_count);
        else
            printf("%zu\n", match_count);
    }

    return match_count;
}

static void print_usage(const char *prog)
{
    fprintf(stderr, "Usage: %s [-i] [-n] [-v] [-c] [-l] <pattern> [file...]\n", prog);
}

int main(int argc, char **argv)
{
    int arg_idx = 1;
    while (arg_idx < argc && argv[arg_idx][0] == '-' && argv[arg_idx][1] != '\0') {
        if (strcmp(argv[arg_idx], "--") == 0) {
            arg_idx++;
            break;
        }
        char *flag = argv[arg_idx] + 1;
        while (*flag) {
            switch (*flag) {
            case 'i': opt_ignore_case = 1; break;
            case 'n': opt_line_number = 1; break;
            case 'v': opt_invert_match = 1; break;
            case 'c': opt_count_only = 1; break;
            case 'l': opt_list_files = 1; break;
            default:
                fprintf(stderr, "grep: invalid option '-%c'\n", *flag);
                print_usage(argv[0]);
                return 2;
            }
            flag++;
        }
        arg_idx++;
    }

    if (arg_idx >= argc) {
        print_usage(argv[0]);
        return 2;
    }

    const char *pattern = argv[arg_idx++];
    int num_files = argc - arg_idx;
    size_t total_matches = 0;
    int had_error = 0;

    if (num_files == 0) {
        total_matches += grep_file(stdin, "(standard input)", pattern, 0);
    } else {
        int print_fn = (num_files > 1 && !opt_list_files);
        for (int i = arg_idx; i < argc; i++) {
            const char *fn = argv[i];
            FILE *fp;
            if (strcmp(fn, "-") == 0) {
                fp = stdin;
                fn = "(standard input)";
            } else {
                fp = fopen(fn, "r");
            }

            if (!fp) {
                fprintf(stderr, "grep: %s: %s\n", fn, strerror(errno));
                had_error = 1;
                continue;
            }

            total_matches += grep_file(fp, fn, pattern, print_fn);
            if (fp != stdin)
                fclose(fp);
        }
    }

    if (had_error)
        return 2;
    return (total_matches > 0) ? 0 : 1;
}
