/*
 * Project Tsukasa — cat File Concatenation Utility
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

#define CAT_BUF_SIZE 4096

static int g_opt_number = 0;
static unsigned long g_line_number = 1;
static int g_at_line_start = 1;

static int cat_fd(int fd, const char *name)
{
    char buf[CAT_BUF_SIZE];
    ssize_t n;

    while ((n = read(fd, buf, sizeof(buf))) > 0) {
        if (!g_opt_number) {
            ssize_t written = 0;
            while (written < n) {
                ssize_t w = write(STDOUT_FILENO, buf + written, (size_t)(n - written));
                if (w < 0) {
                    fprintf(stderr, "cat: write error: %s\n", strerror(errno));
                    return 1;
                }
                written += w;
            }
        } else {
            for (ssize_t i = 0; i < n; i++) {
                if (g_at_line_start) {
                    printf("%6lu\t", g_line_number++);
                    g_at_line_start = 0;
                }
                putchar(buf[i]);
                if (buf[i] == '\n')
                    g_at_line_start = 1;
            }
        }
    }

    if (n < 0) {
        fprintf(stderr, "cat: %s: %s\n", name, strerror(errno));
        return 1;
    }
    return 0;
}

int main(int argc, char **argv)
{
    const char *files[64];
    int file_count = 0;

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-n") == 0) {
            g_opt_number = 1;
        } else if (argv[i][0] == '-' && argv[i][1] != '\0' && strcmp(argv[i], "-") != 0) {
            fprintf(stderr, "cat: invalid option -- '%s'\n", argv[i]);
            return 1;
        } else {
            if (file_count < 64)
                files[file_count++] = argv[i];
        }
    }

    if (file_count == 0) {
        return cat_fd(STDIN_FILENO, "standard input");
    }

    int ret = 0;
    for (int i = 0; i < file_count; i++) {
        if (strcmp(files[i], "-") == 0) {
            if (cat_fd(STDIN_FILENO, "standard input") != 0)
                ret = 1;
        } else {
            int fd = open(files[i], O_RDONLY);
            if (fd < 0) {
                fprintf(stderr, "cat: %s: %s\n", files[i], strerror(errno));
                ret = 1;
                continue;
            }
            if (cat_fd(fd, files[i]) != 0)
                ret = 1;
            close(fd);
        }
    }

    return ret;
}
