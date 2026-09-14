/*
 * Project Tsukasa — OS Installer & Limine Deployer (installer)
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
#include "../include/mbr.h"
#include "../include/fat32.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#define PART_START_LBA       2048u
#define BOOTBLOB_SECTORS     2048u
#define MIN_INSTALL_SECTORS  (64u * 2048u) /* 64 MiB minimum */

static const char *const g_candidate_devices[] = {
    "/dev/hda",
    "/dev/sda",
    "/dev/vda",
    "/dev/hdb",
    "/dev/sdb",
    "/dev/vdb",
};

static const char *const g_bootblob_paths[] = {
    "bootblob.bin",
    "/bootblob.bin",
    "/boot/bootblob.bin",
    "/boot/limine/bootblob.bin",
};

static const char *const g_kernel_paths[] = {
    "/boot/tsukasa_x64.elf",
    "/tsukasa_x64.elf",
    "tsukasa_x64.elf",
};

static const char *const g_initrd_paths[] = {
    "/boot/initrd.img",
    "/initrd.img",
    "initrd.img",
};

static const char *const g_limine_sys_paths[] = {
    "/boot/limine/limine-bios.sys",
    "/limine-bios.sys",
    "/boot/limine-bios.sys",
    "limine-bios.sys",
};

#ifndef O_BINARY
#define O_BINARY 0
#endif

static int find_file(const char *const paths[], size_t count, char *out_path, size_t max)
{
    for (size_t i = 0; i < count; i++) {
        int fd = open(paths[i], O_RDONLY | O_BINARY);
        if (fd >= 0) {
            close(fd);
            strncpy(out_path, paths[i], max - 1);
            out_path[max - 1] = '\0';
            return 0;
        }
    }
    return -1;
}

static int read_all_file(const char *path, uint8_t **out_buf, size_t *out_size)
{
    int fd = open(path, O_RDONLY | O_BINARY);
    if (fd < 0)
        return -1;

    off_t sz = lseek(fd, 0, SEEK_END);
    if (sz < 0) {
        close(fd);
        return -1;
    }
    lseek(fd, 0, SEEK_SET);

    uint8_t *buf = (uint8_t *)malloc((size_t)sz);
    if (!buf && sz > 0) {
        close(fd);
        return -1;
    }

    size_t read_bytes = 0;
    while (read_bytes < (size_t)sz) {
        ssize_t n = read(fd, buf + read_bytes, (size_t)sz - read_bytes);
        if (n <= 0) {
            free(buf);
            close(fd);
            return -1;
        }
        read_bytes += (size_t)n;
    }

    close(fd);
    *out_buf = buf;
    *out_size = (size_t)sz;
    return 0;
}

static void convert_to_83(const char *src, char dst[11])
{
    memset(dst, ' ', 11);
    const char *dot = strrchr(src, '.');
    size_t name_len = dot ? (size_t)(dot - src) : strlen(src);
    if (name_len > 8)
        name_len = 8;

    for (size_t i = 0; i < name_len; i++)
        dst[i] = (char)toupper((unsigned char)src[i]);

    if (dot) {
        const char *ext = dot + 1;
        size_t ext_len = strlen(ext);
        if (ext_len > 3)
            ext_len = 3;
        for (size_t i = 0; i < ext_len; i++)
            dst[8 + i] = (char)toupper((unsigned char)ext[i]);
    }
}

/* In-memory FAT32 volume builder & injector for installation payload */
typedef struct {
    device_handle_t *dev;
    uint32_t         part_start_lba;
    uint32_t         total_sectors;
    uint32_t         reserved_sectors;
    uint32_t         spc;
    uint32_t         fat_size;
    uint32_t         cluster_count;
    uint32_t         next_free_cluster;
    uint32_t         root_dir_entry_count;
    fat32_bpb_t      bpb;
    fat32_fsinfo_t   fsinfo;
} fat32_writer_t;

