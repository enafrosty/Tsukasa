/*
 * Project Tsukasa — partition-table discovery (MBR + GPT) over the
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

#ifndef TSUKASA_DISKMGR_H
#define TSUKASA_DISKMGR_H

#include <stdint.h>
#include "blockdev.h"

/* Scan one whole disk: GPT first (LBA 1 signature + header CRC), MBR fallback, then the raw-FAT32 fallback... */
int diskmgr_rescan(block_dev_t *bd);

/* Rescan every registered whole disk (called from vfs_init after the disk drivers have registered). */
int diskmgr_scan_all(void);

/* True when the sector at `lba` carries a plausible FAT32 BPB. */
int diskmgr_probe_fat32(block_dev_t *bd, uint64_t lba);

/* disk_write_mbr): entry 0 = bootable, type 0x0C (FAT32 LBA), at part_start for part_count sectors; entries... */
int diskmgr_write_mbr(block_dev_t *bd, uint32_t part_start,
                      uint32_t part_count);

#endif /* TSUKASA_DISKMGR_H */
