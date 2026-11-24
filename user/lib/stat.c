/*
 * Project Tsukasa — include "../include/sys/stat.h"
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

#include <stddef.h>
#include <stdint.h>
#include "user/include/sys/stat.h"
#include "user/lib/syscall.h"
#include "user/include/syscall_nums.h"

static mode_t map_mode(const struct tsukasa_stat *in)
{
    mode_t mode = 0;
    if (!in)
        return 0;
    if (in->st_mode != 0)
        return (mode_t)in->st_mode;
    if (in->type == TSUKASA_STAT_TYPE_DIR)
        mode |= S_IFDIR;
    else if (in->type == TSUKASA_STAT_TYPE_CHAR)
        mode |= S_IFCHR;
    else if (in->type == TSUKASA_STAT_TYPE_PIPE)
        mode |= S_IFIFO;
    else if (in->type == TSUKASA_STAT_TYPE_SOCKET)
        mode |= S_IFSOCK;
    else if (in->type == TSUKASA_STAT_TYPE_SYMLINK)
        mode |= S_IFLNK;
    else
        mode |= S_IFREG;

    if (in->mode & TSUKASA_O_RDONLY)
        mode |= S_IRUSR | S_IRGRP | S_IROTH;
    if (in->mode & TSUKASA_O_WRONLY)
        mode |= S_IWUSR | S_IWGRP | S_IWOTH;
    return mode;
}

static void copy_stat(struct stat *dst, const struct tsukasa_stat *st)
{
    dst->st_dev = st->st_dev;
    dst->st_ino = st->st_ino;
    dst->st_mode = map_mode(st);
    dst->st_nlink = st->st_nlink ? st->st_nlink : 1U;
    dst->st_uid = st->st_uid;
    dst->st_gid = st->st_gid;
    dst->st_rdev = st->st_rdev;
    dst->st_size = (off_t)st->size;
    dst->st_blksize = st->st_blksize ? st->st_blksize : 4096U;
    dst->st_blocks = st->blocks;
    dst->st_atime = st->st_atime;
    dst->st_mtime = st->st_mtime;
    dst->st_ctime = st->st_ctime;
}

int stat(const char *pathname, struct stat *statbuf)
{
    struct tsukasa_stat st;
    if (!pathname || !statbuf)
        return -1;
    if (fs_stat(pathname, &st) != 0)
        return -1;
    copy_stat(statbuf, &st);
    return 0;
}

int fstat(int fd, struct stat *statbuf)
{
    struct tsukasa_stat st;
    if (!statbuf)
        return -1;
    if (fs_fstat(fd, &st) != 0)
        return -1;
    copy_stat(statbuf, &st);
    return 0;
}
