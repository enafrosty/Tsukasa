/*
 * Project Tsukasa — FAT12 filesystem driver (read/write) for ramdisk images
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

#ifndef FAT12_H
#define FAT12_H

#include <stdint.h>
#include <stddef.h>

/* Maximum entries returned by fat12_list_dir. */
#define FAT12_MAX_DIRENT  64
/* Maximum name length: "FILENAME.EXT\0" */
#define FAT12_NAME_LEN    13

/* A decoded directory entry. */
typedef struct {
    char     name[FAT12_NAME_LEN];
    uint32_t size;
    uint16_t first_cluster;
    uint8_t  attr;
    int      is_dir;
} fat12_dirent_t;

/* FAT attribute bits. */
#define FAT_ATTR_READ_ONLY  0x01
#define FAT_ATTR_HIDDEN     0x02
#define FAT_ATTR_SYSTEM     0x04
#define FAT_ATTR_VOLUME_ID  0x08
#define FAT_ATTR_DIRECTORY  0x10
#define FAT_ATTR_ARCHIVE    0x20

/* Initialize the FAT12 driver with a raw disk image in RAM. */
int fat12_init(void *disk, size_t size);

/* List entries in the given directory. */
int fat12_list_dir(const char *path, fat12_dirent_t *out, int max);

/* Read a file into a caller-supplied buffer. */
int fat12_read_file(const char *path, void *buf, size_t max);

/* Write (or create) a file. */
int fat12_write_file(const char *path, const void *data, size_t len);

/* Look up a file by path and fill in the dirent. @return 0 on success, -1 if not found. */
int fat12_stat(const char *path, fat12_dirent_t *out);

/* Read up to count bytes from path starting at byte offset. Returns bytes read, 0 on EOF, -1 on error. */
int fat12_read_file_offset(const char *path, void *buf, size_t offset, size_t count);

#endif /* FAT12_H */
