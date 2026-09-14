/*
 * Project Tsukasa — MBR/GPT partition-table discovery (guide 12)
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

#include "diskmgr.h"

#include "../include/kprintf.h"
#include "../include/kutils.h"
#include "../mm/heap.h"

#include <stddef.h>
#include <stdint.h>

/* Byte offsets per the FAT32 BPB layout (fs/fat32.c bpb_t / OSDev FAT): [11] bytes_per_sector(u16) [22]... */
static int is_fat32_bpb(const uint8_t *s)
{
    uint16_t bps, spf16;
    uint32_t spf32;

    if (s[510] != 0x55u || s[511] != 0xAAu)
        return 0;

    if (s[82] == 'F' && s[83] == 'A' && s[84] == 'T' &&
        s[85] == '3' && s[86] == '2')
        return 1;

    bps = (uint16_t)(s[11] | (s[12] << 8));
    spf16 = (uint16_t)(s[22] | (s[23] << 8));
    spf32 = (uint32_t)s[36] | ((uint32_t)s[37] << 8) |
            ((uint32_t)s[38] << 16) | ((uint32_t)s[39] << 24);
    return (bps == 512 && spf16 == 0 && spf32 > 0) ? 1 : 0;
}

int diskmgr_probe_fat32(block_dev_t *bd, uint64_t lba)
{
    uint8_t *buf;
    int fat32 = 0;

    if (!bd || !bd->read)
        return 0;
    buf = (uint8_t *)kmalloc(512);
    if (!buf)
        return 0;
    if (bd->read(bd, lba, 1, buf) == 0)
        fat32 = is_fat32_bpb(buf);
    kfree(buf);
    return fat32;
}

/* Copy the space-padded FAT volume label (BPB bytes 71..81). */
static void diskmgr_load_fat32_label(block_dev_t *bd)
{
    uint8_t *buf = (uint8_t *)kmalloc(512);
    int end, i;

    if (!buf)
        return;
    if (bd->read(bd, 0, 1, buf) == 0 && buf[510] == 0x55u && buf[511] == 0xAAu) {
        end = 11;
        while (end > 0 && buf[71 + end - 1] == ' ')
            end--;
        if (end > 0) {
            for (i = 0; i < end && i < BLOCKDEV_LABEL_MAX - 1; i++)
                bd->label[i] = (char)buf[71 + i];
            bd->label[i] = '\0';
        }
    }
    kfree(buf);
}

typedef struct {
    uint8_t  status;
    uint8_t  chs_first[3];
    uint8_t  type;
    uint8_t  chs_last[3];
    uint32_t lba_start;
    uint32_t sector_count;
} __attribute__((packed)) mbr_part_entry_t;

_Static_assert(sizeof(mbr_part_entry_t) == 16, "MBR entry is 16 bytes");

#define MBR_PART_TYPE_FAT32      0x0Bu
#define MBR_PART_TYPE_FAT32_LBA  0x0Cu
#define MBR_PART_TYPE_GPT_PROT   0xEEu

/* Entries at bytes 446..509, 0x55AA at 510..511 (OSDev Partition Table). */
static int parse_mbr_partitions(block_dev_t *bd)
{
    uint8_t *buf = (uint8_t *)kmalloc(512);
    mbr_part_entry_t entries[4];
    int part_num = 1, found = 0, i;

    if (!buf)
        return -1;
    if (bd->read(bd, 0, 1, buf) != 0) {
        kprintf("[disk] %s: MBR read failed\n", bd->name);
        kfree(buf);
        return -1;
    }
    if (buf[510] != 0x55u || buf[511] != 0xAAu) {
        kprintf("[disk] %s: no boot signature (blank disk)\n", bd->name);
        kfree(buf);
        return 0;
    }

    k_memcpy(entries, buf + 446, sizeof(entries));

    for (i = 0; i < 4; i++) {
        int fat32;
        if (entries[i].type == 0x00 || entries[i].sector_count == 0)
            continue;
        if (entries[i].type == MBR_PART_TYPE_GPT_PROT)
            continue;
        if ((uint64_t)entries[i].lba_start >= bd->sector_count)
            continue;

        fat32 = 0;
        if (entries[i].type == MBR_PART_TYPE_FAT32 ||
            entries[i].type == MBR_PART_TYPE_FAT32_LBA)
            fat32 = diskmgr_probe_fat32(bd, entries[i].lba_start);

        if (blockdev_register_partition(bd, part_num,
                                        entries[i].lba_start,
                                        entries[i].sector_count,
                                        fat32, 0)) {
            found++;
            part_num++;
        }
    }

    if (found == 0 && is_fat32_bpb(buf)) {
        kprintf("[disk] %s: raw FAT32 volume (no partition table)\n",
                bd->name);
        bd->is_fat32 = 1;
        diskmgr_load_fat32_label(bd);
    } else if (found == 0) {
        kprintf("[disk] %s: no MBR partitions\n", bd->name);
    }

    kfree(buf);
    return found;
}