static int fat32_writer_init(fat32_writer_t *w, device_handle_t *dev, uint32_t start_lba, uint32_t sec_count)
{
    memset(w, 0, sizeof(*w));
    w->dev = dev;
    w->part_start_lba = start_lba;
    w->total_sectors = sec_count;
    w->reserved_sectors = 32;

    /* Adaptive cluster size heuristic */
    if (sec_count < 532480u)
        w->spc = 1;
    else if (sec_count < 1064960u)
        w->spc = 2;
    else if (sec_count < 2097152u)
        w->spc = 4;
    else if (sec_count < 16777216u)
        w->spc = 8;
    else if (sec_count < 33554432u)
        w->spc = 16;
    else
        w->spc = 32;

    uint32_t ds = sec_count - w->reserved_sectors;
    uint64_t fat_bytes = ((uint64_t)(ds / w->spc) + 2ULL) * 4ULL;
    w->fat_size = (uint32_t)((fat_bytes + 511ULL) / 512ULL);

    if (sec_count <= w->reserved_sectors + 2u * w->fat_size)
        return -1;

    w->cluster_count = (sec_count - (w->reserved_sectors + 2u * w->fat_size)) / w->spc;
    if (w->cluster_count < FAT32_MIN_CLUSTERS)
        return -1;

    w->next_free_cluster = 3; /* Cluster 2 is root */
    w->root_dir_entry_count = 0;

    /* Build BPB */
    w->bpb.jump_boot[0] = 0xEB;
    w->bpb.jump_boot[1] = 0x58;
    w->bpb.jump_boot[2] = 0x90;
    memcpy(w->bpb.oem_name, "TSUKASA ", 8);
    w->bpb.bytes_per_sector = 512;
    w->bpb.sectors_per_cluster = (uint8_t)w->spc;
    w->bpb.reserved_sector_count = (uint16_t)w->reserved_sectors;
    w->bpb.num_fats = 2;
    w->bpb.root_entry_count = 0;
    w->bpb.total_sectors_16 = 0;
    w->bpb.media = 0xF8;
    w->bpb.fat_size_16 = 0;
    w->bpb.sectors_per_track = 63;
    w->bpb.num_heads = 255;
    w->bpb.hidden_sectors = start_lba;
    w->bpb.total_sectors_32 = sec_count;
    w->bpb.fat_size_32 = w->fat_size;
    w->bpb.ext_flags = 0;
    w->bpb.fs_version = 0;
    w->bpb.root_cluster = 2;
    w->bpb.fs_info = 1;
    w->bpb.backup_boot_sector = 6;
    w->bpb.drive_number = 0x80;
    w->bpb.boot_sig = FAT32_EXT_BOOT_SIG;
    w->bpb.volume_id = 0x5453554Bu;
    memcpy(w->bpb.volume_label, "TSUKASA_OS ", 11);
    memcpy(w->bpb.fs_type, "FAT32   ", 8);
    w->bpb.boot_signature = FAT32_BOOT_SIG;

    /* Build FSInfo */
    w->fsinfo.lead_sig = FAT32_FSINFO_LEAD_SIG;
    w->fsinfo.struct_sig = FAT32_FSINFO_STRC_SIG;
    w->fsinfo.free_count = w->cluster_count - 1;
    w->fsinfo.next_free = 3;
    w->fsinfo.trail_sig = FAT32_FSINFO_TRAIL_SIG;

    /* Write BPB */
    if (device_write_sectors(dev, start_lba, 1, &w->bpb) != 0 ||
        device_write_sectors(dev, start_lba + 6, 1, &w->bpb) != 0)
        return -1;

    /* Write FSInfo */
    if (device_write_sectors(dev, start_lba + 1, 1, &w->fsinfo) != 0 ||
        device_write_sectors(dev, start_lba + 7, 1, &w->fsinfo) != 0)
        return -1;

    /* Initialize FAT tables head */
    uint32_t fat_head[128];
    memset(fat_head, 0, sizeof(fat_head));
    fat_head[0] = FAT32_MEDIA_FIXED;
    fat_head[1] = FAT32_EOC;
    fat_head[2] = FAT32_EOC;

    uint32_t fat1_lba = start_lba + w->reserved_sectors;
    uint32_t fat2_lba = fat1_lba + w->fat_size;
    if (device_write_sectors(dev, fat1_lba, 1, fat_head) != 0 ||
        device_write_sectors(dev, fat2_lba, 1, fat_head) != 0)
        return -1;

    uint8_t zero_sec[512];
    memset(zero_sec, 0, sizeof(zero_sec));
    for (uint32_t s = 1; s < w->fat_size; s++) {
        if (device_write_sectors(dev, fat1_lba + s, 1, zero_sec) != 0 ||
            device_write_sectors(dev, fat2_lba + s, 1, zero_sec) != 0)
            return -1;
    }

    /* Clear Cluster 2 (Root directory) */
    uint32_t root_lba = start_lba + w->reserved_sectors + 2u * w->fat_size;
    for (uint32_t s = 0; s < w->spc; s++) {
        if (device_write_sectors(dev, root_lba + s, 1, zero_sec) != 0)
            return -1;
    }

    return 0;
}

