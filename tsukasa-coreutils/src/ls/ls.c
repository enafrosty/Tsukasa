/*
 * Project Tsukasa — ls Directory Listing Utility
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
#include <sys/stat.h>
#include <errno.h>

#define MAX_DIR_ENTRIES 256
#define COLOR_RESET   "\x1b[0m"
#define COLOR_DIR     "\x1b[1;34m"
#define COLOR_EXEC    "\x1b[1;32m"
#define COLOR_DEV     "\x1b[1;33m"

static int g_opt_all = 0;
static int g_opt_long = 0;
static int g_opt_human = 0;

static int cmp_names(const void *a, const void *b)
{
    const char *sa = *(const char *const *)a;
    const char *sb = *(const char *const *)b;
    return strcmp(sa, sb);
}

static void format_mode(mode_t mode, char *out)
{
    out[0] = S_ISDIR(mode) ? 'd' : (S_ISCHR(mode) ? 'c' : (S_ISSOCK(mode) ? 's' : '-'));
    out[1] = (mode & S_IRUSR) ? 'r' : '-';
    out[2] = (mode & S_IWUSR) ? 'w' : '-';
    out[3] = (mode & S_IXUSR) ? 'x' : '-';
    out[4] = (mode & S_IRGRP) ? 'r' : '-';
    out[5] = (mode & S_IWGRP) ? 'w' : '-';
    out[6] = (mode & S_IXGRP) ? 'x' : '-';
    out[7] = (mode & S_IROTH) ? 'r' : '-';
    out[8] = (mode & S_IWOTH) ? 'w' : '-';
    out[9] = (mode & S_IXOTH) ? 'x' : '-';
    out[10] = '\0';
}

static void format_size(off_t sz, char *out, size_t out_sz)
{
    if (!g_opt_human) {
        snprintf(out, out_sz, "%8lu", (unsigned long)sz);
        return;
    }

    if (sz < 1024) {
        snprintf(out, out_sz, "%7luB", (unsigned long)sz);
    } else if (sz < 1024 * 1024) {
        snprintf(out, out_sz, "%6luK", (unsigned long)(sz / 1024));
    } else if (sz < 1024 * 1024 * 1024) {
        snprintf(out, out_sz, "%6luM", (unsigned long)(sz / (1024 * 1024)));
    } else {
        snprintf(out, out_sz, "%6luG", (unsigned long)(sz / (1024 * 1024 * 1024)));
    }
}

static const char *get_color(mode_t mode)
{
    if (S_ISDIR(mode))
        return COLOR_DIR;
    if (S_ISCHR(mode))
        return COLOR_DEV;
    if (mode & (S_IXUSR | S_IXGRP | S_IXOTH))
        return COLOR_EXEC;
    return "";
}

static int print_entry(const char *full_path, const char *name)
{
    struct stat st;
    if (stat(full_path, &st) != 0) {
        memset(&st, 0, sizeof(st));
    }

    const char *color = get_color(st.st_mode);
    const char *reset = color[0] ? COLOR_RESET : "";

    if (g_opt_long) {
        char mode_str[12];
        char size_str[16];
        format_mode(st.st_mode, mode_str);
        format_size(st.st_size, size_str, sizeof(size_str));
        printf("%s %2lu root root %s %s%s%s\n",
               mode_str, (unsigned long)st.st_nlink, size_str,
               color, name, reset);
    } else {
        printf("%s%s%s  ", color, name, reset);
    }
    return 0;
}

static int list_path(const char *path, int multi)
{
    struct stat st;
    if (stat(path, &st) != 0) {
        fprintf(stderr, "ls: cannot access '%s': %s\n", path, strerror(errno));
        return 1;
    }

    if (!S_ISDIR(st.st_mode)) {
        print_entry(path, path);
        if (!g_opt_long)
            printf("\n");
        return 0;
    }

    if (multi)
        printf("%s:\n", path);

    static char raw_names[MAX_DIR_ENTRIES][64];
    int count = list_dir(path, raw_names, MAX_DIR_ENTRIES);
    if (count < 0) {
        fprintf(stderr, "ls: cannot read directory '%s': %s\n", path, strerror(errno));
        return 1;
    }

    const char *sorted[MAX_DIR_ENTRIES];
    int valid = 0;
    for (int i = 0; i < count; i++) {
        if (!g_opt_all && raw_names[i][0] == '.')
            continue;
        sorted[valid++] = raw_names[i];
    }

    qsort(sorted, (size_t)valid, sizeof(const char *), cmp_names);

    char full_buf[512];
    size_t plen = strlen(path);

    for (int i = 0; i < valid; i++) {
        if (plen == 1 && path[0] == '/')
            snprintf(full_buf, sizeof(full_buf), "/%s", sorted[i]);
        else if (path[plen - 1] == '/')
            snprintf(full_buf, sizeof(full_buf), "%s%s", path, sorted[i]);
        else
            snprintf(full_buf, sizeof(full_buf), "%s/%s", path, sorted[i]);

        print_entry(full_buf, sorted[i]);
    }

    if (!g_opt_long && valid > 0)
        printf("\n");

    return 0;
}

int main(int argc, char **argv)
{
    const char *targets[64];
    int target_count = 0;

    for (int i = 1; i < argc; i++) {
        if (argv[i][0] == '-' && argv[i][1] != '\0') {
            for (size_t j = 1; argv[i][j]; j++) {
                if (argv[i][j] == 'a')
                    g_opt_all = 1;
                else if (argv[i][j] == 'l')
                    g_opt_long = 1;
                else if (argv[i][j] == 'h')
                    g_opt_human = 1;
                else {
                    fprintf(stderr, "ls: invalid option -- '%c'\n", argv[i][j]);
                    return 2;
                }
            }
        } else {
            if (target_count < 64)
                targets[target_count++] = argv[i];
        }
    }

    if (target_count == 0) {
        targets[0] = ".";
        target_count = 1;
    }

    int ret = 0;
    for (int i = 0; i < target_count; i++) {
        if (i > 0 && target_count > 1)
            printf("\n");
        if (list_path(targets[i], target_count > 1) != 0)
            ret = 1;
    }

    return ret;
}
