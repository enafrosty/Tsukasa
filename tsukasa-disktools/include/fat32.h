/*
 * Project Tsukasa — FAT32 On-Disk Specification Header
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

#ifndef _TSUKASA_FAT32_H
#define _TSUKASA_FAT32_H

#include <stdint.h>

#define FAT32_BOOT_SIG         0xAA55u
#define FAT32_EXT_BOOT_SIG     0x29u
#define FAT32_FSINFO_LEAD_SIG  0x41615252u /* "RRaA" */
#define FAT32_FSINFO_STRC_SIG  0x61417272u /* "rrAa" */
#define FAT32_FSINFO_TRAIL_SIG 0xAA550000u

#define FAT32_EOC              0x0FFFFFFFu
#define FAT32_MEDIA_FIXED      0x0FFFFFF8u
#define FAT32_BAD_CLUSTER      0x0FFFFFF7u

#define FAT32_ATTR_READ_ONLY   0x01u
#define FAT32_ATTR_HIDDEN      0x02u
#define FAT32_ATTR_SYSTEM      0x04u
#define FAT32_ATTR_VOLUME_ID   0x08u
#define FAT32_ATTR_DIRECTORY   0x10u
#define FAT32_ATTR_ARCHIVE     0x20u

#define FAT32_MIN_CLUSTERS     65525u

#pragma pack(push, 1)
typedef struct {
    uint8_t  jump_boot[3];           /* 0xEB, 0x58, 0x90 */
    char     oem_name[8];            /* "TSUKASA " */
    uint16_t bytes_per_sector;       /* 512 */
    uint8_t  sectors_per_cluster;    /* 1, 2, 4, 8, 16, 32, 64 */
    uint16_t reserved_sector_count;  /* 32 */
    uint8_t  num_fats;               /* 2 */
    uint16_t root_entry_count;       /* 0 (FAT32 uses cluster chain) */
    uint16_t total_sectors_16;       /* 0 */
    uint8_t  media;                  /* 0xF8 (Fixed disk) */
    uint16_t fat_size_16;            /* 0 */
    uint16_t sectors_per_track;      /* 63 */
    uint16_t num_heads;              /* 255 */
    uint32_t hidden_sectors;         /* Partition start LBA */
    uint32_t total_sectors_32;       /* Total volume sectors */
    uint32_t fat_size_32;            /* Sectors per FAT */
    uint16_t ext_flags;              /* 0 */
    uint16_t fs_version;             /* 0 */
    uint32_t root_cluster;           /* 2 */
    uint16_t fs_info;                /* 1 */
    uint16_t backup_boot_sector;     /* 6 */
    uint8_t  reserved[12];           /* 0 */
    uint8_t  drive_number;           /* 0x80 */
    uint8_t  reserved1;              /* 0 */
    uint8_t  boot_sig;               /* 0x29 */
    uint32_t volume_id;              /* 32-bit timestamp hash */
    char     volume_label[11];       /* 11 chars, space-padded */
    char     fs_type[8];             /* "FAT32   " */
    uint8_t  boot_code[420];         /* 0 */
    uint16_t boot_signature;         /* 0xAA55 */
} fat32_bpb_t;

typedef struct {
    uint32_t lead_sig;               /* 0x41615252 ("RRaA") */
    uint8_t  reserved1[480];         /* 0 */
    uint32_t struct_sig;             /* 0x61417272 ("rrAa") */
    uint32_t free_count;             /* Total data clusters - 1 */
    uint32_t next_free;              /* 3 */
    uint8_t  reserved2[12];          /* 0 */
    uint32_t trail_sig;              /* 0xAA550000 */
} fat32_fsinfo_t;

typedef struct {
    char     name[11];               /* 8.3 filename space-padded */
    uint8_t  attr;                   /* Attributes */
    uint8_t  nt_res;                 /* Reserved for Windows NT */
    uint8_t  crt_time_tenth;         /* Millisecond stamp at create */
    uint16_t crt_time;               /* Create time */
    uint16_t crt_date;               /* Create date */
    uint16_t lst_acc_date;           /* Last access date */
    uint16_t fst_clus_hi;            /* High 16 bits of first cluster */
    uint16_t wrt_time;               /* Last write time */
    uint16_t wrt_date;               /* Last write date */
    uint16_t fst_clus_lo;            /* Low 16 bits of first cluster */
    uint32_t file_size;              /* File size in bytes */
} fat32_dir_entry_t;
#pragma pack(pop)

_Static_assert(sizeof(fat32_bpb_t) == 512, "FAT32 BPB must be 512 bytes");
_Static_assert(sizeof(fat32_fsinfo_t) == 512, "FAT32 FSInfo must be 512 bytes");
_Static_assert(sizeof(fat32_dir_entry_t) == 32, "FAT32 dir entry must be 32 bytes");

#endif /* _TSUKASA_FAT32_H */
