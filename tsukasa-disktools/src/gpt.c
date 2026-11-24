/*
 * Project Tsukasa - GUID Partition Table (GPT) Implementation
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

#include "../include/gpt.h"
#include "../include/mbr.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

/* BIOS Boot Partition: 21686148-6449-6E6F-744E-656564454649 */
const uint8_t GPT_GUID_BIOS_BOOT[16] = {
    0x48, 0x61, 0x68, 0x21, 0x49, 0x64, 0x6F, 0x6E,
    0x74, 0x4E, 0x65, 0x65, 0x64, 0x45, 0x46, 0x49
};

/* EFI System Partition: C12A7328-F81F-11D2-BA4B-00A0C93EC93B */
const uint8_t GPT_GUID_ESP[16] = {
    0x28, 0x73, 0x2A, 0xC1, 0x1F, 0xF8, 0xD2, 0x11,
    0xBA, 0x4B, 0x00, 0xA0, 0xC9, 0x3E, 0xC9, 0x3B
};

/* Linux Filesystem Data: 0FC63DAF-8483-4772-8E79-3D69D8477DE4 */
const uint8_t GPT_GUID_LINUX_FS[16] = {
    0xAF, 0x3D, 0xC6, 0x0F, 0x83, 0x84, 0x72, 0x47,
    0x8E, 0x79, 0x3D, 0x69, 0xD8, 0x47, 0x7D, 0xE4
};

uint32_t gpt_crc32(const void *data, size_t len)
{
    const uint8_t *p = (const uint8_t *)data;
    uint32_t crc = 0xFFFFFFFFU;
    for (size_t i = 0; i < len; i++) {
        crc ^= p[i];
        for (int j = 0; j < 8; j++)
            crc = (crc >> 1) ^ (0xEDB88320U & (0U - (crc & 1U)));
    }
    return ~crc;
}

void gpt_gen_uuid(uint8_t out[16])
{
    static uint64_t seq = 0x9E3779B97F4A7C15ULL;
    int got_urandom = 0;

    int fd = open("/dev/urandom", O_RDONLY);
    if (fd >= 0) {
        ssize_t n = read(fd, out, 16);
        close(fd);
        if (n == 16)
            got_urandom = 1;
    }

    if (!got_urandom) {
        uint64_t seed = (uint64_t)time(NULL) ^ ((uint64_t)getpid() << 16);
        seq ^= seed + 0x9E3779B97F4A7C15ULL;
        for (int i = 0; i < 16; i += 8) {
            seq ^= seq >> 12;
            seq ^= seq << 25;
            seq ^= seq >> 27;
            uint64_t v = seq * 0x2545F4914F6CDD1DULL;
            memcpy(out + i, &v, 8);
        }
    }

    /* Mixed-endian UUID v4 version (byte 7 high nibble = 4) and variant (byte 8 = 10xx). */
    out[7] = (uint8_t)((out[7] & 0x0FU) | 0x40U);
    out[8] = (uint8_t)((out[8] & 0x3FU) | 0x80U);
}

void gpt_format_uuid(const uint8_t guid[16], char out[37])
{
    snprintf(out, 37,
             "%02x%02x%02x%02x-%02x%02x-%02x%02x-%02x%02x-%02x%02x%02x%02x%02x%02x",
             guid[3], guid[2], guid[1], guid[0],
             guid[5], guid[4],
             guid[7], guid[6],
             guid[8], guid[9],
             guid[10], guid[11], guid[12], guid[13], guid[14], guid[15]);
}

int gpt_guid_is_zero(const uint8_t guid[16])
{
    for (int i = 0; i < 16; i++) {
        if (guid[i] != 0)
            return 0;
    }
    return 1;
}

int gpt_guid_eq(const uint8_t a[16], const uint8_t b[16])
{
    return memcmp(a, b, 16) == 0;
}

void gpt_set_name_ascii(gpt_entry_t *entry, const char *ascii)
{
    memset(entry->name, 0, sizeof(entry->name));
    if (!ascii)
        return;
    for (size_t i = 0; ascii[i] && i < 35; i++)
        entry->name[i] = (uint16_t)(uint8_t)ascii[i];
}

