/*
 * Project Tsukasa — cp File and Directory Copy Utility
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
#include <sys/stat.h>
#include <errno.h>

#define COPY_BUF_SIZE 4096
#define MAX_ENTRIES   256

static int g_opt_recursive = 0;

static const char *get_basename(const char *path)
{
    const char *last = strrchr(path, '/');
    if (last)
        return last + 1;
    return path;
}

static int copy_file(const char *src, const char *dst)
{
    int sfd = open(src, O_RDONLY);
    if (sfd < 0) {
        fprintf(stderr, "cp: cannot open '%s': %s\n", src, strerror(errno));
        return 1;
    }

    struct stat st;
    mode_t mode = 0644;
    if (fstat(sfd, &st) == 0 && st.st_mode)
        mode = st.st_mode & 0777;

    int dfd = open(dst, O_WRONLY | O_CREAT | O_TRUNC, mode);
    if (dfd < 0) {
        fprintf(stderr, "cp: cannot create '%s': %s\n", dst, strerror(errno));
        close(sfd);
        return 1;
    }

    char buf[COPY_BUF_SIZE];
    ssize_t n;
    int ret = 0;

    while ((n = read(sfd, buf, sizeof(buf))) > 0) {
        ssize_t written = 0;
        while (written < n) {
            ssize_t w = write(dfd, buf + written, (size_t)(n - written));
            if (w < 0) {
                fprintf(stderr, "cp: error writing to '%s': %s\n", dst, strerror(errno));
                ret = 1;
                break;
            }
            written += w;
        }
        if (ret != 0)
            break;
    }

    if (n < 0) {
        fprintf(stderr, "cp: error reading '%s': %s\n", src, strerror(errno));
        ret = 1;
    }

    close(sfd);
    close(dfd);
    return ret;
}

static int copy_path(const char *src, const char *dst);

static int copy_directory(const char *src, const char *dst)
{
    if (mkdir(dst, 0755) != 0 && errno != EEXIST) {
        fprintf(stderr, "cp: cannot create directory '%s': %s\n", dst, strerror(errno));
        return 1;
    }

    static char names[MAX_ENTRIES][64];
    int count = list_dir(src, names, MAX_ENTRIES);
    if (count < 0) {
        fprintf(stderr, "cp: cannot read directory '%s': %s\n", src, strerror(errno));
        return 1;
    }

    int ret = 0;
    char sub_src[512];
    char sub_dst[512];
    size_t slen = strlen(src);
    size_t dlen = strlen(dst);

    for (int i = 0; i < count; i++) {
        if (strcmp(names[i], ".") == 0 || strcmp(names[i], "..") == 0)
            continue;

        if (slen == 1 && src[0] == '/')
            snprintf(sub_src, sizeof(sub_src), "/%s", names[i]);
        else if (src[slen - 1] == '/')
            snprintf(sub_src, sizeof(sub_src), "%s%s", src, names[i]);
        else
            snprintf(sub_src, sizeof(sub_src), "%s/%s", src, names[i]);

        if (dlen == 1 && dst[0] == '/')
            snprintf(sub_dst, sizeof(sub_dst), "/%s", names[i]);
        else if (dst[dlen - 1] == '/')
            snprintf(sub_dst, sizeof(sub_dst), "%s%s", dst, names[i]);
        else
            snprintf(sub_dst, sizeof(sub_dst), "%s/%s", dst, names[i]);

        if (copy_path(sub_src, sub_dst) != 0)
            ret = 1;
    }

    return ret;
}

static int copy_path(const char *src, const char *dst)
{
    struct stat st;
    if (stat(src, &st) != 0) {
        fprintf(stderr, "cp: cannot stat '%s': %s\n", src, strerror(errno));
        return 1;
    }

    if (S_ISDIR(st.st_mode)) {
        if (!g_opt_recursive) {
            fprintf(stderr, "cp: -r not specified; omitting directory '%s'\n", src);
            return 1;
        }
        return copy_directory(src, dst);
    }

    return copy_file(src, dst);
}

int main(int argc, char **argv)
{
    const char *operands[64];
    int op_count = 0;

    for (int i = 1; i < argc; i++) {
        if (argv[i][0] == '-' && argv[i][1] != '\0') {
            for (size_t j = 1; argv[i][j]; j++) {
                if (argv[i][j] == 'r' || argv[i][j] == 'R')
                    g_opt_recursive = 1;
                else {
                    fprintf(stderr, "cp: invalid option -- '%c'\n", argv[i][j]);
                    return 1;
                }
            }
        } else {
            if (op_count < 64)
                operands[op_count++] = argv[i];
        }
    }

    if (op_count < 2) {
        fprintf(stderr, "cp: missing file operand\n");
        return 1;
    }

    const char *dest = operands[op_count - 1];
    struct stat dst_st;
    int dst_is_dir = (stat(dest, &dst_st) == 0 && S_ISDIR(dst_st.st_mode));

    if (op_count > 2 && !dst_is_dir) {
        fprintf(stderr, "cp: target '%s' is not a directory\n", dest);
        return 1;
    }

    int ret = 0;
    char target_path[512];

    for (int i = 0; i < op_count - 1; i++) {
        const char *src = operands[i];
        const char *final_dst = dest;

        if (dst_is_dir) {
            const char *bname = get_basename(src);
            size_t dlen = strlen(dest);
            if (dlen == 1 && dest[0] == '/')
                snprintf(target_path, sizeof(target_path), "/%s", bname);
            else if (dest[dlen - 1] == '/')
                snprintf(target_path, sizeof(target_path), "%s%s", dest, bname);
            else
                snprintf(target_path, sizeof(target_path), "%s/%s", dest, bname);
            final_dst = target_path;
        }

        if (copy_path(src, final_dst) != 0)
            ret = 1;
    }

    return ret;
}
