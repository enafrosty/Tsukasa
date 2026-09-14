/*
 * Project Tsukasa — ATA PIO 28-bit LBA driver (primary bus, polling)
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

#ifndef ATA_H
#define ATA_H

#include <stdint.h>
#include <stddef.h>

/* Probe the primary ATA bus for a master drive. Returns 1 if a drive was found, 0 otherwise. */
int  ata_init(void);

/* Read `count` 512-byte sectors starting at `lba` into `buf`. */
int  ata_read_sectors(uint32_t lba, uint8_t count, void *buf);

/* Write `count` 512-byte sectors starting at `lba` from `buf`. Returns 0 on success, -1 on error. */
int  ata_write_sectors(uint32_t lba, uint8_t count, const void *buf);

/* Total number of sectors reported by the drive (from IDENTIFY). */
uint32_t ata_sector_count(void);

#endif /* ATA_H */
