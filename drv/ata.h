/*
 * ata.h - ATA PIO 28-bit LBA driver (primary bus, polling).
 * Tested against QEMU's -hda virtual disk.
 */

#ifndef ATA_H
#define ATA_H

#include <stdint.h>
#include <stddef.h>

/**
 * Probe the primary ATA bus for a master drive.
 * Returns 1 if a drive was found, 0 otherwise.
 */
int  ata_init(void);

/**
 * Read `count` 512-byte sectors starting at `lba` into `buf`.
 * `buf` must be at least count*512 bytes.
 * Returns 0 on success, -1 on error.
 */
int  ata_read_sectors(uint32_t lba, uint8_t count, void *buf);

/**
 * Write `count` 512-byte sectors starting at `lba` from `buf`.
 * Returns 0 on success, -1 on error.
 */
int  ata_write_sectors(uint32_t lba, uint8_t count, const void *buf);

/** Total number of sectors reported by the drive (from IDENTIFY). */
uint32_t ata_sector_count(void);

#endif /* ATA_H */