static int fat32_writer_add_file(fat32_writer_t *w, const char *dos_name, const void *data, size_t size)
{
    if (!w || !dos_name)
        return -1;

    uint32_t cluster_bytes = w->spc * SECTOR_SIZE;
    uint32_t num_clusters = (size > 0) ? (uint32_t)((size + cluster_bytes - 1) / cluster_bytes) : 0;

    uint32_t first_cluster = 0;
    if (num_clusters > 0) {
        first_cluster = w->next_free_cluster;
        w->next_free_cluster += num_clusters;

        /* Write data to clusters */
        size_t written = 0;
        for (uint32_t i = 0; i < num_clusters; i++) {
            uint32_t clus = first_cluster + i;
            uint32_t clus_lba = w->part_start_lba + w->reserved_sectors +
                                2u * w->fat_size + (clus - 2u) * w->spc;

            uint8_t clus_buf[64 * 512]; /* Max spc=64 */
            memset(clus_buf, 0, sizeof(clus_buf));

            size_t rem = size - written;
            size_t chunk = rem < cluster_bytes ? rem : cluster_bytes;
            if (data && chunk > 0)
                memcpy(clus_buf, (const uint8_t *)data + written, chunk);

            if (device_write_sectors(w->dev, clus_lba, w->spc, clus_buf) != 0)
                return -1;

            written += chunk;

            /* Update FAT entry */
            uint32_t next_val = (i == num_clusters - 1) ? FAT32_EOC : (clus + 1);
            uint32_t fat_offset_bytes = clus * 4u;
            uint32_t fat_sector_idx = fat_offset_bytes / SECTOR_SIZE;
            uint32_t entry_in_sec = (fat_offset_bytes % SECTOR_SIZE) / 4u;

            uint32_t sec_buf[128];
            uint32_t fat1_sec_lba = w->part_start_lba + w->reserved_sectors + fat_sector_idx;
            uint32_t fat2_sec_lba = fat1_sec_lba + w->fat_size;

            if (device_read_sectors(w->dev, fat1_sec_lba, 1, sec_buf) != 0)
                return -1;

            sec_buf[entry_in_sec] = next_val;

            if (device_write_sectors(w->dev, fat1_sec_lba, 1, sec_buf) != 0 ||
                device_write_sectors(w->dev, fat2_sec_lba, 1, sec_buf) != 0)
                return -1;
        }
    }

    /* Add entry to Root Directory Cluster 2 */
    uint32_t dir_sector = w->root_dir_entry_count / (SECTOR_SIZE / 32u);
    uint32_t dir_idx = w->root_dir_entry_count % (SECTOR_SIZE / 32u);

    if (dir_sector >= w->spc) {
        fprintf(stderr, "Root directory overflow\n");
        return -1;
    }

    uint32_t root_sec_lba = w->part_start_lba + w->reserved_sectors + 2u * w->fat_size + dir_sector;
    fat32_dir_entry_t dir_entries[16];
    if (device_read_sectors(w->dev, root_sec_lba, 1, dir_entries) != 0)
        return -1;

    fat32_dir_entry_t *de = &dir_entries[dir_idx];
    memset(de, 0, sizeof(*de));
    convert_to_83(dos_name, de->name);
    de->attr = FAT32_ATTR_ARCHIVE;
    de->fst_clus_hi = (uint16_t)(first_cluster >> 16);
    de->fst_clus_lo = (uint16_t)(first_cluster & 0xFFFFu);
    de->file_size = (uint32_t)size;

    if (device_write_sectors(w->dev, root_sec_lba, 1, dir_entries) != 0)
        return -1;

    w->root_dir_entry_count++;

    /* Update FSInfo */
    if (num_clusters > 0 && w->fsinfo.free_count >= num_clusters)
        w->fsinfo.free_count -= num_clusters;
    w->fsinfo.next_free = w->next_free_cluster;

    device_write_sectors(w->dev, w->part_start_lba + 1, 1, &w->fsinfo);
    device_write_sectors(w->dev, w->part_start_lba + 7, 1, &w->fsinfo);

    return 0;
}

