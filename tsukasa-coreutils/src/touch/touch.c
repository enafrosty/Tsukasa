/*
 * Project Tsukasa — touch File Timestamp and Creation Utility
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
#include <unistd.h>
#include <fcntl.h>
#include <string.h>
#include <errno.h>

static int touch_file(const char *path)
{
    int fd = open(path, O_WRONLY | O_CREAT, 0644);
    if (fd < 0) {
        fprintf(stderr, "touch: cannot touch '%s': %s\n", path, strerror(errno));
        return 1;
    }
    close(fd);
    return 0;
}

int main(int argc, char **argv)
{
    if (argc < 2) {
        fprintf(stderr, "touch: missing file operand\n");
        return 1;
    }

    int ret = 0;
    for (int i = 1; i < argc; i++) {
        if (argv[i][0] == '-' && argv[i][1] != '\0')
            continue;

        if (touch_file(argv[i]) != 0)
            ret = 1;
    }

    return ret;
}
