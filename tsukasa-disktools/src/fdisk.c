/*
 * Project Tsukasa — Master Boot Record (MBR) Partition Manager (fdisk)
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

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

static void print_help(void)
{
    printf("\nHelp:\n");
    printf("  p   print the partition table\n");
    printf("  n   add a new partition\n");
    printf("  d   delete a partition\n");
    printf("  t   change a partition's type\n");
    printf("  a   toggle a partition's bootable flag\n");
    printf("  w   write table to disk and exit\n");
    printf("  q   quit without saving changes\n\n");
}

static int read_line(char *buf, size_t max)
{
    if (!buf || max == 0)
        return 0;

    size_t idx = 0;
    while (idx < max - 1) {
        char c;
        ssize_t n = read(STDIN_FILENO, &c, 1);
        if (n <= 0) {
            if (idx == 0)
                return -1;
            break;
        }
        if (c == '\r')
            continue;
        if (c == '\n')
            break;
        buf[idx++] = c;
    }
    buf[idx] = '\0';

    /* Trim leading and trailing whitespace */
    char *start = buf;
    while (*start && isspace((unsigned char)*start))
        start++;
    if (start != buf)
        memmove(buf, start, strlen(start) + 1);

    size_t len = strlen(buf);
    while (len > 0 && isspace((unsigned char)buf[len - 1])) {
        buf[len - 1] = '\0';
        len--;
    }

    return (int)len;
}

static void print_table(device_handle_t *dev, const mbr_partition_entry_t *entries)
{
    char sz_buf[32];
    format_size(device_get_size(dev), sz_buf, sizeof(sz_buf));
    printf("\nDisk %s: %s, %llu sectors\n", dev->path, sz_buf, (unsigned long long)dev->sector_count);
    printf("Units: sectors of 1 * %u = %u bytes\n\n", dev->sector_size, dev->sector_size);

    printf("Slot  Boot  Start        End          Sectors      Size       Type\n");
    printf("----  ----  -----------  -----------  -----------  ---------  -------------------------\n");

    int found = 0;
    for (int i = 0; i < 4; i++) {
        const mbr_partition_entry_t *e = &entries[i];
        if (e->sector_count == 0)
            continue;

        found++;
        char part_sz[32];
        format_size((uint64_t)e->sector_count * dev->sector_size, part_sz, sizeof(part_sz));

        uint32_t end_lba = e->lba_start + e->sector_count - 1;
        printf("%-4d  %-4s  %-11u  %-11u  %-11u  %-9s  %s (0x%02X)\n",
               i + 1,
               (e->boot_indicator == MBR_BOOTABLE) ? "*" : "",
               e->lba_start,
               end_lba,
               e->sector_count,
               part_sz,
               mbr_type_name(e->partition_type),
               e->partition_type);
    }

    if (!found)
        printf("(no partitions defined)\n");
    printf("\n");
}

