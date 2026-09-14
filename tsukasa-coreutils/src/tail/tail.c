/*
 * Project Tsukasa — tail Last Lines Viewer Utility
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

static int tail_stream(int fd, const char *name, long lines)
{
    if (lines <= 0)
        return 0;

    char **ring = (char **)calloc((size_t)lines, sizeof(char *));
    if (!ring) {
        fprintf(stderr, "tail: memory allocation failed\n");
        return 1;
    }

    size_t ring_head = 0;
    size_t ring_count = 0;

    char buf[4096];
    char line[4096];
    size_t line_len = 0;
    ssize_t n;

    while ((n = read(fd, buf, sizeof(buf))) > 0) {
        for (ssize_t i = 0; i < n; i++) {
            if (line_len + 1 < sizeof(line))
                line[line_len++] = buf[i];

            if (buf[i] == '\n') {
                line[line_len] = '\0';
                if (ring[ring_head])
                    free(ring[ring_head]);
                ring[ring_head] = strdup(line);
                ring_head = (ring_head + 1) % (size_t)lines;
                if (ring_count < (size_t)lines)
                    ring_count++;
                line_len = 0;
            }
        }
    }

    if (line_len > 0) {
        line[line_len] = '\0';
        if (ring[ring_head])
            free(ring[ring_head]);
        ring[ring_head] = strdup(line);
        ring_head = (ring_head + 1) % (size_t)lines;
        if (ring_count < (size_t)lines)
            ring_count++;
    }

    if (n < 0) {
        fprintf(stderr, "tail: %s: %s\n", name, strerror(errno));
        for (long i = 0; i < lines; i++) {
            if (ring[i])
                free(ring[i]);
        }
        free(ring);
        return 1;
    }

    size_t start = (ring_count < (size_t)lines) ? 0 : ring_head;
    for (size_t i = 0; i < ring_count; i++) {
        size_t idx = (start + i) % (size_t)lines;
        if (ring[idx])
            printf("%s", ring[idx]);
    }

    for (long i = 0; i < lines; i++) {
        if (ring[i])
            free(ring[i]);
    }
    free(ring);
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
            fprintf(stderr, "tail: invalid option -- '%s'\n", argv[i]);
            return 1;
        } else {
            if (file_count < 64)
                files[file_count++] = argv[i];
        }
    }

    if (lines <= 0)
        return 0;

    if (file_count == 0) {
        return tail_stream(STDIN_FILENO, "standard input", lines);
    }

    int ret = 0;
    for (int i = 0; i < file_count; i++) {
        if (file_count > 1) {
            if (i > 0)
                printf("\n");
            printf("==> %s <==\n", files[i]);
        }

        if (strcmp(files[i], "-") == 0) {
            if (tail_stream(STDIN_FILENO, "standard input", lines) != 0)
                ret = 1;
        } else {
            int fd = open(files[i], O_RDONLY);
            if (fd < 0) {
                fprintf(stderr, "tail: cannot open '%s': %s\n", files[i], strerror(errno));
                ret = 1;
                continue;
            }
            if (tail_stream(fd, files[i], lines) != 0)
                ret = 1;
            close(fd);
        }
    }

    return ret;
}
