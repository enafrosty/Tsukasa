/*
 * Project Tsukasa — cut Column Extraction Utility
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
#define MAX_RANGES    128

typedef struct {
    int start;
    int end; /* 0 means open-ended to end of line */
} cut_range_t;

static cut_range_t g_ranges[MAX_RANGES];
static size_t g_range_count = 0;
static char g_delim = '\t';
static int g_mode_fields = 0;
static int g_mode_chars = 0;

static int parse_ranges(const char *spec)
{
    const char *p = spec;
    g_range_count = 0;

    while (*p) {
        while (*p == ' ' || *p == '\t')
            p++;
        if (*p == '\0')
            break;

        if (g_range_count >= MAX_RANGES) {
            fprintf(stderr, "cut: too many ranges specified\n");
            return -1;
        }

        int start = 0;
        int end = 0;

        if (*p == '-') {
            p++;
            start = 1;
            if (!isdigit((unsigned char)*p)) {
                fprintf(stderr, "cut: invalid range '-%s'\n", p);
                return -1;
            }
            end = atoi(p);
            while (isdigit((unsigned char)*p))
                p++;
        } else if (isdigit((unsigned char)*p)) {
            start = atoi(p);
            while (isdigit((unsigned char)*p))
                p++;
            if (*p == '-') {
                p++;
                if (isdigit((unsigned char)*p)) {
                    end = atoi(p);
                    while (isdigit((unsigned char)*p))
                        p++;
                } else {
                    end = 0;
                }
            } else {
                end = start;
            }
        } else {
            fprintf(stderr, "cut: invalid byte or field list\n");
            return -1;
        }

        if (start <= 0 || (end > 0 && end < start)) {
            fprintf(stderr, "cut: invalid range bounds\n");
            return -1;
        }

        g_ranges[g_range_count].start = start;
        g_ranges[g_range_count].end = end;
        g_range_count++;

        if (*p == ',')
            p++;
        else if (*p != '\0' && *p != ' ' && *p != '\t') {
            fprintf(stderr, "cut: unexpected character '%c' in range list\n", *p);
            return -1;
        }
    }

    return (g_range_count > 0) ? 0 : -1;
}

static int in_ranges(int index)
{
    for (size_t i = 0; i < g_range_count; i++) {
        if (index >= g_ranges[i].start) {
            if (g_ranges[i].end == 0 || index <= g_ranges[i].end)
                return 1;
        }
    }
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

static void cut_line_fields(const char *line, FILE *out)
{
    if (!strchr(line, g_delim)) {
        fputs(line, out);
        fputc('\n', out);
        return;
    }

    int field_idx = 1;
    const char *p = line;
    int printed_field = 0;

    while (*p) {
        const char *start = p;
        while (*p && *p != g_delim)
            p++;

        size_t len = (size_t)(p - start);
        if (in_ranges(field_idx)) {
            if (printed_field)
                fputc(g_delim, out);
            fwrite(start, 1, len, out);
            printed_field = 1;
        }

        field_idx++;
        if (*p == g_delim) {
            p++;
            if (*p == '\0' && in_ranges(field_idx)) {
                if (printed_field)
                    fputc(g_delim, out);
                printed_field = 1;
            }
        }
    }

    fputc('\n', out);
}

static void cut_line_chars(const char *line, FILE *out)
{
    size_t len = strlen(line);
    for (size_t i = 0; i < len; i++) {
        int pos = (int)(i + 1);
        if (in_ranges(pos))
            fputc(line[i], out);
    }
    fputc('\n', out);
}

static void process_file(FILE *fp, FILE *out)
{
    char buf[LINE_BUF_SIZE];
    while (read_line(fp, buf, sizeof(buf)) >= 0) {
        if (g_mode_fields)
            cut_line_fields(buf, out);
        else if (g_mode_chars)
            cut_line_chars(buf, out);
    }
}

static void print_usage(const char *prog)
{
    fprintf(stderr, "Usage: %s [-d delim] (-f list | -c list) [file...]\n", prog);
}

int main(int argc, char **argv)
{
    const char *range_str = NULL;
    int arg_idx = 1;

    while (arg_idx < argc && argv[arg_idx][0] == '-' && argv[arg_idx][1] != '\0') {
        if (strcmp(argv[arg_idx], "--") == 0) {
            arg_idx++;
            break;
        }
        if (strcmp(argv[arg_idx], "-") == 0)
            break;

        char *arg = argv[arg_idx];
        if (strcmp(arg, "-d") == 0) {
            arg_idx++;
            if (arg_idx >= argc) {
                fprintf(stderr, "cut: option '-d' requires an argument\n");
                return 1;
            }
            g_delim = argv[arg_idx][0];
        } else if (strncmp(arg, "-d", 2) == 0 && arg[2] != '\0') {
            g_delim = arg[2];
        } else if (strcmp(arg, "-f") == 0) {
            arg_idx++;
            if (arg_idx >= argc) {
                fprintf(stderr, "cut: option '-f' requires an argument\n");
                return 1;
            }
            g_mode_fields = 1;
            range_str = argv[arg_idx];
        } else if (strncmp(arg, "-f", 2) == 0 && arg[2] != '\0') {
            g_mode_fields = 1;
            range_str = arg + 2;
        } else if (strcmp(arg, "-c") == 0) {
            arg_idx++;
            if (arg_idx >= argc) {
                fprintf(stderr, "cut: option '-c' requires an argument\n");
                return 1;
            }
            g_mode_chars = 1;
            range_str = argv[arg_idx];
        } else if (strncmp(arg, "-c", 2) == 0 && arg[2] != '\0') {
            g_mode_chars = 1;
            range_str = arg + 2;
        } else {
            fprintf(stderr, "cut: invalid option '%s'\n", arg);
            print_usage(argv[0]);
            return 1;
        }
        arg_idx++;
    }

    if ((g_mode_fields && g_mode_chars) || (!g_mode_fields && !g_mode_chars)) {
        fprintf(stderr, "cut: exactly one of -f or -c must be specified\n");
        print_usage(argv[0]);
        return 1;
    }

    if (!range_str || parse_ranges(range_str) < 0) {
        print_usage(argv[0]);
        return 1;
    }

    int num_files = argc - arg_idx;
    int had_error = 0;

    if (num_files == 0) {
        process_file(stdin, stdout);
    } else {
        for (int i = arg_idx; i < argc; i++) {
            const char *fn = argv[i];
            FILE *fp;
            if (strcmp(fn, "-") == 0) {
                fp = stdin;
            } else {
                fp = fopen(fn, "r");
            }

            if (!fp) {
                fprintf(stderr, "cut: %s: %s\n", fn, strerror(errno));
                had_error = 1;
                continue;
            }

            process_file(fp, stdout);
            if (fp != stdin)
                fclose(fp);
        }
    }

    return had_error ? 1 : 0;
}
