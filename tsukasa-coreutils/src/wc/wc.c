/*
 * Project Tsukasa — wc Word, Line, and Byte Count Utility
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
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>

static int g_opt_lines = 0;
static int g_opt_words = 0;
static int g_opt_bytes = 0;

typedef struct {
    unsigned long lines;
    unsigned long words;
    unsigned long bytes;
} wc_count_t;

static int is_space_char(char c)
{
    return (c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f' || c == '\v');
}

static int count_fd(int fd, const char *name, wc_count_t *out)
{
    char buf[4096];
    ssize_t n;
    int in_word = 0;

    out->lines = 0;
    out->words = 0;
    out->bytes = 0;

    while ((n = read(fd, buf, sizeof(buf))) > 0) {
        out->bytes += (unsigned long)n;
        for (ssize_t i = 0; i < n; i++) {
            if (buf[i] == '\n')
                out->lines++;

            if (is_space_char(buf[i])) {
                in_word = 0;
            } else if (!in_word) {
                in_word = 1;
                out->words++;
            }
        }
    }

    if (n < 0) {
        fprintf(stderr, "wc: %s: %s\n", name, strerror(errno));
        return 1;
    }
    return 0;
}

static void print_counts(const wc_count_t *c, const char *name)
{
    if (g_opt_lines)
        printf("%7lu ", c->lines);
    if (g_opt_words)
        printf("%7lu ", c->words);
    if (g_opt_bytes)
        printf("%7lu ", c->bytes);
    if (name && name[0])
        printf("%s", name);
    printf("\n");
}

int main(int argc, char **argv)
{
    const char *files[64];
    int file_count = 0;

    for (int i = 1; i < argc; i++) {
        if (argv[i][0] == '-' && argv[i][1] != '\0' && strcmp(argv[i], "-") != 0) {
            for (size_t j = 1; argv[i][j]; j++) {
                if (argv[i][j] == 'l')
                    g_opt_lines = 1;
                else if (argv[i][j] == 'w')
                    g_opt_words = 1;
                else if (argv[i][j] == 'c')
                    g_opt_bytes = 1;
                else {
                    fprintf(stderr, "wc: invalid option -- '%c'\n", argv[i][j]);
                    return 1;
                }
            }
        } else {
            if (file_count < 64)
                files[file_count++] = argv[i];
        }
    }

    if (!g_opt_lines && !g_opt_words && !g_opt_bytes) {
        g_opt_lines = 1;
        g_opt_words = 1;
        g_opt_bytes = 1;
    }

    if (file_count == 0) {
        wc_count_t c;
        if (count_fd(STDIN_FILENO, "standard input", &c) != 0)
            return 1;
        print_counts(&c, "");
        return 0;
    }

    wc_count_t total = {0, 0, 0};
    int ret = 0;

    for (int i = 0; i < file_count; i++) {
        wc_count_t c;
        if (strcmp(files[i], "-") == 0) {
            if (count_fd(STDIN_FILENO, "standard input", &c) != 0) {
                ret = 1;
                continue;
            }
            print_counts(&c, "-");
        } else {
            int fd = open(files[i], O_RDONLY);
            if (fd < 0) {
                fprintf(stderr, "wc: %s: %s\n", files[i], strerror(errno));
                ret = 1;
                continue;
            }
            if (count_fd(fd, files[i], &c) != 0) {
                ret = 1;
                close(fd);
                continue;
            }
            close(fd);
            print_counts(&c, files[i]);
        }

        total.lines += c.lines;
        total.words += c.words;
        total.bytes += c.bytes;
    }

    if (file_count > 1) {
        print_counts(&total, "total");
    }

    return ret;
}
