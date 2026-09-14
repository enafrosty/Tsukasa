/*
 * Project Tsukasa — Master Boot Record (MBR) Definitions
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

#ifndef _TSUKASA_MBR_H
#define _TSUKASA_MBR_H

#include <stdint.h>

#define MBR_SIGNATURE            0xAA55u
#define MBR_BOOTABLE             0x80u
#define MBR_INACTIVE             0x00u

#define MBR_TYPE_EMPTY           0x00u
#define MBR_TYPE_FAT12           0x01u
#define MBR_TYPE_FAT16_SMALL     0x04u
#define MBR_TYPE_FAT16           0x06u
#define MBR_TYPE_NTFS_EXFAT      0x07u
#define MBR_TYPE_FAT32_CHS       0x0Bu
#define MBR_TYPE_FAT32_LBA       0x0Cu
#define MBR_TYPE_FAT16_LBA       0x0Eu
#define MBR_TYPE_LINUX_SWAP      0x82u
#define MBR_TYPE_LINUX_EXT       0x83u
#define MBR_TYPE_GPT_PROTECTIVE  0xEEu
#define MBR_TYPE_EFI_SYSTEM      0xEFu

#pragma pack(push, 1)
typedef struct {
    uint8_t  boot_indicator;   /* 0x80 = Active / Bootable, 0x00 = Inactive */
    uint8_t  start_head;       /* CHS Start Head */
    uint8_t  start_sector;     /* Bits 0-5: Sector (1-63); Bits 6-7: Cylinder bits 8-9 */
    uint8_t  start_cylinder;   /* Cylinder bits 0-7 */
    uint8_t  partition_type;   /* System ID: 0x0C = FAT32 LBA, 0x83 = Linux/Ext2, 0xEE = GPT */
    uint8_t  end_head;         /* CHS End Head */
    uint8_t  end_sector;       /* CHS End Sector */
    uint8_t  end_cylinder;     /* CHS End Cylinder */
    uint32_t lba_start;        /* Starting LBA sector (LE) */
    uint32_t sector_count;     /* Total sector count (LE) */
} mbr_partition_entry_t;

typedef struct {
    uint8_t               boot_code[446];
    mbr_partition_entry_t entries[4];
    uint16_t              boot_signature; /* 0xAA55 */
} mbr_sector_t;
#pragma pack(pop)

_Static_assert(sizeof(mbr_partition_entry_t) == 16, "MBR partition entry must be 16 bytes");
_Static_assert(sizeof(mbr_sector_t) == 512, "MBR sector must be 512 bytes");

static inline void lba_to_chs(uint32_t lba, uint8_t *head, uint8_t *sector, uint8_t *cylinder)
{
    /* Standard legacy CHS translation: 255 heads, 63 sectors/track */
    const uint32_t max_chs_lba = 1023u * 255u * 63u;
    if (lba > max_chs_lba) {
        *head = 254;
        *sector = 0xFF;   /* Cylinder bits 8-9 (0xC0) | Sector 63 (0x3F) */
        *cylinder = 0xFF; /* Cylinder bits 0-7 (1023 & 0xFF) */
        return;
    }

    uint32_t c = lba / (255u * 63u);
    uint32_t rem = lba % (255u * 63u);
    uint32_t h = rem / 63u;
    uint32_t s = (rem % 63u) + 1u;

    *head = (uint8_t)h;
    *sector = (uint8_t)(((c >> 2) & 0xC0u) | (s & 0x3Fu));
    *cylinder = (uint8_t)(c & 0xFFu);
}

static inline const char *mbr_type_name(uint8_t type)
{
    switch (type) {
    case MBR_TYPE_EMPTY:          return "Empty";
    case MBR_TYPE_FAT12:          return "FAT12";
    case MBR_TYPE_FAT16_SMALL:    return "FAT16 (<32MB)";
    case MBR_TYPE_FAT16:          return "FAT16";
    case MBR_TYPE_NTFS_EXFAT:     return "HPFS/NTFS/exFAT";
    case MBR_TYPE_FAT32_CHS:      return "W95 FAT32 (CHS)";
    case MBR_TYPE_FAT32_LBA:      return "W95 FAT32 (LBA)";
    case MBR_TYPE_FAT16_LBA:      return "W95 FAT16 (LBA)";
    case MBR_TYPE_LINUX_SWAP:     return "Linux swap";
    case MBR_TYPE_LINUX_EXT:      return "Linux native (Ext2/3/4)";
    case MBR_TYPE_GPT_PROTECTIVE: return "GPT protective";
    case MBR_TYPE_EFI_SYSTEM:     return "EFI System (FAT)";
    default:                      return "Unknown";
    }
}

#endif /* _TSUKASA_MBR_H */
