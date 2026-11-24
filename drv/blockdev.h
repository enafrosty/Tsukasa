/*
 * Project Tsukasa — minimal block-device seam: uniform sector I/O over
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

#ifndef TSUKASA_BLOCKDEV_H
#define TSUKASA_BLOCKDEV_H

#include <stdint.h>

#define BLOCKDEV_MAX_DEVICES 64
#define BLOCKDEV_NAME_MAX    16
#define BLOCKDEV_LABEL_MAX   32
#define BLOCKDEV_MAX_PARTS   48

typedef enum block_dev_type {
    BLOCKDEV_TYPE_IDE = 0,
    BLOCKDEV_TYPE_SATA = 1,
    BLOCKDEV_TYPE_RAM = 2,
    BLOCKDEV_TYPE_VIRTIO = 3,
    BLOCKDEV_TYPE_NVME = 4,
} block_dev_type_t;

/* One registered disk or partition. */
typedef struct block_dev {
    char             name[BLOCKDEV_NAME_MAX];
    block_dev_type_t type;
    char             label[BLOCKDEV_LABEL_MAX];
    uint32_t         sector_size;
    uint64_t         sector_count;

    int               is_partition;
    int               is_fat32;
    int               is_esp;
    uint64_t          lba_offset;
    uint8_t           part_guid[16];
    int               has_part_guid;
    struct block_dev *parent;

    int  (*read)(struct block_dev *bd, uint64_t lba, uint32_t count,
                 void *buf);
    int  (*write)(struct block_dev *bd, uint64_t lba, uint32_t count,
                  const void *buf);
    int  (*sync)(struct block_dev *bd);
    void *drv_data;
} block_dev_t;

/* Register a whole-disk device. */
int blockdev_register(block_dev_t *bd);

/* Register partition N of `parent` (from this layer's static pool). */
block_dev_t *blockdev_register_partition(block_dev_t *parent, int part_num,
                                         uint64_t lba_offset,
                                         uint64_t sector_count,
                                         int is_fat32, int is_esp);

/* Unregister all partitions belonging to `parent` (for BLKRRPART). */
void blockdev_unregister_partitions(block_dev_t *parent);

/* Look up a registered partition by GPT Partition GUID string. */
block_dev_t *blockdev_find_by_partuuid(const char *uuid_str);

int          blockdev_count(void);
block_dev_t *blockdev_at(int index);
block_dev_t *blockdev_find(const char *name);
block_dev_t *blockdev_primary(void);

block_dev_t *blockdev_find_type(block_dev_type_t type);

/* First whole disk carrying a raw (unpartitioned) FAT32 volume — the classic Tsukasa dev image; mounted at... */
block_dev_t *blockdev_first_raw_fat32(void);

void blockdev_run_selftests(void);
void blockdev_run_irq_selftests(void);

#endif /* TSUKASA_BLOCKDEV_H */