const char *gpt_type_name(const uint8_t type_guid[16])
{
    if (gpt_guid_eq(type_guid, GPT_GUID_BIOS_BOOT))
        return "BIOS boot";
    if (gpt_guid_eq(type_guid, GPT_GUID_ESP))
        return "EFI System";
    if (gpt_guid_eq(type_guid, GPT_GUID_LINUX_FS))
        return "Linux filesystem";
    return "Unknown";
}

static int gpt_validate_header(device_handle_t *dev, uint64_t lba,
                               gpt_header_t *hdr_out,
                               gpt_entry_t entries_out[GPT_MAX_ENTRIES])
{
    uint8_t sec[SECTOR_SIZE];
    if (lba >= dev->sector_count)
        return -1;
    if (device_read_sectors(dev, lba, 1, sec) != 0)
        return -1;

    gpt_header_t hdr;
    memcpy(&hdr, sec, sizeof(hdr));
    if (hdr.signature != GPT_SIGNATURE)
        return -1;
    if (hdr.header_size < GPT_HEADER_SIZE || hdr.header_size > SECTOR_SIZE)
        return -1;
    if (hdr.my_lba != lba)
        return -1;

    uint32_t saved_crc = hdr.crc32;
    uint8_t tmp[SECTOR_SIZE];
    memcpy(tmp, sec, SECTOR_SIZE);
    ((gpt_header_t *)tmp)->crc32 = 0;
    if (gpt_crc32(tmp, hdr.header_size) != saved_crc)
        return -1;

    if (hdr.size_of_partition_entry != GPT_ENTRY_SIZE ||
        hdr.num_partition_entries == 0 ||
        hdr.num_partition_entries > GPT_MAX_ENTRIES)
        return -1;

    uint32_t array_bytes = hdr.num_partition_entries * hdr.size_of_partition_entry;
    uint32_t array_secs = (array_bytes + SECTOR_SIZE - 1U) / SECTOR_SIZE;
    if (hdr.partition_entry_lba + array_secs > dev->sector_count)
        return -1;

    uint8_t raw[GPT_MAX_ENTRIES * GPT_ENTRY_SIZE];
    memset(raw, 0, sizeof(raw));
    if (device_read_sectors(dev, hdr.partition_entry_lba, array_secs, raw) != 0)
        return -1;

    if (gpt_crc32(raw, array_bytes) != hdr.partition_entry_array_crc32)
        return -1;

    if (hdr_out)
        *hdr_out = hdr;
    if (entries_out) {
        memset(entries_out, 0, sizeof(gpt_entry_t) * GPT_MAX_ENTRIES);
        memcpy(entries_out, raw, array_bytes);
    }
    return 0;
}

int gpt_read_table(device_handle_t *dev, gpt_header_t *hdr_out,
                   gpt_entry_t entries_out[GPT_MAX_ENTRIES])
{
    if (!dev || dev->sector_count < 68ULL)
        return -1;

    mbr_sector_t mbr;
    if (device_read_sectors(dev, 0, 1, &mbr) != 0 || mbr.boot_signature != MBR_SIGNATURE)
        return -1;

    int has_prot = 0;
    for (int i = 0; i < 4; i++) {
        if (mbr.entries[i].partition_type == MBR_TYPE_GPT_PROTECTIVE &&
            mbr.entries[i].sector_count > 0) {
            has_prot = 1;
            break;
        }
    }
    if (!has_prot)
        return -1;

    if (gpt_validate_header(dev, 1ULL, hdr_out, entries_out) == 0)
        return 0;
    if (gpt_validate_header(dev, dev->sector_count - 1ULL, hdr_out, entries_out) == 0)
        return 0;
    return -1;
}

