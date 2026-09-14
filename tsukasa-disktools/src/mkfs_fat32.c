/*
 * Project Tsukasa — FAT32 Filesystem Formatter (mkfs.fat32)
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

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

static void print_usage(const char *prog)
{
    fprintf(stderr, "Usage: %s [-n label] [-s sectors_per_cluster] <device_or_image_file>\n", prog);
}

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

int main(int argc, char *argv[])
{
    const char *dev_path = NULL;
    const char *label_arg = "TSUKASA";
    uint32_t user_spc = 0;
    int verbose = 0;

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-n") == 0 || strcmp(argv[i], "-L") == 0) {
            if (i + 1 >= argc) {
                print_usage(argv[0]);
                return 1;
            }
            label_arg = argv[++i];
        } else if (strcmp(argv[i], "-s") == 0) {
            if (i + 1 >= argc) {
                print_usage(argv[0]);
                return 1;
            }
            user_spc = (uint32_t)atoi(argv[++i]);
        } else if (strcmp(argv[i], "-F") == 0) {
            if (i + 1 >= argc) {
                print_usage(argv[0]);
                return 1;
            }
            i++; /* Skip format version flag (e.g. 32) */
        } else if (strcmp(argv[i], "-v") == 0) {
            verbose = 1;
        } else if (argv[i][0] == '-') {
            fprintf(stderr, "Unknown option: %s\n", argv[i]);
            print_usage(argv[0]);
            return 1;
        } else {
            dev_path = argv[i];
        }
    }

    if (!dev_path) {
        print_usage(argv[0]);
        return 1;
    }

    device_handle_t dev;
    if (device_open(dev_path, O_RDWR, &dev) != 0) {
        fprintf(stderr, "mkfs.fat32: cannot open %s\n", dev_path);
        return 1;
    }

    uint64_t total_sectors = dev.sector_count;
    if (total_sectors > 0xFFFFFFFFULL)
        total_sectors = 0xFFFFFFFFULL;

    uint32_t ts = (uint32_t)total_sectors;
    uint32_t rs = 32;

    /* Adaptive Sectors Per Cluster heuristic */
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

    /* FAT Table Sizing */
    uint32_t ds = (ts > rs) ? (ts - rs) : 0;
    uint64_t fat_bytes = ((uint64_t)(ds / spc) + 2ULL) * 4ULL;
    uint32_t s_fat = (uint32_t)((fat_bytes + 511ULL) / 512ULL);

    /* Cluster count validation */
    if (ts <= rs + 2u * s_fat) {
        fprintf(stderr, "mkfs.fat32: volume too small for valid FAT32\n");
        device_close(&dev);
        return 1;
    }

    uint32_t cluster_count = (ts - (rs + 2u * s_fat)) / spc;
    if (cluster_count < FAT32_MIN_CLUSTERS) {
        fprintf(stderr, "mkfs.fat32: cluster count %u < minimum %u, volume too small for FAT32\n",
                cluster_count, FAT32_MIN_CLUSTERS);
        device_close(&dev);
        return 1;
    }

    if (verbose) {
        char sz_buf[32];
        format_size(device_get_size(&dev), sz_buf, sizeof(sz_buf));
        printf("Formatting %s (%s, %u sectors)\n", dev_path, sz_buf, ts);
        printf("Parameters: %u reserved, 2 FATs, %u sectors/FAT, %u sectors/cluster, %u clusters\n",
               rs, s_fat, spc, cluster_count);
    }

    /* 1. Build and write BPB (LBA 0 and backup LBA 6) */
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
    bpb.hidden_sectors = 0;
    bpb.total_sectors_32 = ts;
    bpb.fat_size_32 = s_fat;
    bpb.ext_flags = 0;
    bpb.fs_version = 0;
    bpb.root_cluster = 2;
    bpb.fs_info = 1;
    bpb.backup_boot_sector = 6;
    bpb.drive_number = 0x80;
    bpb.boot_sig = FAT32_EXT_BOOT_SIG;
    bpb.volume_id = 0x5453554Bu; /* 'TSUK' hash */
    format_volume_label(label_arg, bpb.volume_label);
    memcpy(bpb.fs_type, "FAT32   ", 8);
    bpb.boot_signature = FAT32_BOOT_SIG;

    if (device_write_sectors(&dev, 0, 1, &bpb) != 0 ||
        device_write_sectors(&dev, 6, 1, &bpb) != 0) {
        fprintf(stderr, "mkfs.fat32: failed to write BPB\n");
        device_close(&dev);
        return 1;
    }

    /* 2. Build and write FSInfo (LBA 1 and backup LBA 7) */
    fat32_fsinfo_t fsinfo;
    memset(&fsinfo, 0, sizeof(fsinfo));

    fsinfo.lead_sig = FAT32_FSINFO_LEAD_SIG;
    fsinfo.struct_sig = FAT32_FSINFO_STRC_SIG;
    fsinfo.free_count = cluster_count - 1; /* Cluster 2 used for root */
    fsinfo.next_free = 3;
    fsinfo.trail_sig = FAT32_FSINFO_TRAIL_SIG;

    if (device_write_sectors(&dev, 1, 1, &fsinfo) != 0 ||
        device_write_sectors(&dev, 7, 1, &fsinfo) != 0) {
        fprintf(stderr, "mkfs.fat32: failed to write FSInfo\n");
        device_close(&dev);
        return 1;
    }

    /* 3. Initialize FAT1 and FAT2 tables */
    uint8_t zero_sec[512];
    memset(zero_sec, 0, sizeof(zero_sec));

    /* Prepare first sector of FAT (Entry 0, 1, 2) */
    uint32_t fat_head[128];
    memset(fat_head, 0, sizeof(fat_head));
    fat_head[0] = FAT32_MEDIA_FIXED;
    fat_head[1] = FAT32_EOC;
    fat_head[2] = FAT32_EOC; /* Root directory cluster chain end */

    uint32_t fat1_lba = rs;
    uint32_t fat2_lba = rs + s_fat;

    /* Write first sector of FAT1 and FAT2 */
    if (device_write_sectors(&dev, fat1_lba, 1, fat_head) != 0 ||
        device_write_sectors(&dev, fat2_lba, 1, fat_head) != 0) {
        fprintf(stderr, "mkfs.fat32: failed to write FAT table head\n");
        device_close(&dev);
        return 1;
    }

    /* Zero out remaining sectors of FAT1 and FAT2 */
    for (uint32_t s = 1; s < s_fat; s++) {
        if (device_write_sectors(&dev, fat1_lba + s, 1, zero_sec) != 0 ||
            device_write_sectors(&dev, fat2_lba + s, 1, zero_sec) != 0) {
            fprintf(stderr, "mkfs.fat32: failed to clear FAT tables\n");
            device_close(&dev);
            return 1;
        }
    }

    /* 4. Zero out Root Directory (Cluster 2) */
    uint32_t root_lba = rs + 2u * s_fat;
    for (uint32_t s = 0; s < spc; s++) {
        if (device_write_sectors(&dev, root_lba + s, 1, zero_sec) != 0) {
            fprintf(stderr, "mkfs.fat32: failed to clear root cluster\n");
            device_close(&dev);
            return 1;
        }
    }

    /* 5. If volume label provided, write volume label entry at byte 0 of cluster 2 */
    if (label_arg && label_arg[0]) {
        fat32_dir_entry_t label_entry;
        memset(&label_entry, 0, sizeof(label_entry));
        format_volume_label(label_arg, label_entry.name);
        label_entry.attr = FAT32_ATTR_VOLUME_ID;

        uint8_t root_sec0[512];
        memset(root_sec0, 0, sizeof(root_sec0));
        memcpy(root_sec0, &label_entry, sizeof(label_entry));

        if (device_write_sectors(&dev, root_lba, 1, root_sec0) != 0) {
            fprintf(stderr, "mkfs.fat32: failed to write volume label\n");
            device_close(&dev);
            return 1;
        }
    }

    device_sync(&dev);
    device_close(&dev);

    printf("mkfs.fat32: successfully formatted %s (FAT32, label '%.11s', %u clusters)\n",
           dev_path, bpb.volume_label, cluster_count);
    return 0;
}