typedef struct {
    uint64_t signature;
    uint32_t revision;
    uint32_t header_size;
    uint32_t crc32;
    uint32_t reserved;
    uint64_t my_lba;
    uint64_t alternate_lba;
    uint64_t first_usable_lba;
    uint64_t last_usable_lba;
    uint8_t  disk_guid[16];
    uint64_t partition_entry_lba;
    uint32_t num_partition_entries;
    uint32_t size_of_partition_entry;
    uint32_t partition_entry_array_crc32;
} __attribute__((packed)) gpt_header_t;

typedef struct {
    uint8_t  type_guid[16];
    uint8_t  partition_guid[16];
    uint64_t start_lba;
    uint64_t end_lba;
    uint64_t attributes;
    uint16_t name[36];
} __attribute__((packed)) gpt_entry_t;

_Static_assert(sizeof(gpt_entry_t) == 128, "GPT entry is 128 bytes");

#define GPT_SIGNATURE 0x5452415020494645ULL

static const uint8_t GPT_GUID_ESP[16] = {
    0x28, 0x73, 0x2A, 0xC1, 0x1F, 0xF8, 0xD2, 0x11,
    0xBA, 0x4B, 0x00, 0xA0, 0xC9, 0x3E, 0xC9, 0x3B
};

static uint32_t crc32_compute(const uint8_t *data, uint32_t len)
{
    uint32_t crc = 0xFFFFFFFFu;
    for (uint32_t i = 0; i < len; i++) {
        crc ^= data[i];
        for (int j = 0; j < 8; j++)
            crc = (crc >> 1) ^ (0xEDB88320u & (0u - (crc & 1u)));
    }
    return ~crc;
}

/* Validate the GPT header at LBA 1. */
static int gpt_header_valid(block_dev_t *bd, gpt_header_t *hdr_out)
{
    uint8_t *buf = (uint8_t *)kmalloc(512);
    gpt_header_t *hdr;
    uint32_t stored_crc, computed;

    if (!buf)
        return 0;
    if (bd->read(bd, 1, 1, buf) != 0) {
        kfree(buf);
        return 0;
    }
    hdr = (gpt_header_t *)buf;
    if (hdr->signature != GPT_SIGNATURE) {
        kfree(buf);
        return 0;
    }
    if (hdr->header_size < 92 || hdr->header_size > 512 ||
        hdr->size_of_partition_entry != 128 ||
        hdr->num_partition_entries == 0 ||
        hdr->num_partition_entries > 128) {
        kprintf("[disk] %s: GPT header geometry unsupported "
                "(hdr=%u entry=%u count=%u)\n",
                bd->name, hdr->header_size, hdr->size_of_partition_entry,
                hdr->num_partition_entries);
        kfree(buf);
        return 0;
    }

    stored_crc = hdr->crc32;
    hdr->crc32 = 0;
    computed = crc32_compute(buf, hdr->header_size);
    hdr->crc32 = stored_crc;
    if (computed != stored_crc) {
        kprintf("[disk] %s: GPT header CRC mismatch (0x%x != 0x%x) — "
                "table rejected\n", bd->name, computed, stored_crc);
        kfree(buf);
        return 0;
    }

    *hdr_out = *hdr;
    kfree(buf);
    return 1;
}