int gpt_write_table(device_handle_t *dev, const uint8_t disk_guid[16],
                    const gpt_entry_t entries[GPT_MAX_ENTRIES],
                    uint32_t num_entries)
{
    if (!dev || dev->sector_count < 68ULL)
        return -1;
    if (num_entries == 0 || num_entries > GPT_MAX_ENTRIES)
        num_entries = GPT_MAX_ENTRIES;

    /* 1. Write Protective MBR at LBA 0 (preserve boot code bytes 0..439). */
    mbr_sector_t mbr;
    if (device_read_sectors(dev, 0, 1, &mbr) != 0)
        memset(&mbr, 0, sizeof(mbr));
    memset(mbr.boot_code + 440, 0, sizeof(mbr.boot_code) - 440);
    memset(mbr.entries, 0, sizeof(mbr.entries));

    uint64_t prot_secs = dev->sector_count - 1ULL;
    mbr.entries[0].boot_indicator = MBR_INACTIVE;
    mbr.entries[0].start_head = 0x00;
    mbr.entries[0].start_sector = 0x02;
    mbr.entries[0].start_cylinder = 0x00;
    mbr.entries[0].partition_type = MBR_TYPE_GPT_PROTECTIVE;
    mbr.entries[0].end_head = 0xFF;
    mbr.entries[0].end_sector = 0xFF;
    mbr.entries[0].end_cylinder = 0xFF;
    mbr.entries[0].lba_start = 1U;
    mbr.entries[0].sector_count = (prot_secs > 0xFFFFFFFFULL) ? 0xFFFFFFFFU : (uint32_t)prot_secs;
    mbr.boot_signature = MBR_SIGNATURE;

    if (device_write_sectors(dev, 0, 1, &mbr) != 0)
        return -1;

    /* 2. Build partition entry array (128 entries = 32 sectors). */
    uint8_t array_buf[GPT_MAX_ENTRIES * GPT_ENTRY_SIZE];
    memset(array_buf, 0, sizeof(array_buf));
    memcpy(array_buf, entries, (size_t)num_entries * GPT_ENTRY_SIZE);
    uint32_t array_bytes = GPT_MAX_ENTRIES * GPT_ENTRY_SIZE;
    uint32_t array_crc = gpt_crc32(array_buf, array_bytes);

    uint64_t backup_hdr_lba = dev->sector_count - 1ULL;
    uint64_t backup_arr_lba = backup_hdr_lba - GPT_ARRAY_SECTORS;
    uint64_t first_usable = GPT_FIRST_USABLE_LBA;
    uint64_t last_usable = backup_arr_lba - 1ULL;

    /* 3. Write primary and backup partition arrays. */
    if (device_write_sectors(dev, 2ULL, GPT_ARRAY_SECTORS, array_buf) != 0)
        return -1;
    if (device_write_sectors(dev, backup_arr_lba, GPT_ARRAY_SECTORS, array_buf) != 0)
        return -1;

    /* 4. Build and write primary GPT header at LBA 1. */
    uint8_t hdr_sec[SECTOR_SIZE];
    memset(hdr_sec, 0, sizeof(hdr_sec));
    gpt_header_t *pri = (gpt_header_t *)hdr_sec;
    pri->signature = GPT_SIGNATURE;
    pri->revision = GPT_REVISION_1_0;
    pri->header_size = GPT_HEADER_SIZE;
    pri->my_lba = 1ULL;
    pri->alternate_lba = backup_hdr_lba;
    pri->first_usable_lba = first_usable;
    pri->last_usable_lba = last_usable;
    memcpy(pri->disk_guid, disk_guid, 16);
    pri->partition_entry_lba = 2ULL;
    pri->num_partition_entries = GPT_MAX_ENTRIES;
    pri->size_of_partition_entry = GPT_ENTRY_SIZE;
    pri->partition_entry_array_crc32 = array_crc;
    pri->crc32 = gpt_crc32(hdr_sec, GPT_HEADER_SIZE);

    if (device_write_sectors(dev, 1ULL, 1, hdr_sec) != 0)
        return -1;

    /* 5. Build and write backup GPT header at last LBA. */
    pri->my_lba = backup_hdr_lba;
    pri->alternate_lba = 1ULL;
    pri->partition_entry_lba = backup_arr_lba;
    pri->crc32 = 0;
    pri->crc32 = gpt_crc32(hdr_sec, GPT_HEADER_SIZE);

    if (device_write_sectors(dev, backup_hdr_lba, 1, hdr_sec) != 0)
        return -1;

    return device_sync(dev);
}
