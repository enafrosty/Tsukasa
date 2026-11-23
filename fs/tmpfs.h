/*
 * Project Tsukasa - Scalable multi-instance in-memory filesystem (tmpfs)
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

#ifndef TSUKASA_TMPFS_H
#define TSUKASA_TMPFS_H

#include <stddef.h>
#include <stdint.h>

#include "vfs.h"

#define TMPFS_PAGE_SIZE      4096U
#define TMPFS_MAX_SYMLINKS   16

#define TMPFS_TYPE_FILE      1
#define TMPFS_TYPE_DIR       2
#define TMPFS_TYPE_SYMLINK   3

#define TMPFS_S_ISUID        04000U
#define TMPFS_S_ISGID        02000U
#define TMPFS_S_ISVTX        01000U
#define TMPFS_MODE_MASK      07777U

typedef struct tmpfs_inode tmpfs_inode_t;
typedef struct tmpfs_fs tmpfs_fs_t;

/* Create and destroy independent tmpfs mount instances. */
tmpfs_fs_t *tmpfs_create(void);
void tmpfs_destroy(tmpfs_fs_t *fs);

/* Path lookup and symlink resolution step (returns 0 on found, 1 on symlink hit, <0 on error). */
int tmpfs_resolve_step(tmpfs_fs_t *fs, const char *subpath, int follow_last,
                       tmpfs_inode_t **ino_out, char *sym_target_out,
                       size_t sym_cap, size_t *consumed_out);

/* Full intra-instance lookup (resolves relative/local symlinks up to TMPFS_MAX_SYMLINKS). */
tmpfs_inode_t *tmpfs_lookup(tmpfs_fs_t *fs, const char *subpath, int follow_symlinks);

/* Stat and directory operations. */
int tmpfs_stat(tmpfs_fs_t *fs, const char *subpath, int follow_symlinks, vfs_stat_t *out);
void tmpfs_stat_inode(const tmpfs_inode_t *ino, vfs_stat_t *out);
int tmpfs_mkdir(tmpfs_fs_t *fs, const char *subpath, uint32_t mode);
int tmpfs_mkdir_p(tmpfs_fs_t *fs, const char *subpath, uint32_t mode);
int tmpfs_rmdir(tmpfs_fs_t *fs, const char *subpath);
int tmpfs_readdir(tmpfs_fs_t *fs, const char *subpath,
                  char names[][VFS_NAME_MAX], int max);

/* File, symlink, unlink, rename operations. */
int tmpfs_create_file(tmpfs_fs_t *fs, const char *subpath, uint32_t mode,
                      tmpfs_inode_t **out_ino);
int tmpfs_symlink(tmpfs_fs_t *fs, const char *subpath, const char *target,
                  uint32_t mode);
int tmpfs_readlink(tmpfs_fs_t *fs, const char *subpath, char *buf, size_t cap);
int tmpfs_unlink(tmpfs_fs_t *fs, const char *subpath);
int tmpfs_rename(tmpfs_fs_t *fs, const char *old_subpath, const char *new_subpath);

/* Inode I/O and metadata. */
void tmpfs_inode_ref(tmpfs_inode_t *ino);
void tmpfs_inode_unref(tmpfs_inode_t *ino);
size_t tmpfs_read(tmpfs_inode_t *ino, size_t pos, void *buf, size_t count);
size_t tmpfs_write(tmpfs_inode_t *ino, size_t pos, const void *buf, size_t count);
int tmpfs_truncate(tmpfs_inode_t *ino, size_t new_size);
size_t tmpfs_inode_size(const tmpfs_inode_t *ino);
int tmpfs_inode_type(const tmpfs_inode_t *ino);
uint32_t tmpfs_inode_mode(const tmpfs_inode_t *ino);
void tmpfs_set_attrs(tmpfs_inode_t *ino, uint32_t mode, uint32_t uid,
                     uint32_t gid, uint64_t mtime);

#endif /* TSUKASA_TMPFS_H */