static int parse_gpt_partitions(block_dev_t *bd, const gpt_header_t *hdr)
{
    uint8_t *buf = (uint8_t *)kmalloc(512);
    uint32_t per_sector = 512u / 128u;
    uint32_t sectors = (hdr->num_partition_entries + per_sector - 1) / per_sector;
    int part_num = 1, found = 0;

    if (!buf)
        return -1;

    for (uint32_t s = 0; s < sectors; s++) {
        if (bd->read(bd, hdr->partition_entry_lba + s, 1, buf) != 0)
            break;
        for (uint32_t e = 0; e < per_sector; e++) {
            gpt_entry_t *entry = (gpt_entry_t *)(buf + e * 128u);
            uint32_t idx = s * per_sector + e;
            uint64_t start, end, size;
            int zero = 1, is_esp = 1, fat32, j;

            if (idx >= hdr->num_partition_entries)
                break;
            for (j = 0; j < 16; j++)
                if (entry->type_guid[j] != 0) { zero = 0; break; }
            if (zero)
                continue;

            start = entry->start_lba;
            end = entry->end_lba;
            if (end < start || end >= bd->sector_count)
                continue;
            size = end - start + 1;

            for (j = 0; j < 16; j++)
                if (entry->type_guid[j] != GPT_GUID_ESP[j]) { is_esp = 0; break; }

            fat32 = diskmgr_probe_fat32(bd, start);

            if (blockdev_register_partition(bd, part_num, start, size,
                                            fat32, is_esp)) {
                found++;
                part_num++;
            }
        }
    }

    if (found == 0)
        kprintf("[disk] %s: GPT present but no partitions registered\n",
                bd->name);
    kfree(buf);
    return found;
}

int diskmgr_rescan(block_dev_t *bd)
{
    gpt_header_t hdr;

    if (!bd || bd->is_partition || !bd->read)
        return -1;

    if (gpt_header_valid(bd, &hdr)) {
        kprintf("[disk] %s: GPT detected (%u-entry table at LBA %u)\n",
                bd->name, hdr.num_partition_entries,
                (uint32_t)hdr.partition_entry_lba);
        return parse_gpt_partitions(bd, &hdr);
    }
    return parse_mbr_partitions(bd);
}

int diskmgr_write_mbr(block_dev_t *bd, uint32_t part_start,
                      uint32_t part_count)
{
    uint8_t *buf;
    uint8_t *e;
    int rc;

    if (!bd || bd->is_partition || !bd->write || part_count == 0)
        return -1;
    if ((uint64_t)part_start + part_count > bd->sector_count)
        return -1;

    buf = (uint8_t *)kmalloc(512);
    if (!buf)
        return -1;
    if (bd->read(bd, 0, 1, buf) != 0) {
        kfree(buf);
        return -1;
    }

    for (int i = 446; i < 510; i++)
        buf[i] = 0;
    e = buf + 446;
    e[0] = 0x80;
    e[1] = 0x00; e[2] = 0x02; e[3] = 0x00;
    e[4] = MBR_PART_TYPE_FAT32_LBA;
    e[5] = 0xFF; e[6] = 0xFF; e[7] = 0xFF;
    e[8]  = (uint8_t)part_start;
    e[9]  = (uint8_t)(part_start >> 8);
    e[10] = (uint8_t)(part_start >> 16);
    e[11] = (uint8_t)(part_start >> 24);
    e[12] = (uint8_t)part_count;
    e[13] = (uint8_t)(part_count >> 8);
    e[14] = (uint8_t)(part_count >> 16);
    e[15] = (uint8_t)(part_count >> 24);
    buf[510] = 0x55;
    buf[511] = 0xAA;

    rc = bd->write(bd, 0, 1, buf);
    kfree(buf);
    if (rc == 0)
        kprintf("[disk] %s: MBR written (part @%u x%u)\n",
                bd->name, part_start, part_count);
    return rc;
}

int diskmgr_scan_all(void)
{
    int total = 0;

    int whole = blockdev_count();
    for (int i = 0; i < whole; i++) {
        block_dev_t *bd = blockdev_at(i);
        if (!bd || bd->is_partition)
            continue;
        int n = diskmgr_rescan(bd);
        if (n > 0)
            total += n;
    }
    kprintf("[disk] scan complete: %d partition(s) across %d disk(s)\n",
            total, whole);
    return total;
}
