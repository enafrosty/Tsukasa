/*
 * Project Tsukasa — FAT32 formatter over the block-device seam (guide 12)
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

#include "mkfs_fat32.h"

#include "../include/kprintf.h"
#include "../include/kutils.h"
#include "../mm/heap.h"

#include <stddef.h>
#include <stdint.h>

/* On-disk FAT32 boot sector — field layout per the FAT spec / OSDev FAT; identical to fs/fat32.c's bpb_t... */
typedef struct __attribute__((packed)) {
    uint8_t  jump_boot[3];
    char     oem_name[8];
    uint16_t bytes_per_sector;
    uint8_t  sectors_per_cluster;
    uint16_t reserved_sector_count;
    uint8_t  num_fats;
    uint16_t root_entry_count;
    uint16_t total_sectors_16;
    uint8_t  media;
    uint16_t fat_size_16;
    uint16_t sectors_per_track;
    uint16_t num_heads;
    uint32_t hidden_sectors;
    uint32_t total_sectors_32;
    uint32_t fat_size_32;
    uint16_t ext_flags;
    uint16_t fs_version;
    uint32_t root_cluster;
    uint16_t fs_info;
    uint16_t backup_boot_sector;
    uint8_t  reserved[12];
    uint8_t  drive_number;
    uint8_t  reserved1;
    uint8_t  boot_sig;
    uint32_t volume_id;
    char     volume_label[11];
    char     fs_type[8];
    uint8_t  boot_code[420];
    uint16_t boot_signature;
} mkfs_bpb_t;

typedef struct __attribute__((packed)) {
    uint32_t lead_sig;
    uint8_t  reserved1[480];
    uint32_t struct_sig;
    uint32_t free_count;
    uint32_t next_free;
    uint8_t  reserved2[12];
    uint32_t trail_sig;
} mkfs_fsinfo_t;

_Static_assert(sizeof(mkfs_bpb_t) == 512, "BPB sector is 512 bytes");
_Static_assert(sizeof(mkfs_fsinfo_t) == 512, "FSInfo sector is 512 bytes");

static void fill_bpb(mkfs_bpb_t *bpb, uint8_t spc, uint32_t reserved,
                     uint32_t sectors_per_fat, uint32_t sector_count,
                     uint32_t hidden, const char upper_label[11])
{
    k_memset(bpb, 0, 512);
    bpb->jump_boot[0] = 0xEB;
    bpb->jump_boot[1] = 0x58;
    bpb->jump_boot[2] = 0x90;
    k_memcpy(bpb->oem_name, "MSDOS5.0", 8);
    bpb->bytes_per_sector      = 512;
    bpb->sectors_per_cluster   = spc;
    bpb->reserved_sector_count = (uint16_t)reserved;
    bpb->num_fats              = 2;
    bpb->media                 = 0xF8;
    bpb->sectors_per_track     = 63;
    bpb->num_heads             = 255;
    bpb->hidden_sectors        = hidden;
    bpb->total_sectors_32      = sector_count;
    bpb->fat_size_32           = sectors_per_fat;
    bpb->root_cluster          = 2;
    bpb->fs_info               = 1;
    bpb->backup_boot_sector    = 6;
    bpb->drive_number          = 0x80;
    bpb->boot_sig              = 0x29;
    bpb->volume_id             = 0x12345678;
    k_memcpy(bpb->volume_label, upper_label, 11);
    k_memcpy(bpb->fs_type, "FAT32   ", 8);
    bpb->boot_signature = 0xAA55;
}

static void fill_fsinfo(mkfs_fsinfo_t *fsi)
{
    k_memset(fsi, 0, 512);
    fsi->lead_sig   = 0x41615252u;
    fsi->struct_sig = 0x61417272u;
    fsi->free_count = 0xFFFFFFFFu;
    fsi->next_free  = 0xFFFFFFFFu;
    fsi->trail_sig  = 0xAA550000u;
}