static void do_new_partition(device_handle_t *dev, mbr_partition_entry_t *entries)
{
    int slot = -1;
    for (int i = 0; i < 4; i++) {
        if (entries[i].sector_count == 0) {
            slot = i;
            break;
        }
    }

    if (slot < 0) {
        printf("All 4 primary partitions are in use. Delete one first.\n");
        return;
    }

    char line[64];
    printf("Partition number (1-4, default %d): ", slot + 1);
    int rc = read_line(line, sizeof(line));
    if (rc > 0) {
        int chosen = atoi(line);
        if (chosen >= 1 && chosen <= 4)
            slot = chosen - 1;
    }

    if (entries[slot].sector_count > 0) {
        printf("Partition %d is already defined. Delete it first.\n", slot + 1);
        return;
    }

    /* Compute default first sector (1 MiB aligned = 2048) */
    uint32_t default_start = 2048;
    for (int i = 0; i < 4; i++) {
        if (entries[i].sector_count > 0) {
            uint32_t end_lba = entries[i].lba_start + entries[i].sector_count;
            uint32_t aligned_end = ((end_lba + 2047u) / 2048u) * 2048u;
            if (aligned_end > default_start)
                default_start = aligned_end;
        }
    }

    if (default_start >= dev->sector_count) {
        printf("No space left on device.\n");
        return;
    }

    uint32_t first_lba = default_start;
    printf("First sector (%u-%llu, default %u): ",
           default_start, (unsigned long long)dev->sector_count - 1, default_start);
    rc = read_line(line, sizeof(line));
    if (rc > 0) {
        uint32_t val = (uint32_t)strtoul(line, NULL, 10);
        if (val >= 2048 && val < dev->sector_count)
            first_lba = val;
    }

    /* Prompt for last sector or size */
    uint32_t default_last = (uint32_t)(dev->sector_count - 1);
    printf("Last sector, +/-sectors or +/-size{M,G} (%u-%u, default %u): ",
           first_lba, default_last, default_last);
    rc = read_line(line, sizeof(line));

    uint32_t last_lba = default_last;
    if (rc > 0) {
        if (line[0] == '+') {
            size_t len = strlen(line);
            char suffix = line[len - 1];
            if (suffix == 'M' || suffix == 'm') {
                line[len - 1] = '\0';
                uint32_t mib = (uint32_t)strtoul(line + 1, NULL, 10);
                uint32_t secs = mib * 2048u;
                last_lba = first_lba + secs - 1;
            } else if (suffix == 'G' || suffix == 'g') {
                line[len - 1] = '\0';
                uint32_t gib = (uint32_t)strtoul(line + 1, NULL, 10);
                uint32_t secs = gib * 2048u * 1024u;
                last_lba = first_lba + secs - 1;
            } else {
                uint32_t secs = (uint32_t)strtoul(line + 1, NULL, 10);
                last_lba = first_lba + secs - 1;
            }
        } else {
            uint32_t val = (uint32_t)strtoul(line, NULL, 10);
            if (val >= first_lba && val <= default_last)
                last_lba = val;
        }
    }

    if (last_lba >= dev->sector_count)
        last_lba = default_last;
    if (last_lba < first_lba) {
        printf("Invalid sector range.\n");
        return;
    }

    uint32_t count = last_lba - first_lba + 1;

    mbr_partition_entry_t *e = &entries[slot];
    memset(e, 0, sizeof(*e));
    e->boot_indicator = MBR_INACTIVE;
    e->partition_type = MBR_TYPE_LINUX_EXT;
    e->lba_start = first_lba;
    e->sector_count = count;

    lba_to_chs(first_lba, &e->start_head, &e->start_sector, &e->start_cylinder);
    lba_to_chs(last_lba, &e->end_head, &e->end_sector, &e->end_cylinder);

    char sz_str[32];
    format_size((uint64_t)count * dev->sector_size, sz_str, sizeof(sz_str));
    printf("Created a new partition %d of type 'Linux native (Ext2/3/4)' and of size %s.\n",
           slot + 1, sz_str);
}

static void do_delete_partition(mbr_partition_entry_t *entries)
{
    char line[64];
    printf("Partition number (1-4): ");
    int rc = read_line(line, sizeof(line));
    if (rc <= 0)
        return;

    int slot = atoi(line) - 1;
    if (slot < 0 || slot >= 4) {
        printf("Invalid partition number.\n");
        return;
    }

    if (entries[slot].sector_count == 0) {
        printf("Partition %d is not defined.\n", slot + 1);
        return;
    }

    memset(&entries[slot], 0, sizeof(mbr_partition_entry_t));
    printf("Partition %d has been deleted.\n", slot + 1);
}

static void do_change_type(mbr_partition_entry_t *entries)
{
    char line[64];
    printf("Partition number (1-4): ");
    int rc = read_line(line, sizeof(line));
    if (rc <= 0)
        return;

    int slot = atoi(line) - 1;
    if (slot < 0 || slot >= 4) {
        printf("Invalid partition number.\n");
        return;
    }

    if (entries[slot].sector_count == 0) {
        printf("Partition %d is not defined.\n", slot + 1);
        return;
    }

    printf("Hex code (e.g. 0c, 83, ee): ");
    rc = read_line(line, sizeof(line));
    if (rc <= 0)
        return;

    uint32_t type = (uint32_t)strtoul(line, NULL, 16);
    entries[slot].partition_type = (uint8_t)type;
    printf("Changed type of partition %d to 0x%02X (%s).\n",
           slot + 1, (uint8_t)type, mbr_type_name((uint8_t)type));
}

