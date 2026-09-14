/*
 * Project Tsukasa — Ext2 Filesystem Formatter (mkfs.ext2)
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
#include "../include/ext2.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define BLOCK_SIZE         1024u
#define SECTORS_PER_BLOCK  (BLOCK_SIZE / SECTOR_SIZE)
#define BLOCKS_PER_GROUP   8192u
#define INODES_PER_GROUP   2048u
#define INODE_SIZE         128u
#define INODE_TABLE_BLOCKS ((INODES_PER_GROUP * INODE_SIZE) / BLOCK_SIZE) /* 256 */

static void print_usage(const char *prog)
{
    fprintf(stderr, "Usage: %s [-b block_size] [-L label] <device_or_image_file>\n", prog);
}

static int write_block(device_handle_t *dev, uint32_t block_num, const void *buf)
{
    uint64_t lba = (uint64_t)block_num * SECTORS_PER_BLOCK;
    return device_write_sectors(dev, lba, SECTORS_PER_BLOCK, buf);
}

int main(int argc, char *argv[])
{
    const char *dev_path = NULL;
    const char *label_arg = NULL;
    int verbose = 0;

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-L") == 0) {
            if (i + 1 >= argc) {
                print_usage(argv[0]);
                return 1;
            }
            label_arg = argv[++i];
        } else if (strcmp(argv[i], "-b") == 0) {
            if (i + 1 >= argc) {
                print_usage(argv[0]);
                return 1;
            }
            uint32_t bsz = (uint32_t)atoi(argv[++i]);
            if (bsz != 1024) {
                fprintf(stderr, "mkfs.ext2: only 1024-byte block size is currently supported\n");
                return 1;
            }
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
        fprintf(stderr, "mkfs.ext2: cannot open %s\n", dev_path);
        return 1;
    }

    uint64_t total_blocks_64 = dev.sector_count / SECTORS_PER_BLOCK;
    if (total_blocks_64 < 64) {
        fprintf(stderr, "mkfs.ext2: device %s is too small for Ext2\n", dev_path);
        device_close(&dev);
        return 1;
    }

    if (total_blocks_64 > 0xFFFFFFFFULL)
        total_blocks_64 = 0xFFFFFFFFULL;

    uint32_t total_blocks = (uint32_t)total_blocks_64;
    uint32_t num_groups = (total_blocks + BLOCKS_PER_GROUP - 1u) / BLOCKS_PER_GROUP;
    uint32_t total_inodes = num_groups * INODES_PER_GROUP;

    /* Group descriptor table size */
    uint32_t gdt_bytes = num_groups * sizeof(ext2_group_desc_t);
    uint32_t gdt_blocks = (gdt_bytes + BLOCK_SIZE - 1u) / BLOCK_SIZE;

    if (verbose) {
        char sz_buf[32];
        format_size(device_get_size(&dev), sz_buf, sizeof(sz_buf));
        printf("Formatting %s (%s, %u blocks, %u groups)\n",
               dev_path, sz_buf, total_blocks, num_groups);
    }

    ext2_group_desc_t *gdt = (ext2_group_desc_t *)calloc(num_groups, sizeof(ext2_group_desc_t));
    if (!gdt) {
        fprintf(stderr, "mkfs.ext2: out of memory\n");
        device_close(&dev);
        return 1;
    }

    uint32_t total_free_blocks = 0;
    uint32_t total_free_inodes = 0;

    /* Calculate layout for each group */
    for (uint32_t g = 0; g < num_groups; g++) {
        uint32_t grp_start = (g == 0) ? 1u : (g * BLOCKS_PER_GROUP);
        uint32_t grp_blocks = BLOCKS_PER_GROUP;
        if (g == 0)
            grp_blocks = BLOCKS_PER_GROUP - 1u; /* Block 0 is boot block */
        if (grp_start + grp_blocks > total_blocks)
            grp_blocks = total_blocks - grp_start;

        uint32_t cur = grp_start;
        /* Group 0 has superblock and GDT copy */
        if (g == 0) {
            cur += 1u;          /* Block 1: Superblock */
            cur += gdt_blocks;  /* Blocks 2..: GDT */
        }

        gdt[g].bg_block_bitmap = cur++;
        gdt[g].bg_inode_bitmap = cur++;
        gdt[g].bg_inode_table = cur;
        cur += INODE_TABLE_BLOCKS;

        uint32_t overhead = cur - grp_start;
        if (g == 0)
            overhead += 1u; /* Root directory data block */

        if (grp_blocks > overhead)
            gdt[g].bg_free_blocks_count = (uint16_t)(grp_blocks - overhead);
        else
            gdt[g].bg_free_blocks_count = 0;

        if (g == 0) {
            gdt[g].bg_free_inodes_count = (uint16_t)(INODES_PER_GROUP - 11u);
            gdt[g].bg_used_dirs_count = 1;
        } else {
            gdt[g].bg_free_inodes_count = (uint16_t)INODES_PER_GROUP;
            gdt[g].bg_used_dirs_count = 0;
        }

        total_free_blocks += gdt[g].bg_free_blocks_count;
        total_free_inodes += gdt[g].bg_free_inodes_count;
    }

    /* 1. Clear Block 0 (Boot block, bytes 0..1023) */
    uint8_t zero_block[BLOCK_SIZE];
    memset(zero_block, 0, sizeof(zero_block));
    if (write_block(&dev, 0, zero_block) != 0) {
        fprintf(stderr, "mkfs.ext2: failed to clear boot block\n");
        free(gdt);
        device_close(&dev);
        return 1;
    }

    /* 2. Build and write Superblock at Block 1 (offset 1024) */
    ext2_super_block_t sb;
    memset(&sb, 0, sizeof(sb));

    sb.s_inodes_count = total_inodes;
    sb.s_blocks_count = total_blocks;
    sb.s_r_blocks_count = (total_blocks * 5u) / 100u; /* 5% reserved */
    sb.s_free_blocks_count = total_free_blocks;
    sb.s_free_inodes_count = total_free_inodes;
    sb.s_first_data_block = 1;
    sb.s_log_block_size = 0; /* 1024 bytes */
    sb.s_log_frag_size = 0;
    sb.s_blocks_per_group = BLOCKS_PER_GROUP;
    sb.s_frags_per_group = BLOCKS_PER_GROUP;
    sb.s_inodes_per_group = INODES_PER_GROUP;
    sb.s_mtime = 0;
    sb.s_wtime = 0x50000000u;
    sb.s_mnt_count = 0;
    sb.s_max_mnt_count = 20;
    sb.s_magic = EXT2_SUPER_MAGIC;
    sb.s_state = EXT2_STATE_CLEAN;
    sb.s_errors = EXT2_ERRORS_CONTINUE;
    sb.s_minor_rev_level = 0;
    sb.s_lastcheck = 0x50000000u;
    sb.s_checkinterval = 0;
    sb.s_creator_os = 0; /* Linux */
    sb.s_rev_level = 0;
    sb.s_def_resuid = 0;
    sb.s_def_resgid = 0;

    if (write_block(&dev, 1, &sb) != 0) {
        fprintf(stderr, "mkfs.ext2: failed to write superblock\n");
        free(gdt);
        device_close(&dev);
        return 1;
    }

    /* 3. Write Group Descriptor Table starting at Block 2 */
    for (uint32_t b = 0; b < gdt_blocks; b++) {
        memset(zero_block, 0, sizeof(zero_block));
        size_t off = (size_t)b * BLOCK_SIZE;
        size_t rem = gdt_bytes - off;
        size_t chunk = rem < BLOCK_SIZE ? rem : BLOCK_SIZE;
        memcpy(zero_block, (const uint8_t *)gdt + off, chunk);
        if (write_block(&dev, 2 + b, zero_block) != 0) {
            fprintf(stderr, "mkfs.ext2: failed to write group descriptors\n");
            free(gdt);
            device_close(&dev);
            return 1;
        }
    }

    /* 4. Write Block Bitmap and Inode Bitmap for each group */
    for (uint32_t g = 0; g < num_groups; g++) {
        /* Block Bitmap */
        uint8_t block_bitmap[BLOCK_SIZE];
        memset(block_bitmap, 0, sizeof(block_bitmap));

        uint32_t grp_start = (g == 0) ? 1u : (g * BLOCKS_PER_GROUP);
        uint32_t grp_blocks = BLOCKS_PER_GROUP;
        if (g == 0)
            grp_blocks = BLOCKS_PER_GROUP - 1u;
        if (grp_start + grp_blocks > total_blocks)
            grp_blocks = total_blocks - grp_start;

        uint32_t used_in_grp = grp_blocks - gdt[g].bg_free_blocks_count;
        for (uint32_t bit = 0; bit < used_in_grp; bit++)
            block_bitmap[bit / 8u] |= (uint8_t)(1u << (bit % 8u));

        /* Mark trailing non-existent blocks as used */
        for (uint32_t bit = grp_blocks; bit < BLOCKS_PER_GROUP; bit++)
            block_bitmap[bit / 8u] |= (uint8_t)(1u << (bit % 8u));

        if (write_block(&dev, gdt[g].bg_block_bitmap, block_bitmap) != 0) {
            fprintf(stderr, "mkfs.ext2: failed to write block bitmap for group %u\n", g);
            free(gdt);
            device_close(&dev);
            return 1;
        }

        /* Inode Bitmap */
        uint8_t inode_bitmap[BLOCK_SIZE];
        memset(inode_bitmap, 0, sizeof(inode_bitmap));

        if (g == 0) {
            /* Mark Inodes 1..11 as used */
            for (uint32_t bit = 0; bit < 11; bit++)
                inode_bitmap[bit / 8u] |= (uint8_t)(1u << (bit % 8u));
        }

        /* Mark trailing non-existent inodes beyond INODES_PER_GROUP (if any) */
        for (uint32_t bit = INODES_PER_GROUP; bit < BLOCK_SIZE * 8u; bit++)
            inode_bitmap[bit / 8u] |= (uint8_t)(1u << (bit % 8u));

        if (write_block(&dev, gdt[g].bg_inode_bitmap, inode_bitmap) != 0) {
            fprintf(stderr, "mkfs.ext2: failed to write inode bitmap for group %u\n", g);
            free(gdt);
            device_close(&dev);
            return 1;
        }

        /* Zero out inode table blocks */
        memset(zero_block, 0, sizeof(zero_block));
        for (uint32_t t = 0; t < INODE_TABLE_BLOCKS; t++) {
            if (write_block(&dev, gdt[g].bg_inode_table + t, zero_block) != 0) {
                fprintf(stderr, "mkfs.ext2: failed to clear inode table for group %u\n", g);
                free(gdt);
                device_close(&dev);
                return 1;
            }
        }
    }

    /* 5. Initialize Root Inode (Inode 2) in Group 0's inode table */
    uint32_t root_data_block = gdt[0].bg_inode_table + INODE_TABLE_BLOCKS;

    ext2_inode_t root_inode;
    memset(&root_inode, 0, sizeof(root_inode));
    root_inode.i_mode = EXT2_S_IFDIR | 0755u;
    root_inode.i_uid = 0;
    root_inode.i_size = BLOCK_SIZE;
    root_inode.i_atime = 0x50000000u;
    root_inode.i_ctime = 0x50000000u;
    root_inode.i_mtime = 0x50000000u;
    root_inode.i_dtime = 0;
    root_inode.i_gid = 0;
    root_inode.i_links_count = 2;
    root_inode.i_blocks = SECTORS_PER_BLOCK; /* 512-byte blocks */
    root_inode.i_flags = 0;
    root_inode.i_block[0] = root_data_block;

    /* Inode 2 is at offset sizeof(ext2_inode_t) = 128 bytes in first block of table */
    uint8_t itbl_sec0[BLOCK_SIZE];
    memset(itbl_sec0, 0, sizeof(itbl_sec0));
    memcpy(itbl_sec0 + sizeof(ext2_inode_t), &root_inode, sizeof(root_inode));

    if (write_block(&dev, gdt[0].bg_inode_table, itbl_sec0) != 0) {
        fprintf(stderr, "mkfs.ext2: failed to write root inode\n");
        free(gdt);
        device_close(&dev);
        return 1;
    }

    /* 6. Write Root Data Block (entries for '.' and '..') */
    uint8_t root_dir_block[BLOCK_SIZE];
    memset(root_dir_block, 0, sizeof(root_dir_block));

    /* Entry 1: '.' */
    ext2_dir_entry_t *dot = (ext2_dir_entry_t *)root_dir_block;
    dot->inode = EXT2_ROOT_INO;
    dot->rec_len = 12;
    dot->name_len = 1;
    dot->file_type = EXT2_FT_DIR;
    dot->name[0] = '.';

    /* Entry 2: '..' */
    ext2_dir_entry_t *dotdot = (ext2_dir_entry_t *)(root_dir_block + 12);
    dotdot->inode = EXT2_ROOT_INO;
    dotdot->rec_len = (uint16_t)(BLOCK_SIZE - 12u);
    dotdot->name_len = 2;
    dotdot->file_type = EXT2_FT_DIR;
    dotdot->name[0] = '.';
    dotdot->name[1] = '.';

    if (write_block(&dev, root_data_block, root_dir_block) != 0) {
        fprintf(stderr, "mkfs.ext2: failed to write root directory data block\n");
        free(gdt);
        device_close(&dev);
        return 1;
    }

    device_sync(&dev);
    free(gdt);
    device_close(&dev);

    (void)label_arg;
    printf("mkfs.ext2: successfully formatted %s (%u blocks, %u inodes)\n",
           dev_path, total_blocks, total_inodes);
    return 0;
}
