/*
 * Project Tsukasa — FAT32 filesystem driver (multi-volume since guide 12)
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

#ifndef FAT32_H
#define FAT32_H

#include <stddef.h>
#include <stdint.h>

struct block_dev;

/* Maximum length of a file/directory name (8.3 long-name path). */
#define FAT32_NAME_MAX  256
#define FAT32_MAX_DIRENT 64

typedef struct {
    char   name[FAT32_NAME_MAX];
    uint32_t size;
    int    is_dir;
    uint32_t first_cluster;
} fat32_dirent_t;

/* Opaque mounted-volume handle (static pool inside fat32.c). */
typedef struct fat32_volume fat32_volume_t;

/* Bind a volume to `bd` and read/validate its BPB (the device is expected to be partition-relative: its... */
fat32_volume_t *fat32_vol_mount(struct block_dev *bd);

int fat32_vol_create_file(fat32_volume_t *vol, const char *path);

int fat32_vol_list_dir(fat32_volume_t *vol, const char *path,
                       fat32_dirent_t *entries, int max);
int fat32_vol_stat(fat32_volume_t *vol, const char *path,
                   fat32_dirent_t *out);
int fat32_vol_read_file(fat32_volume_t *vol, const char *path,
                        void *buf, size_t max_bytes);
int fat32_vol_write_file(fat32_volume_t *vol, const char *path,
                         const void *buf, size_t size);
int fat32_vol_rename(fat32_volume_t *vol, const char *old_path,
                     const char *new_path);

/* Legacy single-volume API (the /disk mount) */

/* Mount the primary volume: the first whole disk carrying a raw FAT32 filesystem. */
int fat32_init(void);

int fat32_list_dir(const char *path, fat32_dirent_t *entries, int max);
int fat32_stat(const char *path, fat32_dirent_t *out);
int fat32_read_file(const char *path, void *buf, size_t max_bytes);
int fat32_write_file(const char *path, const void *buf, size_t size);
int fat32_rename(const char *old_path, const char *new_path);

#endif /* FAT32_H */
