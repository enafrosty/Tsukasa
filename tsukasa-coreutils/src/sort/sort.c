/*
 * Project Tsukasa — sort Text Line Sorting Utility
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

static int opt_reverse = 0;
static int opt_numeric = 0;
static int opt_unique = 0;
static const char *opt_output = NULL;

static char *read_line_alloc(FILE *fp)
{
    size_t cap = 128;
    size_t len = 0;
    char *buf = (char *)malloc(cap);
    if (!buf)
        return NULL;

    int c;
    while ((c = fgetc(fp)) != EOF) {
        if (c == '\r')
            continue;
        if (c == '\n') {
            buf[len] = '\0';
            return buf;
        }
        if (len + 1 >= cap) {
            cap *= 2;
            char *new_buf = (char *)realloc(buf, cap);
            if (!new_buf) {
                free(buf);
                return NULL;
            }
            buf = new_buf;
        }
        buf[len++] = (char)c;
    }

    if (len > 0) {
        buf[len] = '\0';
        return buf;
    }

    free(buf);
    return NULL;
}

static int compare_lines(const void *a, const void *b)
{
    const char *s1 = *(const char **)a;
    const char *s2 = *(const char **)b;
    int res = 0;

    if (opt_numeric) {
        long n1 = strtol(s1, NULL, 10);
        long n2 = strtol(s2, NULL, 10);
        if (n1 < n2)
            res = -1;
        else if (n1 > n2)
            res = 1;
        else
            res = strcmp(s1, s2);
    } else {
        res = strcmp(s1, s2);
    }

    return opt_reverse ? -res : res;
}

static int lines_equal(const char *s1, const char *s2)
{
    if (opt_numeric) {
        long n1 = strtol(s1, NULL, 10);
        long n2 = strtol(s2, NULL, 10);
        if (n1 == n2)
            return (strcmp(s1, s2) == 0);
        return 0;
    }
    return (strcmp(s1, s2) == 0);
}

static void print_usage(const char *prog)
{
    fprintf(stderr, "Usage: %s [-r] [-n] [-u] [-o outfile] [file...]\n", prog);
}

int main(int argc, char **argv)
{
    int arg_idx = 1;
    while (arg_idx < argc && argv[arg_idx][0] == '-' && argv[arg_idx][1] != '\0') {
        if (strcmp(argv[arg_idx], "--") == 0) {
            arg_idx++;
            break;
        }
        char *arg = argv[arg_idx];
        if (strcmp(arg, "-o") == 0) {
            arg_idx++;
            if (arg_idx >= argc) {
                fprintf(stderr, "sort: option '-o' requires an argument\n");
                return 1;
            }
            opt_output = argv[arg_idx];
        } else if (strncmp(arg, "-o", 2) == 0 && arg[2] != '\0') {
            opt_output = arg + 2;
        } else {
            char *flag = arg + 1;
            while (*flag) {
                switch (*flag) {
                case 'r': opt_reverse = 1; break;
                case 'n': opt_numeric = 1; break;
                case 'u': opt_unique = 1; break;
                default:
                    fprintf(stderr, "sort: invalid option '-%c'\n", *flag);
                    print_usage(argv[0]);
                    return 1;
                }
                flag++;
            }
        }
        arg_idx++;
    }

    size_t line_cap = 256;
    size_t line_count = 0;
    char **lines = (char **)malloc(line_cap * sizeof(char *));
    if (!lines) {
        perror("sort");
        return 1;
    }

    int num_files = argc - arg_idx;
    int had_error = 0;

    for (int i = (num_files == 0 ? -1 : arg_idx); i < argc; i++) {
        FILE *fp;
        const char *fn;
        if (i == -1) {
            fp = stdin;
            fn = "-";
        } else {
            fn = argv[i];
            if (strcmp(fn, "-") == 0)
                fp = stdin;
            else
                fp = fopen(fn, "r");
        }

        if (!fp) {
            fprintf(stderr, "sort: %s: %s\n", fn, strerror(errno));
            had_error = 1;
            continue;
        }

        char *line;
        while ((line = read_line_alloc(fp)) != NULL) {
            if (line_count >= line_cap) {
                line_cap *= 2;
                char **new_lines = (char **)realloc(lines, line_cap * sizeof(char *));
                if (!new_lines) {
                    perror("sort");
                    free(line);
                    if (fp != stdin)
                        fclose(fp);
                    for (size_t k = 0; k < line_count; k++)
                        free(lines[k]);
                    free(lines);
                    return 1;
                }
                lines = new_lines;
            }
            lines[line_count++] = line;
        }

        if (fp != stdin)
            fclose(fp);
        if (i == -1)
            break;
    }

    if (had_error && line_count == 0) {
        free(lines);
        return 1;
    }

    if (line_count > 1)
        qsort(lines, line_count, sizeof(char *), compare_lines);

    FILE *out = stdout;
    if (opt_output) {
        out = fopen(opt_output, "w");
        if (!out) {
            fprintf(stderr, "sort: %s: %s\n", opt_output, strerror(errno));
            for (size_t k = 0; k < line_count; k++)
                free(lines[k]);
            free(lines);
            return 1;
        }
    }

    const char *prev = NULL;
    for (size_t i = 0; i < line_count; i++) {
        if (opt_unique && prev && lines_equal(prev, lines[i]))
            continue;

        fputs(lines[i], out);
        fputc('\n', out);
        prev = lines[i];
    }

    if (opt_output)
        fclose(out);

    for (size_t k = 0; k < line_count; k++)
        free(lines[k]);
    free(lines);

    return had_error ? 1 : 0;
}
