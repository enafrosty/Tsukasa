/*
 * Project Tsukasa — mkdir Directory Creation Utility
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
#include <sys/stat.h>
#include <errno.h>

static int g_opt_parents = 0;

static int make_parent_dirs(char *path, mode_t mode)
{
    size_t len = strlen(path);
    while (len > 1 && path[len - 1] == '/') {
        path[len - 1] = '\0';
        len--;
    }

    char *p = path;
    if (*p == '/')
        p++;

    while (*p) {
        if (*p == '/') {
            *p = '\0';
            struct stat st;
            if (stat(path, &st) != 0) {
                if (mkdir(path, mode) != 0 && errno != EEXIST) {
                    fprintf(stderr, "mkdir: cannot create directory '%s': %s\n",
                            path, strerror(errno));
                    *p = '/';
                    return 1;
                }
            } else if (!S_ISDIR(st.st_mode)) {
                fprintf(stderr, "mkdir: cannot create directory '%s': Not a directory\n", path);
                *p = '/';
                return 1;
            }
            *p = '/';
        }
        p++;
    }

    struct stat st;
    if (stat(path, &st) != 0) {
        if (mkdir(path, mode) != 0) {
            fprintf(stderr, "mkdir: cannot create directory '%s': %s\n",
                    path, strerror(errno));
            return 1;
        }
    } else if (!S_ISDIR(st.st_mode)) {
        fprintf(stderr, "mkdir: cannot create directory '%s': Not a directory\n", path);
        return 1;
    }

    return 0;
}

int main(int argc, char **argv)
{
    const char *dirs[64];
    int dir_count = 0;

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-p") == 0) {
            g_opt_parents = 1;
        } else if (argv[i][0] == '-' && argv[i][1] != '\0') {
            fprintf(stderr, "mkdir: invalid option -- '%s'\n", argv[i]);
            return 1;
        } else {
            if (dir_count < 64)
                dirs[dir_count++] = argv[i];
        }
    }

    if (dir_count == 0) {
        fprintf(stderr, "mkdir: missing operand\n");
        return 1;
    }

    mode_t mode = 0755;
    int ret = 0;

    for (int i = 0; i < dir_count; i++) {
        if (g_opt_parents) {
            char path_buf[512];
            strncpy(path_buf, dirs[i], sizeof(path_buf) - 1);
            path_buf[sizeof(path_buf) - 1] = '\0';
            if (make_parent_dirs(path_buf, mode) != 0)
                ret = 1;
        } else {
            if (mkdir(dirs[i], mode) != 0) {
                fprintf(stderr, "mkdir: cannot create directory '%s': %s\n",
                        dirs[i], strerror(errno));
                ret = 1;
            }
        }
    }

    return ret;
}
