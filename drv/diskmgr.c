/*
 * Project Tsukasa — MBR/GPT partition-table discovery
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

/* Byte offsets per the FAT32 BPB layout. */
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
    uint32_t ss;
    uint8_t *buf;
    int fat32 = 0;

    if (!bd || !bd->read)
        return 0;
    ss = bd->sector_size ? bd->sector_size : 512u;
    if (ss < 512u)
        return 0;
    buf = (uint8_t *)kmalloc(ss);
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
    uint32_t ss = bd->sector_size ? bd->sector_size : 512u;
    uint8_t *buf = (uint8_t *)kmalloc(ss);
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

static int mbr_has_protective_gpt(const uint8_t *mbr)
{
    mbr_part_entry_t entries[4];
    if (mbr[510] != 0x55u || mbr[511] != 0xAAu)
        return 0;
    k_memcpy(entries, mbr + 446, sizeof(entries));
    for (int i = 0; i < 4; i++) {
        if (entries[i].type == MBR_PART_TYPE_GPT_PROT && entries[i].sector_count > 0)
            return 1;
    }
    return 0;
}

static int parse_mbr_partitions(block_dev_t *bd, const uint8_t *mbr)
{
    mbr_part_entry_t entries[4];
    int part_num = 1, found = 0, i;

    if (mbr[510] != 0x55u || mbr[511] != 0xAAu) {
        kprintf("[disk] %s: no boot signature (blank disk)\n", bd->name);
        return 0;
    }

    k_memcpy(entries, mbr + 446, sizeof(entries));

    for (i = 0; i < 4; i++) {
        int fat32;
        uint64_t lba_start = (uint64_t)entries[i].lba_start;
        uint64_t sec_cnt = (uint64_t)entries[i].sector_count;

        if (entries[i].type == 0x00 || sec_cnt == 0)
            continue;
        if (entries[i].type == MBR_PART_TYPE_GPT_PROT)
            continue;
        if (lba_start == 0 || lba_start >= bd->sector_count ||
            lba_start + sec_cnt < lba_start ||
            lba_start + sec_cnt > bd->sector_count)
            continue;

        fat32 = 0;
        if (entries[i].type == MBR_PART_TYPE_FAT32 ||
            entries[i].type == MBR_PART_TYPE_FAT32_LBA)
            fat32 = diskmgr_probe_fat32(bd, lba_start);

        if (blockdev_register_partition(bd, part_num, lba_start, sec_cnt,
                                        fat32, 0)) {
            found++;
            part_num++;
        }
    }

    if (found == 0 && is_fat32_bpb(mbr)) {
        kprintf("[disk] %s: raw FAT32 volume (no partition table)\n", bd->name);
        bd->is_fat32 = 1;
        diskmgr_load_fat32_label(bd);
    } else if (found == 0) {
        kprintf("[disk] %s: no MBR partitions\n", bd->name);
    }

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

/* EFI System Partition: C12A7328-F81F-11D2-BA4B-00A0C93EC93B */
static const uint8_t GPT_GUID_ESP[16] = {
    0x28, 0x73, 0x2A, 0xC1, 0x1F, 0xF8, 0xD2, 0x11,
    0xBA, 0x4B, 0x00, 0xA0, 0xC9, 0x3E, 0xC9, 0x3B
};

/* Linux Filesystem Data: 0FC63DAF-8483-4772-8E79-3D69D8477DE4 */
static const uint8_t GPT_GUID_LINUX_FS[16] = {
    0xAF, 0x3D, 0xC6, 0x0F, 0x83, 0x84, 0x72, 0x47,
    0x8E, 0x79, 0x3D, 0x69, 0xD8, 0x47, 0x7D, 0xE4
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

static int guid_eq(const uint8_t a[16], const uint8_t b[16])
{
    for (int i = 0; i < 16; i++) {
        if (a[i] != b[i])
            return 0;
    }
    return 1;
}

static int guid_is_zero(const uint8_t g[16])
{
    for (int i = 0; i < 16; i++) {
        if (g[i] != 0)
            return 0;
    }
    return 1;
}

/*
 * Validate GPT header at `lba` AND its partition entry array CRC32.
 * Returns 1 on full validation success (caller takes ownership of *entries_out),
 * 0 if absent or invalid.
 */
static int gpt_validate_at(block_dev_t *bd, uint64_t lba, const char *role,
                           gpt_header_t *hdr_out, uint8_t **entries_out,
                           int *had_sig_out)
{
    uint32_t ss = bd->sector_size ? bd->sector_size : 512u;
    uint8_t *buf;
    uint8_t *entries_buf;
    gpt_header_t *hdr;
    uint32_t stored_crc, computed;
    uint64_t array_bytes, entry_sectors;

    if (had_sig_out)
        *had_sig_out = 0;

    if (lba >= bd->sector_count || ss < 512u)
        return 0;

    buf = (uint8_t *)kmalloc(ss);
    if (!buf)
        return 0;

    if (bd->read(bd, lba, 1, buf) != 0) {
        kfree(buf);
        return 0;
    }

    hdr = (gpt_header_t *)buf;
    if (hdr->signature != GPT_SIGNATURE) {
        kfree(buf);
        return 0;
    }
    if (had_sig_out)
        *had_sig_out = 1;

    if (hdr->header_size < 92u || hdr->header_size > ss) {
        kprintf("[disk] %s: %s GPT header size invalid (%u)\n",
                bd->name, role, hdr->header_size);
        kfree(buf);
        return 0;
    }

    stored_crc = hdr->crc32;
    hdr->crc32 = 0;
    computed = crc32_compute(buf, hdr->header_size);
    hdr->crc32 = stored_crc;
    if (computed != stored_crc) {
        kprintf("[disk] %s: %s GPT header CRC mismatch (0x%08x != 0x%08x)\n",
                bd->name, role, computed, stored_crc);
        kfree(buf);
        return 0;
    }

    if (hdr->my_lba != lba) {
        kprintf("[disk] %s: %s GPT my_lba mismatch\n", bd->name, role);
        kfree(buf);
        return 0;
    }

    if (hdr->size_of_partition_entry < 128u ||
        hdr->size_of_partition_entry > 512u ||
        (hdr->size_of_partition_entry & 7u) != 0 ||
        hdr->num_partition_entries == 0 ||
        hdr->num_partition_entries > 128u) {
        kprintf("[disk] %s: %s GPT geometry unsupported (entry=%u count=%u)\n",
                bd->name, role, hdr->size_of_partition_entry,
                hdr->num_partition_entries);
        kfree(buf);
        return 0;
    }

    array_bytes = (uint64_t)hdr->num_partition_entries *
                  (uint64_t)hdr->size_of_partition_entry;
    entry_sectors = (array_bytes + ss - 1u) / ss;
    if (hdr->partition_entry_lba + entry_sectors < hdr->partition_entry_lba ||
        hdr->partition_entry_lba + entry_sectors > bd->sector_count) {
        kprintf("[disk] %s: %s GPT partition array out of bounds\n",
                bd->name, role);
        kfree(buf);
        return 0;
    }

    entries_buf = (uint8_t *)kmalloc((size_t)(entry_sectors * ss));
    if (!entries_buf) {
        kfree(buf);
        return 0;
    }

    if (bd->read(bd, hdr->partition_entry_lba, (uint32_t)entry_sectors,
                 entries_buf) != 0) {
        kprintf("[disk] %s: %s GPT partition array read failed\n",
                bd->name, role);
        kfree(entries_buf);
        kfree(buf);
        return 0;
    }

    computed = crc32_compute(entries_buf, (uint32_t)array_bytes);
    if (computed != hdr->partition_entry_array_crc32) {
        kprintf("[disk] %s: %s GPT entry array CRC mismatch (0x%08x != 0x%08x)\n",
                bd->name, role, computed, hdr->partition_entry_array_crc32);
        kfree(entries_buf);
        kfree(buf);
        return 0;
    }

    *hdr_out = *hdr;
    *entries_out = entries_buf;
    kfree(buf);
    return 1;
}

static int parse_gpt_partitions(block_dev_t *bd, const gpt_header_t *hdr,
                                const uint8_t *entries_buf)
{
    int part_num = 1, found = 0;

    for (uint32_t idx = 0; idx < hdr->num_partition_entries; idx++) {
        const gpt_entry_t *entry = (const gpt_entry_t *)(
            entries_buf + (size_t)idx * hdr->size_of_partition_entry);
        uint64_t start, end, size;
        int is_esp, is_linux, fat32;
        block_dev_t *pbd;

        if (guid_is_zero(entry->type_guid))
            continue;

        start = entry->start_lba;
        end = entry->end_lba;
        if (start == 0 || end < start || end >= bd->sector_count)
            continue;
        size = end - start + 1;

        is_esp = guid_eq(entry->type_guid, GPT_GUID_ESP);
        is_linux = guid_eq(entry->type_guid, GPT_GUID_LINUX_FS);
        fat32 = diskmgr_probe_fat32(bd, start);

        pbd = blockdev_register_partition(bd, part_num, start, size,
                                          fat32, is_esp);
        if (pbd) {
            k_memcpy(pbd->part_guid, entry->partition_guid, 16);
            pbd->has_part_guid = 1;
            if (!is_esp && !fat32 && is_linux) {
                const char *lbl = "Linux Filesystem";
                int k;
                for (k = 0; lbl[k] && k < BLOCKDEV_LABEL_MAX - 1; k++)
                    pbd->label[k] = lbl[k];
                pbd->label[k] = '\0';
            }
            found++;
            part_num++;
        }
    }

    if (found == 0)
        kprintf("[disk] %s: GPT present but no partitions registered\n",
                bd->name);
    return found;
}

int diskmgr_scan_device(block_dev_t *bd)
{
    uint32_t ss;
    uint8_t *mbr;
    gpt_header_t hdr;
    uint8_t *entries_buf = NULL;
    int prot_mbr = 0;
    int primary_sig = 0;
    int backup_sig = 0;
    int found;

    if (!bd || bd->is_partition || !bd->read || bd->sector_count == 0)
        return -1;

    ss = bd->sector_size ? bd->sector_size : 512u;
    if (ss < 512u)
        return -1;

    mbr = (uint8_t *)kmalloc(ss);
    if (!mbr)
        return -1;
    if (bd->read(bd, 0, 1, mbr) != 0) {
        kprintf("[disk] %s: LBA 0 read failed\n", bd->name);
        kfree(mbr);
        return -1;
    }

    prot_mbr = mbr_has_protective_gpt(mbr);

    /* Try primary GPT at LBA 1. */
    if (gpt_validate_at(bd, 1, "primary", &hdr, &entries_buf, &primary_sig)) {
        kprintf("[disk] %s: primary GPT valid (%u entries at LBA %u)\n",
                bd->name, hdr.num_partition_entries,
                (uint32_t)hdr.partition_entry_lba);
        found = parse_gpt_partitions(bd, &hdr, entries_buf);
        kfree(entries_buf);
        kfree(mbr);
        return found;
    }

    /* Fall back to backup GPT if primary failed or protective MBR was present. */
    if ((prot_mbr || primary_sig) && bd->sector_count > 2) {
        uint64_t backup_lba = bd->sector_count - 1;
        if (gpt_validate_at(bd, backup_lba, "backup", &hdr, &entries_buf,
                            &backup_sig)) {
            kprintf("[disk] %s: recovered using backup GPT at LBA %u\n",
                    bd->name, (uint32_t)backup_lba);
            found = parse_gpt_partitions(bd, &hdr, entries_buf);
            kfree(entries_buf);
            kfree(mbr);
            return found;
        }
        kprintf("[disk] %s: GPT rejected (primary and backup invalid) - 0 partitions\n",
                bd->name);
        kfree(mbr);
        return 0;
    }

    found = parse_mbr_partitions(bd, mbr);
    kfree(mbr);
    return found;
}

int diskmgr_rescan(block_dev_t *bd)
{
    return diskmgr_scan_device(bd);
}

int diskmgr_write_mbr(block_dev_t *bd, uint32_t part_start,
                      uint32_t part_count)
{
    uint32_t ss;
    uint8_t *buf;
    uint8_t *e;
    int rc;

    if (!bd || bd->is_partition || !bd->write || part_count == 0)
        return -1;
    if ((uint64_t)part_start + part_count > bd->sector_count)
        return -1;

    ss = bd->sector_size ? bd->sector_size : 512u;
    buf = (uint8_t *)kmalloc(ss);
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
        int n = diskmgr_scan_device(bd);
        if (n > 0)
            total += n;
    }
    kprintf("[disk] scan complete: %d partition(s) across %d disk(s)\n",
            total, whole);
    return total;
}