int mkfs_fat32_format(block_dev_t *bd, const char *label)
{
    const uint32_t reserved = 32;
    uint32_t sector_count, data_sectors, cluster_count, fat_bytes;
    uint32_t sectors_per_fat, root_start;
    uint8_t spc;
    uint8_t *buf;
    char upper_label[11];
    int i;

    if (!bd || !bd->write)
        return -1;
    if (bd->sector_count < MIN_FAT32_SECTORS) {
        kprintf("[mkfs] %s: too small for FAT32 (< 32 MiB)\n", bd->name);
        return -1;
    }
    if (bd->sector_count > 0xFFFFFFFFULL) {
        kprintf("[mkfs] %s: > 2 TiB not supported\n", bd->name);
        return -1;
    }
    sector_count = (uint32_t)bd->sector_count;

    if      (sector_count <    532480u) spc = 1;
    else if (sector_count <   1064960u) spc = 2;
    else if (sector_count <   2097152u) spc = 4;
    else if (sector_count <  16777216u) spc = 8;
    else if (sector_count <  33554432u) spc = 16;
    else if (sector_count <  67108864u) spc = 32;
    else                                spc = 64;

    data_sectors    = sector_count - reserved;
    cluster_count   = data_sectors / spc;
    fat_bytes       = (cluster_count + 2u) * 4u;
    sectors_per_fat = (fat_bytes + 511u) / 512u;

    {
        const char *l = (label && label[0]) ? label : "NO NAME";
        for (i = 0; i < 11; i++) {
            char c = l[i];
            if (c == '\0') {
                for (; i < 11; i++)
                    upper_label[i] = ' ';
                break;
            }
            if (c >= 'a' && c <= 'z')
                c = (char)(c - 32);
            upper_label[i] = c;
        }
    }

    buf = (uint8_t *)kmalloc(4096);
    if (!buf)
        return -1;

    fill_bpb((mkfs_bpb_t *)buf, spc, reserved, sectors_per_fat,
             sector_count, (uint32_t)bd->lba_offset, upper_label);
    if (bd->write(bd, 0, 1, buf) != 0 || bd->write(bd, 6, 1, buf) != 0)
        goto fail;

    fill_fsinfo((mkfs_fsinfo_t *)buf);
    if (bd->write(bd, 1, 1, buf) != 0 || bd->write(bd, 7, 1, buf) != 0)
        goto fail;

    k_memset(buf, 0, 4096);
    for (i = 0; i < 2; i++) {
        uint32_t fat_start = reserved + (uint32_t)i * sectors_per_fat;
        uint32_t done = 0;
        while (done < sectors_per_fat) {
            uint32_t n = sectors_per_fat - done;
            if (n > 8)
                n = 8;
            if (bd->write(bd, fat_start + done, n, buf) != 0)
                goto fail;
            done += n;
        }
    }

    {
        uint32_t *fat = (uint32_t *)buf;
        fat[0] = 0x0FFFFFF8u;
        fat[1] = 0x0FFFFFFFu;
        fat[2] = 0x0FFFFFFFu;
        if (bd->write(bd, reserved, 1, buf) != 0 ||
            bd->write(bd, reserved + sectors_per_fat, 1, buf) != 0)
            goto fail;
    }

    k_memset(buf, 0, 4096);
    root_start = reserved + 2u * sectors_per_fat;
    {
        uint32_t done = 0;
        while (done < spc) {
            uint32_t n = (uint32_t)spc - done;
            if (n > 8)
                n = 8;
            if (bd->write(bd, root_start + done, n, buf) != 0)
                goto fail;
            done += n;
        }
    }

    kfree(buf);

    bd->is_fat32 = 1;
    {
        int end = 11;
        while (end > 0 && upper_label[end - 1] == ' ')
            end--;
        for (i = 0; i < end && i < BLOCKDEV_LABEL_MAX - 1; i++)
            bd->label[i] = upper_label[i];
        bd->label[i] = '\0';
    }

    kprintf("[mkfs] %s: FAT32 formatted (label=%s spc=%u fat_sectors=%u)\n",
            bd->name, bd->label, (uint32_t)spc, sectors_per_fat);
    return 0;

fail:
    kprintf("[mkfs] %s: write failed, format aborted\n", bd->name);
    kfree(buf);
    return -1;
}
