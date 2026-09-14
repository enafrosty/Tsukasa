/*
 * Project Tsukasa — FAT32 formatter (guide 12)
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

#ifndef TSUKASA_MKFS_FAT32_H
#define TSUKASA_MKFS_FAT32_H

#include "../drv/blockdev.h"

#define MIN_FAT32_SECTORS 65536u

/* Format `bd` (a partition child or a whole disk) as FAT32: BPB + backup BPB, FSInfo + backup, both FATs,... */
int mkfs_fat32_format(block_dev_t *bd, const char *label);

#endif /* TSUKASA_MKFS_FAT32_H */
