/*
 * Project Tsukasa - Journaled ext2/ext3/ext4 filesystem adapter
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

#ifndef TSUKASA_EXT4FS_H
#define TSUKASA_EXT4FS_H

#include <stddef.h>
#include <stdint.h>

#include "vfs.h"

struct block_dev;

typedef struct ext4fs ext4fs_t;

ext4fs_t *ext4fs_mount(struct block_dev *bd, int read_only, int *err_out);
int ext4fs_umount(ext4fs_t *fs);
int ext4fs_sync(ext4fs_t *fs);

int ext4fs_stat(ext4fs_t *fs, const char *subpath, int follow_last, vfs_stat_t *out);
int ext4fs_open(ext4fs_t *fs, const char *subpath, int vfs_flags, uint32_t create_mode,
                void **handle_out, int *is_dir_out, uint64_t *ino_out, size_t *size_out);
void ext4fs_close(ext4fs_t *fs, void *handle, int dirty);
long ext4fs_read(ext4fs_t *fs, void *handle, size_t pos, void *buf, size_t count);
long ext4fs_write(ext4fs_t *fs, void *handle, size_t pos, const void *buf, size_t count);
long ext4fs_read_subpath(ext4fs_t *fs, const char *subpath, size_t pos, void *buf, size_t count);
long ext4fs_write_subpath(ext4fs_t *fs, const char *subpath, size_t pos, const void *buf, size_t count);
size_t ext4fs_file_size(ext4fs_t *fs, void *handle, const char *subpath);
int ext4fs_fstat(ext4fs_t *fs, void *handle, const char *subpath, vfs_stat_t *out);

int ext4fs_mkdir(ext4fs_t *fs, const char *subpath, uint32_t mode);
int ext4fs_rmdir(ext4fs_t *fs, const char *subpath);
int ext4fs_unlink(ext4fs_t *fs, const char *subpath);
int ext4fs_rename(ext4fs_t *fs, const char *old_sub, const char *new_sub);
int ext4fs_symlink(ext4fs_t *fs, const char *target, const char *link_sub);
int ext4fs_readlink(ext4fs_t *fs, const char *subpath, char *buf, size_t cap);
int ext4fs_truncate(ext4fs_t *fs, const char *subpath, size_t length);
int ext4fs_ftruncate(ext4fs_t *fs, void *handle, size_t length);
int ext4fs_chmod(ext4fs_t *fs, const char *subpath, uint32_t mode);
int ext4fs_chown(ext4fs_t *fs, const char *subpath, uint32_t uid, uint32_t gid);
int ext4fs_readdir(ext4fs_t *fs, const char *subpath, char names[][VFS_NAME_MAX], int max);

#endif /* TSUKASA_EXT4FS_H */
