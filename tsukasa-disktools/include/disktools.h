/*
 * Project Tsukasa — Storage Suite Common Block Device Header
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

#ifndef _TSUKASA_DISKTOOLS_H
#define _TSUKASA_DISKTOOLS_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <sys/types.h>
#include <unistd.h>
#include <fcntl.h>

#define SECTOR_SIZE 512u

#ifndef BLKRRPART
#define BLKRRPART    0x125F
#endif
#ifndef BLKSSZGET
#define BLKSSZGET    0x1268
#endif
#ifndef BLKGETSIZE64
#define BLKGETSIZE64 0x80081272UL
#endif

typedef struct {
    int      fd;
    uint64_t sector_count;
    uint32_t sector_size;
    char     path[128];
} device_handle_t;

/* Device I/O helpers */
int      device_open(const char *path, int flags, device_handle_t *dev);
void     device_close(device_handle_t *dev);
int      device_read_sectors(device_handle_t *dev, uint64_t lba, uint32_t count, void *buf);
int      device_write_sectors(device_handle_t *dev, uint64_t lba, uint32_t count, const void *buf);
int      device_sync(device_handle_t *dev);
int      device_reread_partitions(device_handle_t *dev);
uint64_t device_get_size(device_handle_t *dev);
void     format_size(uint64_t bytes, char *out, size_t max);

/* Filesystem formatting helpers */
int      fat32_format_device(device_handle_t *dev, const char *label,
                             uint32_t hidden_sectors, uint32_t user_spc, int verbose);
int      ext4_format_device(device_handle_t *dev, const char *label, int verbose);

#endif /* _TSUKASA_DISKTOOLS_H */
