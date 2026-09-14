/*
 * Project Tsukasa — read-only USTAR (tar) filesystem backend
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

#ifndef TAR_H
#define TAR_H

#include <stddef.h>
#include <stdint.h>

#include "vfs.h"   /* vfs_stat_t, VFS_PATH_MAX, VFS_NAME_MAX */

/* One indexed archive member. */
typedef struct tar_inode {
    char           path[VFS_PATH_MAX];
    const uint8_t *data;
    uint64_t       size;
    int            is_dir;
} tar_inode_t;

/* Opaque mounted-archive handle (allocated by tar_mount, stored as the VFS mount ctx). */
typedef struct tar_fs tar_fs_t;

/* Scan `archive` once and build an in-memory index (no data copy). */
tar_fs_t *tar_mount(const void *archive, uint64_t archive_size);

/* Number of indexed entries (files + directories). */
int tar_file_count(const tar_fs_t *fs);

/* Exact-path lookup. Returns the inode or NULL. */
const tar_inode_t *tar_lookup(const tar_fs_t *fs, const char *path);

/* Fill a vfs_stat_t for `path` (file, directory, or implicit directory). */
int tar_stat(const tar_fs_t *fs, const char *path, vfs_stat_t *out);

/* List the DIRECT children of `path` (deduped; files and subdirs). */
int tar_list_dir(const tar_fs_t *fs, const char *path,
                 char names[][VFS_NAME_MAX], int max);

/* Bounds-checked read of an indexed file straight from the archive buffer. */
uint64_t tar_read(const tar_fs_t *fs, const tar_inode_t *ino,
                  uint64_t pos, void *buf, uint64_t count);

#endif /* TAR_H */
