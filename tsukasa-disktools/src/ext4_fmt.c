/*
 * Project Tsukasa - Ext4 Filesystem Formatter Core
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
#include "../include/gpt.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define EXT4_BLK_SIZE          4096u
#define EXT4_SEC_PER_BLK       (EXT4_BLK_SIZE / SECTOR_SIZE)
#define EXT4_BLKS_PER_GRP      32768u
#define EXT4_INODE_SIZE_V1     256u
#define EXT4_JOURNAL_INO       8u
#define EXT4_EXTENT_MAGIC      0xF30Au
#define EXT4_INODE_EXTENTS     0x00080000u

#define EXT4_FCOM_HAS_JOURNAL  0x0004u
#define EXT4_FINCOM_FILETYPE   0x0002u
#define EXT4_FINCOM_EXTENTS    0x0040u
#define EXT4_FRO_SPARSE_SUPER  0x0001u
#define EXT4_FRO_LARGE_FILE    0x0002u
#define EXT4_FRO_HUGE_FILE     0x0008u
#define EXT4_FRO_DIR_NLINK     0x0020u
#define EXT4_FRO_EXTRA_ISIZE   0x0040u

#define JBD2_MAGIC             0xC03B3998u
#define JBD2_SUPERBLOCK_V2     4u

#pragma pack(push, 1)
typedef struct {
    uint32_t s_inodes_count;
    uint32_t s_blocks_count_lo;
    uint32_t s_r_blocks_count_lo;
    uint32_t s_free_blocks_count_lo;
    uint32_t s_free_inodes_count;
    uint32_t s_first_data_block;
    uint32_t s_log_block_size;
    uint32_t s_log_cluster_size;
    uint32_t s_blocks_per_group;
    uint32_t s_frags_per_group;
    uint32_t s_inodes_per_group;
    uint32_t s_mtime;
    uint32_t s_wtime;
    uint16_t s_mnt_count;
    uint16_t s_max_mnt_count;
    uint16_t s_magic;
    uint16_t s_state;
    uint16_t s_errors;
    uint16_t s_minor_rev_level;
    uint32_t s_lastcheck;
    uint32_t s_checkinterval;
    uint32_t s_creator_os;
    uint32_t s_rev_level;
    uint16_t s_def_resuid;
    uint16_t s_def_resgid;
    uint32_t s_first_ino;
    uint16_t s_inode_size;
    uint16_t s_block_group_nr;
    uint32_t s_feature_compat;
    uint32_t s_feature_incompat;
    uint32_t s_feature_ro_compat;
    uint8_t  s_uuid[16];
    char     s_volume_name[16];
    char     s_last_mounted[64];
    uint32_t s_algorithm_usage_bitmap;
    uint8_t  s_prealloc_blocks;
    uint8_t  s_prealloc_dir_blocks;
    uint16_t s_reserved_gdt_blocks;
    uint8_t  s_journal_uuid[16];
    uint32_t s_journal_inum;
    uint32_t s_journal_dev;
    uint32_t s_last_orphan;
    uint32_t s_hash_seed[4];
    uint8_t  s_def_hash_version;
    uint8_t  s_jnl_backup_type;
    uint16_t s_desc_size;
    uint32_t s_default_mount_opts;
    uint32_t s_first_meta_bg;
    uint32_t s_mkfs_time;
    uint32_t s_jnl_blocks[17];
    uint32_t s_blocks_count_hi;
    uint32_t s_r_blocks_count_hi;
    uint32_t s_free_blocks_count_hi;
    uint16_t s_min_extra_isize;
    uint16_t s_want_extra_isize;
    uint32_t s_flags;
    uint8_t  s_reserved[668];
} ext4_sb_raw_t;

typedef struct {
    uint16_t i_mode;
    uint16_t i_uid;
    uint32_t i_size_lo;
    uint32_t i_atime;
    uint32_t i_ctime;
    uint32_t i_mtime;
    uint32_t i_dtime;
    uint16_t i_gid;
    uint16_t i_links_count;
    uint32_t i_blocks_lo;
    uint32_t i_flags;
    uint32_t i_osd1;
    uint32_t i_block[15];
    uint32_t i_generation;
    uint32_t i_file_acl_lo;
    uint32_t i_size_hi;
    uint32_t i_obso_faddr;
    uint16_t i_blocks_high;
    uint16_t i_file_acl_high;
    uint16_t i_uid_high;
    uint16_t i_gid_high;
    uint16_t i_checksum_lo;
    uint16_t i_reserved2;
    uint16_t i_extra_isize;
    uint16_t i_checksum_hi;
    uint32_t i_ctime_extra;
    uint32_t i_mtime_extra;
    uint32_t i_atime_extra;
    uint32_t i_crtime;
    uint32_t i_crtime_extra;
    uint32_t i_version_hi;
    uint8_t  i_pad[100];
} ext4_inode_raw_t;

typedef struct {
    uint16_t eh_magic;
    uint16_t eh_entries;
    uint16_t eh_max;
    uint16_t eh_depth;
    uint32_t eh_generation;
} ext4_extent_hdr_t;

typedef struct {
    uint32_t ee_block;
    uint16_t ee_len;
    uint16_t ee_start_hi;
    uint32_t ee_start_lo;
} ext4_extent_leaf_t;

typedef struct {
    uint32_t h_magic;
    uint32_t h_blocktype;
    uint32_t h_sequence;
    uint32_t s_blocksize;
    uint32_t s_maxlen;
    uint32_t s_first;
    uint32_t s_sequence;
    uint32_t s_start;
    int32_t  s_errno;
    uint32_t s_feature_compat;
    uint32_t s_feature_incompat;
    uint32_t s_feature_ro_compat;
    uint8_t  s_uuid[16];
    uint32_t s_nr_users;
    uint32_t s_dynsuper;
    uint32_t s_max_transaction;
    uint32_t s_max_trandata;
    uint8_t  s_checksum_type;
    uint8_t  s_padding2[3];
    uint32_t s_padding[42];
    uint32_t s_checksum;
    uint8_t  s_users[16 * 48];
} jbd2_sb_raw_t;
#pragma pack(pop)

_Static_assert(sizeof(ext4_sb_raw_t) == 1024, "Ext4 superblock must be 1024 bytes");
_Static_assert(sizeof(ext4_inode_raw_t) == 256, "Ext4 inode must be 256 bytes");
_Static_assert(sizeof(jbd2_sb_raw_t) == 1024, "JBD2 superblock must be 1024 bytes");

static uint32_t be32(uint32_t v)
{
    return ((v & 0x000000FFu) << 24) |
           ((v & 0x0000FF00u) << 8) |
           ((v & 0x00FF0000u) >> 8) |
           ((v & 0xFF000000u) >> 24);
}

static int is_pow_of(uint32_t n, uint32_t base)
{
    if (n < base)
        return 0;
    while (n % base == 0)
        n /= base;
    return n == 1u;
}

static int has_sparse_super(uint32_t g)
{
    return g == 0u || g == 1u ||
           is_pow_of(g, 3u) || is_pow_of(g, 5u) || is_pow_of(g, 7u);
}

static int write_blk(device_handle_t *dev, uint32_t blk, const void *buf)
{
    return device_write_sectors(dev, (uint64_t)blk * EXT4_SEC_PER_BLK, EXT4_SEC_PER_BLK, buf);
}

static int write_zero_blks(device_handle_t *dev, uint32_t start_blk, uint32_t count)
{
    static uint8_t zbuf[4 * EXT4_BLK_SIZE];
    uint32_t done = 0;
    while (done < count) {
        uint32_t chunk = count - done;
        if (chunk > 4u)
            chunk = 4u;
        if (device_write_sectors(dev, (uint64_t)(start_blk + done) * EXT4_SEC_PER_BLK,
                                 chunk * EXT4_SEC_PER_BLK, zbuf) != 0)
            return -1;
        done += chunk;
    }
    return 0;
}

static void init_extent_inode(ext4_inode_raw_t *ino, uint16_t mode, uint64_t size_bytes,
                              uint16_t links, uint32_t phys_blk, uint16_t blk_cnt,
                              uint32_t now)
{
    memset(ino, 0, sizeof(*ino));
    ino->i_mode = mode;
    ino->i_size_lo = (uint32_t)(size_bytes & 0xFFFFFFFFULL);
    ino->i_size_hi = (uint32_t)(size_bytes >> 32);
    ino->i_atime = now;
    ino->i_ctime = now;
    ino->i_mtime = now;
    ino->i_crtime = now;
    ino->i_links_count = links;
    ino->i_blocks_lo = (uint32_t)blk_cnt * EXT4_SEC_PER_BLK;
    ino->i_flags = EXT4_INODE_EXTENTS;
    ino->i_extra_isize = 32u;

    ext4_extent_hdr_t *eh = (ext4_extent_hdr_t *)ino->i_block;
    eh->eh_magic = EXT4_EXTENT_MAGIC;
    eh->eh_entries = 1u;
    eh->eh_max = 4u;
    eh->eh_depth = 0u;
    eh->eh_generation = 0u;

    ext4_extent_leaf_t *ee = (ext4_extent_leaf_t *)(ino->i_block + 3);
    ee->ee_block = 0u;
    ee->ee_len = blk_cnt;
    ee->ee_start_hi = 0u;
    ee->ee_start_lo = phys_blk;
}

int ext4_format_device(device_handle_t *dev, const char *label, int verbose)
{
    if (!dev || dev->fd < 0)
        return -1;

    uint64_t total_blocks_64 = dev->sector_count / EXT4_SEC_PER_BLK;
    if (total_blocks_64 < 2048ULL) {
        fprintf(stderr, "mkfs.ext4: device %s is too small (need >= 8 MiB)\n", dev->path);
        return -1;
    }
    if (total_blocks_64 > 0xFFFFFFFFULL)
        total_blocks_64 = 0xFFFFFFFFULL;

    uint32_t total_blocks = (uint32_t)total_blocks_64;
    uint32_t inodes_per_group = 4096u;
    if (total_blocks < 8192u)
        inodes_per_group = 1024u;
    else if (total_blocks < 16384u)
        inodes_per_group = 2048u;

    uint32_t inode_table_blocks = (inodes_per_group * EXT4_INODE_SIZE_V1) / EXT4_BLK_SIZE;
    uint32_t journal_blocks = 1024u;
    if (total_blocks < 4096u)
        journal_blocks = 256u;
    else if (total_blocks < 8192u)
        journal_blocks = 512u;

    uint32_t num_groups = (total_blocks + EXT4_BLKS_PER_GRP - 1u) / EXT4_BLKS_PER_GRP;
    uint32_t gdt_bytes = num_groups * (uint32_t)sizeof(ext2_group_desc_t);
    uint32_t gdt_blocks = (gdt_bytes + EXT4_BLK_SIZE - 1u) / EXT4_BLK_SIZE;

    if (num_groups > 1u) {
        uint32_t last_grp = num_groups - 1u;
        uint32_t last_blks = total_blocks - last_grp * EXT4_BLKS_PER_GRP;
        uint32_t min_last = (has_sparse_super(last_grp) ? (1u + gdt_blocks) : 0u) +
                            2u + inode_table_blocks + 4u;
        if (last_blks < min_last) {
            num_groups--;
            total_blocks = num_groups * EXT4_BLKS_PER_GRP;
            gdt_bytes = num_groups * (uint32_t)sizeof(ext2_group_desc_t);
            gdt_blocks = (gdt_bytes + EXT4_BLK_SIZE - 1u) / EXT4_BLK_SIZE;
        }
    }

    uint32_t total_inodes = num_groups * inodes_per_group;
    ext2_group_desc_t *gdt = (ext2_group_desc_t *)calloc(num_groups, sizeof(ext2_group_desc_t));
    if (!gdt) {
        fprintf(stderr, "mkfs.ext4: out of memory\n");
        return -1;
    }

    uint32_t total_free_blocks = 0;
    uint32_t total_free_inodes = 0;
    uint32_t root_data_block = 0;
    uint32_t journal_start_block = 0;

    for (uint32_t g = 0; g < num_groups; g++) {
        uint32_t grp_start = g * EXT4_BLKS_PER_GRP;
        uint32_t grp_blocks = EXT4_BLKS_PER_GRP;
        if (grp_start + grp_blocks > total_blocks)
            grp_blocks = total_blocks - grp_start;

        uint32_t cur = grp_start;
        if (has_sparse_super(g))
            cur += 1u + gdt_blocks;

        gdt[g].bg_block_bitmap = cur++;
        gdt[g].bg_inode_bitmap = cur++;
        gdt[g].bg_inode_table = cur;
        cur += inode_table_blocks;

        if (g == 0u) {
            root_data_block = cur++;
            journal_start_block = cur;
            cur += journal_blocks;
        }

        uint32_t overhead = cur - grp_start;
        if (grp_blocks <= overhead) {
            fprintf(stderr, "mkfs.ext4: insufficient space in group %u\n", g);
            free(gdt);
            return -1;
        }
        gdt[g].bg_free_blocks_count = (uint16_t)(grp_blocks - overhead);

        if (g == 0u) {
            gdt[g].bg_free_inodes_count = (uint16_t)(inodes_per_group - 10u);
            gdt[g].bg_used_dirs_count = 1u;
        } else {
            gdt[g].bg_free_inodes_count = (uint16_t)inodes_per_group;
            gdt[g].bg_used_dirs_count = 0u;
        }

        total_free_blocks += gdt[g].bg_free_blocks_count;
        total_free_inodes += gdt[g].bg_free_inodes_count;
    }

    if (verbose) {
        char sz_buf[32];
        format_size(device_get_size(dev), sz_buf, sizeof(sz_buf));
        printf("Formatting %s (%s, %u 4KiB blocks, %u groups, %u journal blocks)\n",
               dev->path, sz_buf, total_blocks, num_groups, journal_blocks);
    }

    uint32_t now = (uint32_t)time(NULL);
    if (now < 0x50000000u)
        now = 0x67000000u;

    uint8_t fs_uuid[16];
    uint8_t jnl_uuid[16];
    uint8_t seed_bytes[16];
    gpt_gen_uuid(fs_uuid);
    gpt_gen_uuid(jnl_uuid);
    gpt_gen_uuid(seed_bytes);

    ext4_inode_raw_t root_inode;
    init_extent_inode(&root_inode, 0x4000u | 0755u, EXT4_BLK_SIZE, 2u,
                      root_data_block, 1u, now);

    ext4_inode_raw_t jnl_inode;
    init_extent_inode(&jnl_inode, 0x8000u | 0600u,
                      (uint64_t)journal_blocks * EXT4_BLK_SIZE, 1u,
                      journal_start_block, (uint16_t)journal_blocks, now);

    ext4_sb_raw_t sb;
    memset(&sb, 0, sizeof(sb));
    sb.s_inodes_count = total_inodes;
    sb.s_blocks_count_lo = total_blocks;
    sb.s_r_blocks_count_lo = (total_blocks * 5u) / 100u;
    sb.s_free_blocks_count_lo = total_free_blocks;
    sb.s_free_inodes_count = total_free_inodes;
    sb.s_first_data_block = 0u;
    sb.s_log_block_size = 2u;
    sb.s_log_cluster_size = 2u;
    sb.s_blocks_per_group = EXT4_BLKS_PER_GRP;
    sb.s_frags_per_group = EXT4_BLKS_PER_GRP;
    sb.s_inodes_per_group = inodes_per_group;
    sb.s_wtime = now;
    sb.s_max_mnt_count = 0xFFFFu;
    sb.s_magic = EXT2_SUPER_MAGIC;
    sb.s_state = EXT2_STATE_CLEAN;
    sb.s_errors = EXT2_ERRORS_CONTINUE;
    sb.s_lastcheck = now;
    sb.s_creator_os = 0u;
    sb.s_rev_level = 1u;
    sb.s_first_ino = 11u;
    sb.s_inode_size = EXT4_INODE_SIZE_V1;
    sb.s_feature_compat = EXT4_FCOM_HAS_JOURNAL;
    sb.s_feature_incompat = EXT4_FINCOM_FILETYPE | EXT4_FINCOM_EXTENTS;
    sb.s_feature_ro_compat = EXT4_FRO_SPARSE_SUPER | EXT4_FRO_LARGE_FILE |
                             EXT4_FRO_HUGE_FILE | EXT4_FRO_DIR_NLINK |
                             EXT4_FRO_EXTRA_ISIZE;
    memcpy(sb.s_uuid, fs_uuid, 16);
    if (label && label[0]) {
        size_t llen = strlen(label);
        if (llen > sizeof(sb.s_volume_name))
            llen = sizeof(sb.s_volume_name);
        memcpy(sb.s_volume_name, label, llen);
    }
    sb.s_journal_inum = EXT4_JOURNAL_INO;
    memcpy(sb.s_hash_seed, seed_bytes, 16);
    sb.s_def_hash_version = 1u;
    sb.s_jnl_backup_type = 1u;
    sb.s_desc_size = 32u;
    sb.s_mkfs_time = now;
    memcpy(sb.s_jnl_blocks, jnl_inode.i_block, sizeof(jnl_inode.i_block));
    sb.s_jnl_blocks[15] = jnl_inode.i_size_hi;
    sb.s_jnl_blocks[16] = jnl_inode.i_size_lo;
    sb.s_min_extra_isize = 32u;
    sb.s_want_extra_isize = 32u;
    sb.s_flags = 0x0002u;

    uint8_t blk[EXT4_BLK_SIZE];
    for (uint32_t g = 0; g < num_groups; g++) {
        uint32_t grp_start = g * EXT4_BLKS_PER_GRP;
        if (!has_sparse_super(g))
            continue;

        memset(blk, 0, sizeof(blk));
        sb.s_block_group_nr = (uint16_t)g;
        if (g == 0u)
            memcpy(blk + 1024u, &sb, sizeof(sb));
        else
            memcpy(blk, &sb, sizeof(sb));

        if (write_blk(dev, grp_start, blk) != 0) {
            fprintf(stderr, "mkfs.ext4: failed to write superblock for group %u\n", g);
            free(gdt);
            return -1;
        }

        for (uint32_t b = 0; b < gdt_blocks; b++) {
            memset(blk, 0, sizeof(blk));
            size_t off = (size_t)b * EXT4_BLK_SIZE;
            size_t rem = gdt_bytes - off;
            size_t chunk = rem < EXT4_BLK_SIZE ? rem : EXT4_BLK_SIZE;
            memcpy(blk, (const uint8_t *)gdt + off, chunk);
            if (write_blk(dev, grp_start + 1u + b, blk) != 0) {
                fprintf(stderr, "mkfs.ext4: failed to write GDT for group %u\n", g);
                free(gdt);
                return -1;
            }
        }
    }

    for (uint32_t g = 0; g < num_groups; g++) {
        uint32_t grp_start = g * EXT4_BLKS_PER_GRP;
        uint32_t grp_blocks = EXT4_BLKS_PER_GRP;
        if (grp_start + grp_blocks > total_blocks)
            grp_blocks = total_blocks - grp_start;

        memset(blk, 0, sizeof(blk));
        uint32_t used_blks = grp_blocks - gdt[g].bg_free_blocks_count;
        for (uint32_t bit = 0; bit < used_blks; bit++)
            blk[bit / 8u] |= (uint8_t)(1u << (bit % 8u));
        for (uint32_t bit = grp_blocks; bit < EXT4_BLKS_PER_GRP; bit++)
            blk[bit / 8u] |= (uint8_t)(1u << (bit % 8u));

        if (write_blk(dev, gdt[g].bg_block_bitmap, blk) != 0) {
            fprintf(stderr, "mkfs.ext4: failed to write block bitmap for group %u\n", g);
            free(gdt);
            return -1;
        }

        memset(blk, 0, sizeof(blk));
        if (g == 0u) {
            for (uint32_t bit = 0; bit < 10u; bit++)
                blk[bit / 8u] |= (uint8_t)(1u << (bit % 8u));
        }
        for (uint32_t bit = inodes_per_group; bit < EXT4_BLK_SIZE * 8u; bit++)
            blk[bit / 8u] |= (uint8_t)(1u << (bit % 8u));

        if (write_blk(dev, gdt[g].bg_inode_bitmap, blk) != 0) {
            fprintf(stderr, "mkfs.ext4: failed to write inode bitmap for group %u\n", g);
            free(gdt);
            return -1;
        }

        if (write_zero_blks(dev, gdt[g].bg_inode_table, inode_table_blocks) != 0) {
            fprintf(stderr, "mkfs.ext4: failed to zero inode table for group %u\n", g);
            free(gdt);
            return -1;
        }
    }

    /* Write inode table block 0 for group 0 (inode 2 = root, inode 8 = journal). */
    memset(blk, 0, sizeof(blk));
    memcpy(blk + 1u * EXT4_INODE_SIZE_V1, &root_inode, sizeof(root_inode));
    memcpy(blk + (EXT4_JOURNAL_INO - 1u) * EXT4_INODE_SIZE_V1, &jnl_inode, sizeof(jnl_inode));
    if (write_blk(dev, gdt[0].bg_inode_table, blk) != 0) {
        fprintf(stderr, "mkfs.ext4: failed to write root/journal inodes\n");
        free(gdt);
        return -1;
    }

    /* Write root directory block ('.' and '..'). */
    memset(blk, 0, sizeof(blk));
    ext2_dir_entry_t *dot = (ext2_dir_entry_t *)blk;
    dot->inode = EXT2_ROOT_INO;
    dot->rec_len = 12u;
    dot->name_len = 1u;
    dot->file_type = EXT2_FT_DIR;
    dot->name[0] = '.';

    ext2_dir_entry_t *dotdot = (ext2_dir_entry_t *)(blk + 12u);
    dotdot->inode = EXT2_ROOT_INO;
    dotdot->rec_len = (uint16_t)(EXT4_BLK_SIZE - 12u);
    dotdot->name_len = 2u;
    dotdot->file_type = EXT2_FT_DIR;
    dotdot->name[0] = '.';
    dotdot->name[1] = '.';

    if (write_blk(dev, root_data_block, blk) != 0) {
        fprintf(stderr, "mkfs.ext4: failed to write root directory\n");
        free(gdt);
        return -1;
    }

    /* Write JBD2 journal superblock + zero remaining journal blocks. */
    memset(blk, 0, sizeof(blk));
    jbd2_sb_raw_t *jsb = (jbd2_sb_raw_t *)blk;
    jsb->h_magic = be32(JBD2_MAGIC);
    jsb->h_blocktype = be32(JBD2_SUPERBLOCK_V2);
    jsb->h_sequence = be32(0u);
    jsb->s_blocksize = be32(EXT4_BLK_SIZE);
    jsb->s_maxlen = be32(journal_blocks);
    jsb->s_first = be32(1u);
    jsb->s_sequence = be32(1u);
    jsb->s_start = be32(0u);
    jsb->s_nr_users = be32(1u);
    memcpy(jsb->s_uuid, jnl_uuid, 16);
    memcpy(jsb->s_users, fs_uuid, 16);

    if (write_blk(dev, journal_start_block, blk) != 0) {
        fprintf(stderr, "mkfs.ext4: failed to write JBD2 superblock\n");
        free(gdt);
        return -1;
    }
    if (journal_blocks > 1u &&
        write_zero_blks(dev, journal_start_block + 1u, journal_blocks - 1u) != 0) {
        fprintf(stderr, "mkfs.ext4: failed to clear journal blocks\n");
        free(gdt);
        return -1;
    }

    free(gdt);
    if (device_sync(dev) != 0) {
        fprintf(stderr, "mkfs.ext4: failed to sync device\n");
        return -1;
    }

    printf("mkfs.ext4: formatted %s (%u 4KiB blocks, %u inodes, %u journal blocks)\n",
           dev->path, total_blocks, total_inodes, journal_blocks);
    return 0;
}
