/*
 * Project Tsukasa — rm File and Directory Removal Utility
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
#include <sys/stat.h>
#include <errno.h>

#define MAX_ENTRIES 256

static int g_opt_recursive = 0;
static int g_opt_force = 0;

static int remove_path(const char *path)
{
    struct stat st;
    if (stat(path, &st) != 0) {
        if (g_opt_force && errno == ENOENT)
            return 0;
        fprintf(stderr, "rm: cannot remove '%s': %s\n", path, strerror(errno));
        return 1;
    }

    if (S_ISDIR(st.st_mode)) {
        if (!g_opt_recursive) {
            fprintf(stderr, "rm: cannot remove '%s': Is a directory\n", path);
            return 1;
        }

        static char names[MAX_ENTRIES][64];
        int count = list_dir(path, names, MAX_ENTRIES);
        if (count >= 0) {
            char sub[512];
            size_t plen = strlen(path);

            for (int i = 0; i < count; i++) {
                if (strcmp(names[i], ".") == 0 || strcmp(names[i], "..") == 0)
                    continue;

                if (plen == 1 && path[0] == '/')
                    snprintf(sub, sizeof(sub), "/%s", names[i]);
                else if (path[plen - 1] == '/')
                    snprintf(sub, sizeof(sub), "%s%s", path, names[i]);
                else
                    snprintf(sub, sizeof(sub), "%s/%s", path, names[i]);

                if (remove_path(sub) != 0 && !g_opt_force)
                    return 1;
            }
        }

        if (rmdir(path) != 0) {
            if (!g_opt_force) {
                fprintf(stderr, "rm: cannot remove directory '%s': %s\n",
                        path, strerror(errno));
                return 1;
            }
        }
        return 0;
    }

    if (unlink(path) != 0) {
        if (!g_opt_force) {
            fprintf(stderr, "rm: cannot remove '%s': %s\n", path, strerror(errno));
            return 1;
        }
    }
    return 0;
}

int main(int argc, char **argv)
{
    const char *targets[64];
    int target_count = 0;

    for (int i = 1; i < argc; i++) {
        if (argv[i][0] == '-' && argv[i][1] != '\0') {
            for (size_t j = 1; argv[i][j]; j++) {
                if (argv[i][j] == 'r' || argv[i][j] == 'R')
                    g_opt_recursive = 1;
                else if (argv[i][j] == 'f')
                    g_opt_force = 1;
                else {
                    fprintf(stderr, "rm: invalid option -- '%c'\n", argv[i][j]);
                    return 1;
                }
            }
        } else {
            if (target_count < 64)
                targets[target_count++] = argv[i];
        }
    }

    if (target_count == 0) {
        if (!g_opt_force) {
            fprintf(stderr, "rm: missing operand\n");
            return 1;
        }
        return 0;
    }

    int ret = 0;
    for (int i = 0; i < target_count; i++) {
        if (remove_path(targets[i]) != 0)
            ret = 1;
    }

    return ret;
}
