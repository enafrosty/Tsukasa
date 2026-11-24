/*
 * Project Tsukasa - GUID Partition Table (GPT) Definitions & Helpers
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

#ifndef _TSUKASA_GPT_H
#define _TSUKASA_GPT_H

#include "disktools.h"
#include <stdint.h>
#include <stddef.h>

#define GPT_SIGNATURE          0x5452415020494645ULL
#define GPT_REVISION_1_0       0x00010000U
#define GPT_HEADER_SIZE        92U
#define GPT_MAX_ENTRIES        128U
#define GPT_ENTRY_SIZE         128U
#define GPT_ARRAY_SECTORS      ((GPT_MAX_ENTRIES * GPT_ENTRY_SIZE) / SECTOR_SIZE)
#define GPT_FIRST_USABLE_LBA   (2ULL + GPT_ARRAY_SECTORS)

#pragma pack(push, 1)
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
} gpt_header_t;

typedef struct {
    uint8_t  type_guid[16];
    uint8_t  part_guid[16];
    uint64_t start_lba;
    uint64_t end_lba;
    uint64_t attributes;
    uint16_t name[36];
} gpt_entry_t;
#pragma pack(pop)

_Static_assert(sizeof(gpt_header_t) == 92, "GPT header must be 92 bytes");
_Static_assert(sizeof(gpt_entry_t) == 128, "GPT entry must be 128 bytes");

extern const uint8_t GPT_GUID_BIOS_BOOT[16];
extern const uint8_t GPT_GUID_ESP[16];
extern const uint8_t GPT_GUID_LINUX_FS[16];

uint32_t    gpt_crc32(const void *data, size_t len);
void        gpt_gen_uuid(uint8_t out[16]);
void        gpt_format_uuid(const uint8_t guid[16], char out[37]);
int         gpt_guid_is_zero(const uint8_t guid[16]);
int         gpt_guid_eq(const uint8_t a[16], const uint8_t b[16]);
void        gpt_set_name_ascii(gpt_entry_t *entry, const char *ascii);
const char *gpt_type_name(const uint8_t type_guid[16]);

int         gpt_read_table(device_handle_t *dev, gpt_header_t *hdr_out,
                           gpt_entry_t entries_out[GPT_MAX_ENTRIES]);
int         gpt_write_table(device_handle_t *dev, const uint8_t disk_guid[16],
                            const gpt_entry_t entries[GPT_MAX_ENTRIES],
                            uint32_t num_entries);

#endif /* _TSUKASA_GPT_H */