static void do_toggle_bootable(mbr_partition_entry_t *entries)
{
    char line[64];
    printf("Partition number (1-4): ");
    int rc = read_line(line, sizeof(line));
    if (rc <= 0)
        return;

    int slot = atoi(line) - 1;
    if (slot < 0 || slot >= 4) {
        printf("Invalid partition number.\n");
        return;
    }

    if (entries[slot].sector_count == 0) {
        printf("Partition %d is not defined.\n", slot + 1);
        return;
    }

    if (entries[slot].boot_indicator == MBR_BOOTABLE) {
        entries[slot].boot_indicator = MBR_INACTIVE;
        printf("Partition %d is no longer bootable.\n", slot + 1);
    } else {
        entries[slot].boot_indicator = MBR_BOOTABLE;
        printf("Partition %d is now bootable.\n", slot + 1);
    }
}

static int do_write_table(device_handle_t *dev, const mbr_partition_entry_t *entries)
{
    mbr_sector_t sec;
    memset(&sec, 0, sizeof(sec));

    /* Preserve existing Stage 1 boot code in bytes 0..445 if present */
    if (device_read_sectors(dev, 0, 1, &sec) != 0)
        memset(&sec, 0, sizeof(sec));

    memcpy(sec.entries, entries, sizeof(sec.entries));
    sec.boot_signature = MBR_SIGNATURE;

    if (device_write_sectors(dev, 0, 1, &sec) != 0) {
        printf("Error: Failed to write MBR to %s\n", dev->path);
        return -1;
    }

    device_sync(dev);
    printf("The partition table has been altered. Syncing disks.\n");
    return 0;
}

int main(int argc, char *argv[])
{
    if (argc < 2) {
        fprintf(stderr, "Usage: %s <device_or_image_file>\n", argv[0]);
        return 1;
    }

    const char *path = argv[1];
    int list_only = 0;
    if (strcmp(argv[1], "-l") == 0) {
        if (argc < 3) {
            fprintf(stderr, "Usage: %s -l <device_or_image_file>\n", argv[0]);
            return 1;
        }
        path = argv[2];
        list_only = 1;
    }

    device_handle_t dev;
    if (device_open(path, list_only ? O_RDONLY : O_RDWR, &dev) != 0) {
        fprintf(stderr, "fdisk: cannot open %s\n", path);
        return 1;
    }

    mbr_sector_t sec;
    mbr_partition_entry_t entries[4];
    memset(entries, 0, sizeof(entries));

    if (device_read_sectors(&dev, 0, 1, &sec) == 0) {
        if (sec.boot_signature == MBR_SIGNATURE)
            memcpy(entries, sec.entries, sizeof(entries));
    }

    if (list_only) {
        print_table(&dev, entries);
        device_close(&dev);
        return 0;
    }

    printf("Welcome to Tsukasa fdisk (MBR/GPT partition tool).\n");
    printf("Changes will remain in memory only, until you decide to write them.\n\n");
    print_table(&dev, entries);

    char cmd_buf[32];
    for (;;) {
        printf("Command (m for help): ");
        int n = read_line(cmd_buf, sizeof(cmd_buf));
        if (n < 0) {
            printf("\n");
            break;
        }
        if (n == 0)
            continue;

        char c = cmd_buf[0];
        if (c == 'm' || c == 'h' || c == '?') {
            print_help();
        } else if (c == 'p') {
            print_table(&dev, entries);
        } else if (c == 'n') {
            do_new_partition(&dev, entries);
        } else if (c == 'd') {
            do_delete_partition(entries);
        } else if (c == 't') {
            do_change_type(entries);
        } else if (c == 'a') {
            do_toggle_bootable(entries);
        } else if (c == 'w') {
            if (do_write_table(&dev, entries) == 0) {
                device_close(&dev);
                return 0;
            }
        } else if (c == 'q') {
            break;
        } else {
            printf("%c: unknown command, type 'm' for help\n", c);
        }
    }

    device_close(&dev);
    return 0;
}
