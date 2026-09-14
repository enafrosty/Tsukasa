/*
 * Project Tsukasa — Ext2 Filesystem On-Disk Definitions
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

#ifndef _TSUKASA_EXT2_H
#define _TSUKASA_EXT2_H

#include <stdint.h>

#define EXT2_SUPER_MAGIC         0xEF53u
#define EXT2_STATE_CLEAN         1u
#define EXT2_ERRORS_CONTINUE     1u

#define EXT2_ROOT_INO            2u
#define EXT2_FIRST_USER_INO      11u
#define EXT2_INODE_SIZE          128u

#define EXT2_S_IFDIR             0x4000u
#define EXT2_S_IFREG             0x8000u

#define EXT2_FT_UNKNOWN          0u
#define EXT2_FT_REG_FILE         1u
#define EXT2_FT_DIR              2u
#define EXT2_FT_CHRDEV           3u
#define EXT2_FT_BLKDEV           4u
#define EXT2_FT_FIFO             5u
#define EXT2_FT_SOCK             6u
#define EXT2_FT_SYMLINK          7u

#pragma pack(push, 1)
typedef struct {
    uint32_t s_inodes_count;       /* Total inodes */
    uint32_t s_blocks_count;       /* Total blocks */
    uint32_t s_r_blocks_count;     /* Reserved blocks (5%) */
    uint32_t s_free_blocks_count;  /* Free blocks */
    uint32_t s_free_inodes_count;  /* Free inodes */
    uint32_t s_first_data_block;   /* 1 for 1024-byte blocks, 0 for >1024 */
    uint32_t s_log_block_size;     /* 0 = 1024, 1 = 2048, 2 = 4096 */
    uint32_t s_log_frag_size;      /* 0 */
    uint32_t s_blocks_per_group;   /* Typically 8192 */
    uint32_t s_frags_per_group;    /* 8192 */
    uint32_t s_inodes_per_group;   /* Typically 2048 */
    uint32_t s_mtime;              /* Mount time */
    uint32_t s_wtime;              /* Write time */
    uint16_t s_mnt_count;          /* Mount count */
    uint16_t s_max_mnt_count;      /* 20 */
    uint16_t s_magic;              /* 0xEF53 */
    uint16_t s_state;              /* 1 = Clean */
    uint16_t s_errors;             /* 1 = Continue */
    uint16_t s_minor_rev_level;
    uint32_t s_lastcheck;
    uint32_t s_checkinterval;
    uint32_t s_creator_os;         /* 0 = Linux */
    uint32_t s_rev_level;          /* 0 = Good old rev */
    uint16_t s_def_resuid;         /* 0 */
    uint16_t s_def_resgid;         /* 0 */
    uint8_t  s_reserved[940];
} ext2_super_block_t;

typedef struct {
    uint32_t bg_block_bitmap;      /* Block ID of block bitmap */
    uint32_t bg_inode_bitmap;      /* Block ID of inode bitmap */
    uint32_t bg_inode_table;       /* Starting block ID of inode table */
    uint16_t bg_free_blocks_count;
    uint16_t bg_free_inodes_count;
    uint16_t bg_used_dirs_count;
    uint16_t bg_pad;
    uint8_t  bg_reserved[12];
} ext2_group_desc_t;

typedef struct {
    uint16_t i_mode;               /* 0040755 for root dir */
    uint16_t i_uid;                /* 0 */
    uint32_t i_size;               /* Size in bytes */
    uint32_t i_atime;
    uint32_t i_ctime;
    uint32_t i_mtime;
    uint32_t i_dtime;              /* 0 */
    uint16_t i_gid;                /* 0 */
    uint16_t i_links_count;        /* 2 for directory */
    uint32_t i_blocks;             /* Sectors (512-byte blocks) */
    uint32_t i_flags;              /* 0 */
    uint32_t i_osd1;
    uint32_t i_block[15];          /* i_block[0] points to root data block */
    uint32_t i_generation;
    uint32_t i_file_acl;
    uint32_t i_dir_acl;
    uint32_t i_faddr;
    uint8_t  i_osd2[12];
} ext2_inode_t;

typedef struct {
    uint32_t inode;
    uint16_t rec_len;
    uint8_t  name_len;
    uint8_t  file_type;            /* 2 = Directory */
    char     name[];
} ext2_dir_entry_t;
#pragma pack(pop)

_Static_assert(sizeof(ext2_super_block_t) == 1024, "Ext2 Superblock must be 1024 bytes");
_Static_assert(sizeof(ext2_group_desc_t) == 32, "Ext2 Group Descriptor must be 32 bytes");
_Static_assert(sizeof(ext2_inode_t) == 128, "Ext2 Inode must be 128 bytes");

#endif /* _TSUKASA_EXT2_H */