int main(int argc, char *argv[])
{
    const char *target_dev_path = NULL;
    int non_interactive = 0;

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-y") == 0 || strcmp(argv[i], "--non-interactive") == 0) {
            non_interactive = 1;
        } else if (argv[i][0] != '-') {
            target_dev_path = argv[i];
        }
    }

    printf("====================================================\n");
    printf("        Project Tsukasa — OS Installer Wizard        \n");
    printf("====================================================\n\n");

    /* 1. Drive Discovery */
    if (!target_dev_path) {
        printf("[1/6] Scanning system for block devices...\n");
        for (size_t i = 0; i < sizeof(g_candidate_devices) / sizeof(g_candidate_devices[0]); i++) {
            device_handle_t d;
            if (device_open(g_candidate_devices[i], O_RDWR, &d) == 0) {
                if (d.sector_count >= MIN_INSTALL_SECTORS) {
                    char sz[32];
                    format_size(device_get_size(&d), sz, sizeof(sz));
                    printf("  Found suitable disk: %s (%s, %llu sectors)\n",
                           g_candidate_devices[i], sz, (unsigned long long)d.sector_count);
                    if (!target_dev_path)
                        target_dev_path = g_candidate_devices[i];
                }
                device_close(&d);
            }
        }
    }

    if (!target_dev_path) {
        fprintf(stderr, "Error: No suitable install disk found. Please specify device (e.g. /dev/hda).\n");
        return 1;
    }

    device_handle_t dev;
    if (device_open(target_dev_path, O_RDWR, &dev) != 0) {
        fprintf(stderr, "Error: Failed to open target device %s\n", target_dev_path);
        return 1;
    }

    char disk_sz[32];
    format_size(device_get_size(&dev), disk_sz, sizeof(disk_sz));
    printf("Target Drive: %s (%s, %llu sectors)\n",
           target_dev_path, disk_sz, (unsigned long long)dev.sector_count);

    if (dev.sector_count < MIN_INSTALL_SECTORS) {
        fprintf(stderr, "Error: Target disk smaller than minimum required 64 MiB.\n");
        device_close(&dev);
        return 1;
    }

    /* 2. Target Confirmation */
    if (!non_interactive) {
        printf("\nWARNING: All existing data on %s will be permanently lost!\n", target_dev_path);
        printf("Proceed with installation? (y/N): ");
        char ans[16];
        memset(ans, 0, sizeof(ans));
        read(STDIN_FILENO, ans, sizeof(ans) - 1);
        if (ans[0] != 'y' && ans[0] != 'Y') {
            printf("Installation aborted by user.\n");
            device_close(&dev);
            return 0;
        }
    }

    /* 3. Partitioning: Create Primary Partition 1 starting at LBA 2048 */
    printf("\n[2/6] Writing MBR partition table...\n");
    uint32_t part_sectors = (uint32_t)(dev.sector_count - PART_START_LBA);
    mbr_sector_t mbr;
    memset(&mbr, 0, sizeof(mbr));

    mbr.entries[0].boot_indicator = MBR_BOOTABLE;
    mbr.entries[0].partition_type = MBR_TYPE_FAT32_LBA;
    mbr.entries[0].lba_start = PART_START_LBA;
    mbr.entries[0].sector_count = part_sectors;

    lba_to_chs(PART_START_LBA, &mbr.entries[0].start_head,
               &mbr.entries[0].start_sector, &mbr.entries[0].start_cylinder);
    lba_to_chs(PART_START_LBA + part_sectors - 1, &mbr.entries[0].end_head,
               &mbr.entries[0].end_sector, &mbr.entries[0].end_cylinder);

    mbr.boot_signature = MBR_SIGNATURE;

    if (device_write_sectors(&dev, 0, 1, &mbr) != 0) {
        fprintf(stderr, "Error: Failed to write MBR to %s\n", target_dev_path);
        device_close(&dev);
        return 1;
    }
    device_sync(&dev);

    /* 4. Formatting: Synthesize FAT32 Filesystem at LBA 2048 */
    printf("[3/6] Formatting FAT32 partition (TSUKASA_OS)...\n");
    fat32_writer_t fat_writer;
    if (fat32_writer_init(&fat_writer, &dev, PART_START_LBA, part_sectors) != 0) {
        fprintf(stderr, "Error: FAT32 partition formatting failed\n");
        device_close(&dev);
        return 1;
    }

    /* 5. Payload Deployment */
    printf("[4/6] Copying OS boot payload...\n");
    char found_path[128];

    /* Kernel */
    if (find_file(g_kernel_paths, sizeof(g_kernel_paths) / sizeof(g_kernel_paths[0]), found_path, sizeof(found_path)) == 0) {
        uint8_t *kdata = NULL;
        size_t ksize = 0;
        if (read_all_file(found_path, &kdata, &ksize) == 0) {
            printf("  Installing %s (%u bytes)...\n", found_path, (unsigned)ksize);
            fat32_writer_add_file(&fat_writer, "tsukasa.elf", kdata, ksize);
            free(kdata);
        }
    }

    /* Initrd */
    if (find_file(g_initrd_paths, sizeof(g_initrd_paths) / sizeof(g_initrd_paths[0]), found_path, sizeof(found_path)) == 0) {
        uint8_t *idata = NULL;
        size_t isize = 0;
        if (read_all_file(found_path, &idata, &isize) == 0) {
            printf("  Installing %s (%u bytes)...\n", found_path, (unsigned)isize);
            fat32_writer_add_file(&fat_writer, "initrd.img", idata, isize);
            free(idata);
        }
    }

    /* Limine BIOS System Stage */
    if (find_file(g_limine_sys_paths, sizeof(g_limine_sys_paths) / sizeof(g_limine_sys_paths[0]), found_path, sizeof(found_path)) == 0) {
        uint8_t *ldata = NULL;
        size_t lsize = 0;
        if (read_all_file(found_path, &ldata, &lsize) == 0) {
            printf("  Installing %s (%u bytes)...\n", found_path, (unsigned)lsize);
            fat32_writer_add_file(&fat_writer, "limine.sys", ldata, lsize);
            free(ldata);
        }
    }

    /* Limine Config */
    static const char def_limine_cfg[] =
        "timeout: 2\n"
        "verbose: yes\n"
        "\n"
        "/Project Tsukasa (Installed)\n"
        "    protocol: limine\n"
        "    kernel_path: boot():/tsukasa.elf\n"
        "    cmdline: arch=x86_64 single_core=1\n"
        "    module_path: boot():/initrd.img\n";
    printf("  Writing limine.cfg...\n");
    fat32_writer_add_file(&fat_writer, "limine.cfg", def_limine_cfg, sizeof(def_limine_cfg) - 1);

    device_sync(&dev);

    /* 6. Limine Bootloader Deployment (Stage 1 & 2) */
    printf("[5/6] Deploying Limine BIOS bootloader stages...\n");
    if (find_file(g_bootblob_paths, sizeof(g_bootblob_paths) / sizeof(g_bootblob_paths[0]), found_path, sizeof(found_path)) == 0) {
        uint8_t *blob = NULL;
        size_t blob_sz = 0;
        if (read_all_file(found_path, &blob, &blob_sz) == 0) {
            printf("  Found bootloader blob %s (%u bytes)\n", found_path, (unsigned)blob_sz);

            /* Read current LBA 0 to preserve partition table */
            mbr_sector_t current_mbr;
            device_read_sectors(&dev, 0, 1, &current_mbr);

            /* Overwrite only bytes 0..445 with Stage 1 */
            memcpy(current_mbr.boot_code, blob, 446);
            current_mbr.boot_signature = MBR_SIGNATURE;
            device_write_sectors(&dev, 0, 1, &current_mbr);

            /* Write Stage 2 to reserved sectors (LBA 1..2047) */
            if (blob_sz > 512) {
                size_t stage2_bytes = blob_sz - 512;
                uint32_t stage2_sectors = (uint32_t)((stage2_bytes + 511) / 512);
                if (stage2_sectors > 2047)
                    stage2_sectors = 2047;

                device_write_sectors(&dev, 1, stage2_sectors, blob + 512);
            }
            free(blob);
        }
    } else {
        printf("  Note: bootblob.bin not found on live media. Skipping BIOS stage injection.\n");
    }

    /* 7. Final Sync */
    printf("[6/6] Syncing target disk cache...\n");
    device_sync(&dev);
    device_close(&dev);

    printf("\n====================================================\n");
    printf("  Installation Complete! Tsukasa OS is ready to boot.\n");
    printf("  You can now reboot and remove installation media.   \n");
    printf("====================================================\n");

    return 0;
}
