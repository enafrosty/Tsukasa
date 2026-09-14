/*
 * Project Tsukasa — head First Lines Viewer Utility
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
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>

static int head_fd(int fd, const char *name, long lines)
{
    char buf[4096];
    ssize_t n;
    long count = 0;

    while (count < lines && (n = read(fd, buf, sizeof(buf))) > 0) {
        for (ssize_t i = 0; i < n; i++) {
            putchar(buf[i]);
            if (buf[i] == '\n') {
                count++;
                if (count >= lines)
                    break;
            }
        }
    }

    if (n < 0) {
        fprintf(stderr, "head: %s: %s\n", name, strerror(errno));
        return 1;
    }
    return 0;
}

int main(int argc, char **argv)
{
    long lines = 10;
    const char *files[64];
    int file_count = 0;

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-n") == 0 && i + 1 < argc) {
            lines = atol(argv[++i]);
        } else if (argv[i][0] == '-' && argv[i][1] >= '0' && argv[i][1] <= '9') {
            lines = atol(&argv[i][1]);
        } else if (argv[i][0] == '-' && argv[i][1] != '\0' && strcmp(argv[i], "-") != 0) {
            fprintf(stderr, "head: invalid option -- '%s'\n", argv[i]);
            return 1;
        } else {
            if (file_count < 64)
                files[file_count++] = argv[i];
        }
    }

    if (lines <= 0)
        return 0;

    if (file_count == 0) {
        return head_fd(STDIN_FILENO, "standard input", lines);
    }

    int ret = 0;
    for (int i = 0; i < file_count; i++) {
        if (file_count > 1) {
            if (i > 0)
                printf("\n");
            printf("==> %s <==\n", files[i]);
        }

        if (strcmp(files[i], "-") == 0) {
            if (head_fd(STDIN_FILENO, "standard input", lines) != 0)
                ret = 1;
        } else {
            int fd = open(files[i], O_RDONLY);
            if (fd < 0) {
                fprintf(stderr, "head: cannot open '%s': %s\n", files[i], strerror(errno));
                ret = 1;
                continue;
            }
            if (head_fd(fd, files[i], lines) != 0)
                ret = 1;
            close(fd);
        }
    }

    return ret;
}
