/*
 * Project Tsukasa - FAT32 Filesystem Formatter Core
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

#include "../include/disktools.h"
#include "../include/fat32.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void format_volume_label(const char *src, char dst[11])
{
    memset(dst, ' ', 11);
    if (!src)
        return;

    size_t len = strlen(src);
    if (len > 11)
        len = 11;

    for (size_t i = 0; i < len; i++)
        dst[i] = (char)toupper((unsigned char)src[i]);
}

static int write_zero_sectors(device_handle_t *dev, uint64_t lba, uint32_t count)
{
    static uint8_t zbuf[16 * 512];
    uint32_t done = 0;
    while (done < count) {
        uint32_t chunk = count - done;
        if (chunk > 16u)
            chunk = 16u;
        if (device_write_sectors(dev, lba + done, chunk, zbuf) != 0)
            return -1;
        done += chunk;
    }
    return 0;
}

int fat32_format_device(device_handle_t *dev, const char *label,
                        uint32_t hidden_sectors, uint32_t user_spc, int verbose)
{
    if (!dev || dev->fd < 0)
        return -1;

    const char *label_arg = (label && label[0]) ? label : "TSUKASA";
    uint64_t total_sectors = dev->sector_count;
    if (total_sectors > 0xFFFFFFFFULL)
        total_sectors = 0xFFFFFFFFULL;

    uint32_t ts = (uint32_t)total_sectors;
    uint32_t rs = 32;

    uint32_t spc = 0;
    if (user_spc > 0) {
        spc = user_spc;
    } else {
        if (ts < 532480u)
            spc = 1;
        else if (ts < 1064960u)
            spc = 2;
        else if (ts < 2097152u)
            spc = 4;
        else if (ts < 16777216u)
            spc = 8;
        else if (ts < 33554432u)
            spc = 16;
        else
            spc = 32;
    }

    uint32_t ds = (ts > rs) ? (ts - rs) : 0;
    uint64_t fat_bytes = ((uint64_t)(ds / spc) + 2ULL) * 4ULL;
    uint32_t s_fat = (uint32_t)((fat_bytes + 511ULL) / 512ULL);

    if (ts <= rs + 2u * s_fat) {
        fprintf(stderr, "mkfs.fat32: volume too small for valid FAT32\n");
        return -1;
    }

    uint32_t cluster_count = (ts - (rs + 2u * s_fat)) / spc;
    if (cluster_count < FAT32_MIN_CLUSTERS) {
        fprintf(stderr, "mkfs.fat32: cluster count %u < minimum %u, volume too small for FAT32\n",
                cluster_count, FAT32_MIN_CLUSTERS);
        return -1;
    }

    if (verbose) {
        char sz_buf[32];
        format_size(device_get_size(dev), sz_buf, sizeof(sz_buf));
        printf("Formatting %s (%s, %u sectors)\n", dev->path, sz_buf, ts);
        printf("Parameters: %u reserved, 2 FATs, %u sectors/FAT, %u sectors/cluster, %u clusters\n",
               rs, s_fat, spc, cluster_count);
    }

    fat32_bpb_t bpb;
    memset(&bpb, 0, sizeof(bpb));
    bpb.jump_boot[0] = 0xEB;
    bpb.jump_boot[1] = 0x58;
    bpb.jump_boot[2] = 0x90;
    memcpy(bpb.oem_name, "TSUKASA ", 8);
    bpb.bytes_per_sector = 512;
    bpb.sectors_per_cluster = (uint8_t)spc;
    bpb.reserved_sector_count = (uint16_t)rs;
    bpb.num_fats = 2;
    bpb.root_entry_count = 0;
    bpb.total_sectors_16 = 0;
    bpb.media = 0xF8;
    bpb.fat_size_16 = 0;
    bpb.sectors_per_track = 63;
    bpb.num_heads = 255;
    bpb.hidden_sectors = hidden_sectors;
    bpb.total_sectors_32 = ts;
    bpb.fat_size_32 = s_fat;
    bpb.ext_flags = 0;
    bpb.fs_version = 0;
    bpb.root_cluster = 2;
    bpb.fs_info = 1;
    bpb.backup_boot_sector = 6;
    bpb.drive_number = 0x80;
    bpb.boot_sig = FAT32_EXT_BOOT_SIG;
    bpb.volume_id = 0x5453554Bu;
    format_volume_label(label_arg, bpb.volume_label);
    memcpy(bpb.fs_type, "FAT32   ", 8);
    bpb.boot_signature = FAT32_BOOT_SIG;

    if (write_zero_sectors(dev, 0, rs) != 0 ||
        device_write_sectors(dev, 0, 1, &bpb) != 0 ||
        device_write_sectors(dev, 6, 1, &bpb) != 0) {
        fprintf(stderr, "mkfs.fat32: failed to write BPB\n");
        return -1;
    }

    fat32_fsinfo_t fsinfo;
    memset(&fsinfo, 0, sizeof(fsinfo));
    fsinfo.lead_sig = FAT32_FSINFO_LEAD_SIG;
    fsinfo.struct_sig = FAT32_FSINFO_STRC_SIG;
    fsinfo.free_count = cluster_count - 1;
    fsinfo.next_free = 3;
    fsinfo.trail_sig = FAT32_FSINFO_TRAIL_SIG;

    if (device_write_sectors(dev, 1, 1, &fsinfo) != 0 ||
        device_write_sectors(dev, 7, 1, &fsinfo) != 0) {
        fprintf(stderr, "mkfs.fat32: failed to write FSInfo\n");
        return -1;
    }

    uint32_t fat_head[128];
    memset(fat_head, 0, sizeof(fat_head));
    fat_head[0] = FAT32_MEDIA_FIXED;
    fat_head[1] = FAT32_EOC;
    fat_head[2] = FAT32_EOC;

    uint32_t fat1_lba = rs;
    uint32_t fat2_lba = rs + s_fat;
    if (device_write_sectors(dev, fat1_lba, 1, fat_head) != 0 ||
        device_write_sectors(dev, fat2_lba, 1, fat_head) != 0) {
        fprintf(stderr, "mkfs.fat32: failed to write FAT table head\n");
        return -1;
    }

    if (s_fat > 1u) {
        if (write_zero_sectors(dev, fat1_lba + 1u, s_fat - 1u) != 0 ||
            write_zero_sectors(dev, fat2_lba + 1u, s_fat - 1u) != 0) {
            fprintf(stderr, "mkfs.fat32: failed to clear FAT tables\n");
            return -1;
        }
    }

    uint32_t root_lba = rs + 2u * s_fat;
    if (write_zero_sectors(dev, root_lba, spc) != 0) {
        fprintf(stderr, "mkfs.fat32: failed to clear root cluster\n");
        return -1;
    }

    if (label && label[0]) {
        fat32_dir_entry_t label_entry;
        memset(&label_entry, 0, sizeof(label_entry));
        format_volume_label(label_arg, label_entry.name);
        label_entry.attr = FAT32_ATTR_VOLUME_ID;

        uint8_t root_sec0[512];
        memset(root_sec0, 0, sizeof(root_sec0));
        memcpy(root_sec0, &label_entry, sizeof(label_entry));

        if (device_write_sectors(dev, root_lba, 1, root_sec0) != 0) {
            fprintf(stderr, "mkfs.fat32: failed to write volume label\n");
            return -1;
        }
    }

    if (device_sync(dev) != 0) {
        fprintf(stderr, "mkfs.fat32: failed to sync device\n");
        return -1;
    }

    printf("mkfs.fat32: successfully formatted %s (FAT32, label '%.11s', %u clusters)\n",
           dev->path, bpb.volume_label, cluster_count);
    return 0;
}
