/*
 * Project Tsukasa - MBR and GPT Partition Manager (fdisk)
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
#include "../include/gpt.h"
#include "../include/mbr.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    int                   is_gpt;
    mbr_partition_entry_t mbr[4];
    uint8_t               disk_guid[16];
    gpt_entry_t           gpt[GPT_MAX_ENTRIES];
} fdisk_state_t;

static void print_usage(const char *prog)
{
    fprintf(stderr, "Usage: %s [-l | --gpt | --uefi] <device_or_image_file>\n", prog);
}

static void print_help(void)
{
    printf("\nHelp:\n");
    printf("  p   print the partition table\n");
    printf("  g   create a new empty GPT partition table\n");
    printf("  o   create a new empty DOS (MBR) partition table\n");
    printf("  n   add a new partition\n");
    printf("  d   delete a partition\n");
    printf("  t   change a partition's type\n");
    printf("  a   toggle a partition's bootable flag (MBR)\n");
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

static uint64_t parse_end_lba(const char *line, uint64_t first_lba, uint64_t max_lba)
{
    if (!line || !line[0])
        return max_lba;

    if (line[0] == '+') {
        char buf[64];
        strncpy(buf, line + 1, sizeof(buf) - 1);
        buf[sizeof(buf) - 1] = '\0';
        size_t len = strlen(buf);
        if (len == 0)
            return max_lba;
        char suffix = buf[len - 1];
        uint64_t mul = 1ULL;
        if (suffix == 'M' || suffix == 'm') {
            buf[len - 1] = '\0';
            mul = 2048ULL;
        } else if (suffix == 'G' || suffix == 'g') {
            buf[len - 1] = '\0';
            mul = 2048ULL * 1024ULL;
        } else if (suffix == 'K' || suffix == 'k') {
            buf[len - 1] = '\0';
            mul = 2ULL;
        }
        uint64_t count = strtoull(buf, NULL, 10) * mul;
        if (count == 0)
            return first_lba;
        uint64_t last = first_lba + count - 1ULL;
        return (last > max_lba) ? max_lba : last;
    }

    uint64_t val = strtoull(line, NULL, 10);
    if (val < first_lba || val > max_lba)
        return max_lba;
    return val;
}

static void print_table(device_handle_t *dev, const fdisk_state_t *st)
{
    char sz_buf[32];
    format_size(device_get_size(dev), sz_buf, sizeof(sz_buf));
    printf("\nDisk %s: %s, %llu sectors\n",
           dev->path, sz_buf, (unsigned long long)dev->sector_count);
    printf("Units: sectors of 1 * %u = %u bytes\n", dev->sector_size, dev->sector_size);
    printf("Disklabel type: %s\n", st->is_gpt ? "gpt" : "dos");

    if (st->is_gpt) {
        char dguid[37];
        gpt_format_uuid(st->disk_guid, dguid);
        printf("Disk identifier: %s\n\n", dguid);
        printf("Part  Start        End          Sectors      Size       Type\n");
        printf("----  -----------  -----------  -----------  ---------  -------------------------\n");
        int found = 0;
        for (uint32_t i = 0; i < GPT_MAX_ENTRIES; i++) {
            const gpt_entry_t *e = &st->gpt[i];
            if (gpt_guid_is_zero(e->type_guid))
                continue;
            found++;
            uint64_t secs = (e->end_lba >= e->start_lba) ? (e->end_lba - e->start_lba + 1ULL) : 0ULL;
            char part_sz[32];
            format_size(secs * dev->sector_size, part_sz, sizeof(part_sz));
            printf("%-4u  %-11llu  %-11llu  %-11llu  %-9s  %s\n",
                   i + 1u,
                   (unsigned long long)e->start_lba,
                   (unsigned long long)e->end_lba,
                   (unsigned long long)secs,
                   part_sz,
                   gpt_type_name(e->type_guid));
        }
        if (!found)
            printf("(no partitions defined)\n");
        printf("\n");
        return;
    }

    printf("\nSlot  Boot  Start        End          Sectors      Size       Type\n");
    printf("----  ----  -----------  -----------  -----------  ---------  -------------------------\n");

    int found = 0;
    for (int i = 0; i < 4; i++) {
        const mbr_partition_entry_t *e = &st->mbr[i];
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

static void init_gpt_state(fdisk_state_t *st)
{
    memset(st, 0, sizeof(*st));
    st->is_gpt = 1;
    gpt_gen_uuid(st->disk_guid);
}

static void init_mbr_state(fdisk_state_t *st)
{
    memset(st, 0, sizeof(*st));
    st->is_gpt = 0;
}

static void do_new_mbr_partition(device_handle_t *dev, fdisk_state_t *st)
{
    int slot = -1;
    for (int i = 0; i < 4; i++) {
        if (st->mbr[i].sector_count == 0) {
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
    if (st->mbr[slot].sector_count > 0) {
        printf("Partition %d is already defined. Delete it first.\n", slot + 1);
        return;
    }

    uint32_t default_start = 2048;
    for (int i = 0; i < 4; i++) {
        if (st->mbr[i].sector_count > 0) {
            uint32_t end_lba = st->mbr[i].lba_start + st->mbr[i].sector_count;
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

    uint32_t default_last = (uint32_t)(dev->sector_count - 1);
    printf("Last sector, +/-sectors or +/-size{M,G} (%u-%u, default %u): ",
           first_lba, default_last, default_last);
    rc = read_line(line, sizeof(line));
    uint32_t last_lba = (uint32_t)parse_end_lba(rc > 0 ? line : "", first_lba, default_last);
    if (last_lba < first_lba) {
        printf("Invalid sector range.\n");
        return;
    }

    uint32_t count = last_lba - first_lba + 1;
    mbr_partition_entry_t *e = &st->mbr[slot];
    memset(e, 0, sizeof(*e));
    e->boot_indicator = MBR_INACTIVE;
    e->partition_type = MBR_TYPE_LINUX_EXT;
    e->lba_start = first_lba;
    e->sector_count = count;
    lba_to_chs(first_lba, &e->start_head, &e->start_sector, &e->start_cylinder);
    lba_to_chs(last_lba, &e->end_head, &e->end_sector, &e->end_cylinder);

    char sz_str[32];
    format_size((uint64_t)count * dev->sector_size, sz_str, sizeof(sz_str));
    printf("Created a new partition %d of type 'Linux native' and of size %s.\n", slot + 1, sz_str);
}

static void do_new_gpt_partition(device_handle_t *dev, fdisk_state_t *st)
{
    if (dev->sector_count < 68ULL) {
        printf("Device too small for GPT.\n");
        return;
    }
    int slot = -1;
    for (uint32_t i = 0; i < GPT_MAX_ENTRIES; i++) {
        if (gpt_guid_is_zero(st->gpt[i].type_guid)) {
            slot = (int)i;
            break;
        }
    }
    if (slot < 0) {
        printf("All 128 GPT entries are in use.\n");
        return;
    }

    char line[64];
    printf("Partition number (1-128, default %d): ", slot + 1);
    int rc = read_line(line, sizeof(line));
    if (rc > 0) {
        int chosen = atoi(line);
        if (chosen >= 1 && chosen <= (int)GPT_MAX_ENTRIES)
            slot = chosen - 1;
    }
    if (!gpt_guid_is_zero(st->gpt[slot].type_guid)) {
        printf("Partition %d is already defined. Delete it first.\n", slot + 1);
        return;
    }

    uint64_t min_usable = GPT_FIRST_USABLE_LBA;
    uint64_t max_usable = dev->sector_count - 1ULL - GPT_ARRAY_SECTORS - 1ULL;
    uint64_t default_start = 2048ULL;
    if (default_start < min_usable)
        default_start = min_usable;

    for (uint32_t i = 0; i < GPT_MAX_ENTRIES; i++) {
        if (!gpt_guid_is_zero(st->gpt[i].type_guid)) {
            uint64_t next = st->gpt[i].end_lba + 1ULL;
            uint64_t aligned = ((next + 2047ULL) / 2048ULL) * 2048ULL;
            if (aligned > default_start)
                default_start = aligned;
        }
    }
    if (default_start > max_usable) {
        printf("No usable space left on device.\n");
        return;
    }

    uint64_t first_lba = default_start;
    printf("First sector (%llu-%llu, default %llu): ",
           (unsigned long long)min_usable, (unsigned long long)max_usable,
           (unsigned long long)default_start);
    rc = read_line(line, sizeof(line));
    if (rc > 0) {
        uint64_t val = strtoull(line, NULL, 10);
        if (val >= min_usable && val <= max_usable)
            first_lba = val;
    }

    printf("Last sector, +/-sectors or +/-size{M,G} (%llu-%llu, default %llu): ",
           (unsigned long long)first_lba, (unsigned long long)max_usable,
           (unsigned long long)max_usable);
    rc = read_line(line, sizeof(line));
    uint64_t last_lba = parse_end_lba(rc > 0 ? line : "", first_lba, max_usable);
    if (last_lba < first_lba) {
        printf("Invalid sector range.\n");
        return;
    }

    printf("Partition type (1=Linux filesystem, 2=EFI System, 3=BIOS boot, default 1): ");
    rc = read_line(line, sizeof(line));
    const uint8_t *tguid = GPT_GUID_LINUX_FS;
    const char *pname = "Tsukasa Root";
    if (rc > 0) {
        if (line[0] == '2') {
            tguid = GPT_GUID_ESP;
            pname = "EFI System Partition";
        } else if (line[0] == '3') {
            tguid = GPT_GUID_BIOS_BOOT;
            pname = "BIOS Boot Partition";
        }
    }

    gpt_entry_t *e = &st->gpt[slot];
    memset(e, 0, sizeof(*e));
    memcpy(e->type_guid, tguid, 16);
    gpt_gen_uuid(e->part_guid);
    e->start_lba = first_lba;
    e->end_lba = last_lba;
    gpt_set_name_ascii(e, pname);

    uint64_t secs = last_lba - first_lba + 1ULL;
    char sz_str[32];
    format_size(secs * dev->sector_size, sz_str, sizeof(sz_str));
    printf("Created GPT partition %d of type '%s' and of size %s.\n",
           slot + 1, gpt_type_name(tguid), sz_str);
}

static void do_delete_partition(fdisk_state_t *st)
{
    char line[64];
    printf("Partition number (1-%d): ", st->is_gpt ? (int)GPT_MAX_ENTRIES : 4);
    if (read_line(line, sizeof(line)) <= 0)
        return;
    int slot = atoi(line) - 1;

    if (st->is_gpt) {
        if (slot < 0 || slot >= (int)GPT_MAX_ENTRIES || gpt_guid_is_zero(st->gpt[slot].type_guid)) {
            printf("Partition %d is not defined.\n", slot + 1);
            return;
        }
        memset(&st->gpt[slot], 0, sizeof(gpt_entry_t));
    } else {
        if (slot < 0 || slot >= 4 || st->mbr[slot].sector_count == 0) {
            printf("Partition %d is not defined.\n", slot + 1);
            return;
        }
        memset(&st->mbr[slot], 0, sizeof(mbr_partition_entry_t));
    }
    printf("Partition %d has been deleted.\n", slot + 1);
}

static void do_change_type(fdisk_state_t *st)
{
    char line[64];
    printf("Partition number (1-%d): ", st->is_gpt ? (int)GPT_MAX_ENTRIES : 4);
    if (read_line(line, sizeof(line)) <= 0)
        return;
    int slot = atoi(line) - 1;

    if (st->is_gpt) {
        if (slot < 0 || slot >= (int)GPT_MAX_ENTRIES || gpt_guid_is_zero(st->gpt[slot].type_guid)) {
            printf("Partition %d is not defined.\n", slot + 1);
            return;
        }
        printf("Type (1=Linux filesystem, 2=EFI System, 3=BIOS boot): ");
        if (read_line(line, sizeof(line)) <= 0)
            return;
        if (line[0] == '2')
            memcpy(st->gpt[slot].type_guid, GPT_GUID_ESP, 16);
        else if (line[0] == '3')
            memcpy(st->gpt[slot].type_guid, GPT_GUID_BIOS_BOOT, 16);
        else
            memcpy(st->gpt[slot].type_guid, GPT_GUID_LINUX_FS, 16);
        printf("Changed type of partition %d to %s.\n", slot + 1, gpt_type_name(st->gpt[slot].type_guid));
        return;
    }

    if (slot < 0 || slot >= 4 || st->mbr[slot].sector_count == 0) {
        printf("Partition %d is not defined.\n", slot + 1);
        return;
    }
    printf("Hex code (e.g. 0c, 83, ee): ");
    if (read_line(line, sizeof(line)) <= 0)
        return;
    uint32_t type = (uint32_t)strtoul(line, NULL, 16);
    st->mbr[slot].partition_type = (uint8_t)type;
    printf("Changed type of partition %d to 0x%02X (%s).\n",
           slot + 1, (uint8_t)type, mbr_type_name((uint8_t)type));
}

static void do_toggle_bootable(fdisk_state_t *st)
{
    if (st->is_gpt) {
        printf("Bootable flag is MBR-only.\n");
        return;
    }
    char line[64];
    printf("Partition number (1-4): ");
    if (read_line(line, sizeof(line)) <= 0)
        return;
    int slot = atoi(line) - 1;
    if (slot < 0 || slot >= 4 || st->mbr[slot].sector_count == 0) {
        printf("Partition %d is not defined.\n", slot + 1);
        return;
    }
    if (st->mbr[slot].boot_indicator == MBR_BOOTABLE) {
        st->mbr[slot].boot_indicator = MBR_INACTIVE;
        printf("Partition %d is no longer bootable.\n", slot + 1);
    } else {
        st->mbr[slot].boot_indicator = MBR_BOOTABLE;
        printf("Partition %d is now bootable.\n", slot + 1);
    }
}

static int do_write_table(device_handle_t *dev, const fdisk_state_t *st)
{
    if (st->is_gpt) {
        if (gpt_write_table(dev, st->disk_guid, st->gpt, GPT_MAX_ENTRIES) != 0) {
            printf("Error: Failed to write GPT to %s\n", dev->path);
            return -1;
        }
    } else {
        mbr_sector_t sec;
        if (device_read_sectors(dev, 0, 1, &sec) != 0)
            memset(&sec, 0, sizeof(sec));
        memcpy(sec.entries, st->mbr, sizeof(sec.entries));
        sec.boot_signature = MBR_SIGNATURE;
        if (device_write_sectors(dev, 0, 1, &sec) != 0) {
            printf("Error: Failed to write MBR to %s\n", dev->path);
            return -1;
        }
        device_sync(dev);
    }

    (void)device_reread_partitions(dev);
    printf("The partition table has been altered. Syncing disks.\n");
    return 0;
}

static int apply_scripted_gpt(device_handle_t *dev, int include_bios_boot)
{
    if (dev->sector_count < 96ULL * 2048ULL) {
        fprintf(stderr, "fdisk: device %s too small for scripted GPT layout (need >= 96 MiB)\n", dev->path);
        return -1;
    }

    fdisk_state_t st;
    init_gpt_state(&st);

    uint64_t cur = 2048ULL;
    uint64_t max_usable = dev->sector_count - 1ULL - GPT_ARRAY_SECTORS - 1ULL;
    int idx = 0;

    if (include_bios_boot) {
        st.gpt[idx].start_lba = cur;
        st.gpt[idx].end_lba = cur + 2048ULL - 1ULL;
        memcpy(st.gpt[idx].type_guid, GPT_GUID_BIOS_BOOT, 16);
        gpt_gen_uuid(st.gpt[idx].part_guid);
        gpt_set_name_ascii(&st.gpt[idx], "BIOS Boot");
        cur += 2048ULL;
        idx++;
    }

    uint64_t esp_secs = 64ULL * 2048ULL;
    st.gpt[idx].start_lba = cur;
    st.gpt[idx].end_lba = cur + esp_secs - 1ULL;
    memcpy(st.gpt[idx].type_guid, GPT_GUID_ESP, 16);
    gpt_gen_uuid(st.gpt[idx].part_guid);
    gpt_set_name_ascii(&st.gpt[idx], "EFI System Partition");
    cur += esp_secs;
    idx++;

    st.gpt[idx].start_lba = cur;
    st.gpt[idx].end_lba = max_usable;
    memcpy(st.gpt[idx].type_guid, GPT_GUID_LINUX_FS, 16);
    gpt_gen_uuid(st.gpt[idx].part_guid);
    gpt_set_name_ascii(&st.gpt[idx], "Tsukasa Root");

    if (do_write_table(dev, &st) != 0)
        return -1;
    print_table(dev, &st);
    return 0;
}

int main(int argc, char *argv[])
{
    const char *path = NULL;
    int list_only = 0;
    int scripted_gpt = 0;
    int scripted_uefi = 0;

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-l") == 0) {
            list_only = 1;
        } else if (strcmp(argv[i], "--gpt") == 0) {
            scripted_gpt = 1;
        } else if (strcmp(argv[i], "--uefi") == 0) {
            scripted_uefi = 1;
        } else if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0) {
            print_usage(argv[0]);
            return 0;
        } else if (argv[i][0] == '-') {
            fprintf(stderr, "fdisk: unknown option %s\n", argv[i]);
            print_usage(argv[0]);
            return 1;
        } else {
            path = argv[i];
        }
    }

    if (!path) {
        print_usage(argv[0]);
        return 1;
    }

    device_handle_t dev;
    if (device_open(path, list_only ? O_RDONLY : O_RDWR, &dev) != 0) {
        fprintf(stderr, "fdisk: cannot open %s\n", path);
        return 1;
    }

    if (scripted_gpt || scripted_uefi) {
        int rc = apply_scripted_gpt(&dev, scripted_gpt ? 1 : 0);
        device_close(&dev);
        return (rc == 0) ? 0 : 1;
    }

    fdisk_state_t st;
    init_mbr_state(&st);
    gpt_header_t hdr;
    if (gpt_read_table(&dev, &hdr, st.gpt) == 0) {
        st.is_gpt = 1;
        memcpy(st.disk_guid, hdr.disk_guid, 16);
    } else {
        mbr_sector_t sec;
        if (device_read_sectors(&dev, 0, 1, &sec) == 0 && sec.boot_signature == MBR_SIGNATURE)
            memcpy(st.mbr, sec.entries, sizeof(st.mbr));
    }

    if (list_only) {
        print_table(&dev, &st);
        device_close(&dev);
        return 0;
    }

    printf("Welcome to Tsukasa fdisk (MBR/GPT partition tool).\n");
    printf("Changes will remain in memory only, until you decide to write them.\n");
    print_table(&dev, &st);

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
            print_table(&dev, &st);
        } else if (c == 'g') {
            init_gpt_state(&st);
            printf("Created a new GPT disklabel.\n");
        } else if (c == 'o') {
            init_mbr_state(&st);
            printf("Created a new DOS (MBR) disklabel.\n");
        } else if (c == 'n') {
            if (st.is_gpt)
                do_new_gpt_partition(&dev, &st);
            else
                do_new_mbr_partition(&dev, &st);
        } else if (c == 'd') {
            do_delete_partition(&st);
        } else if (c == 't') {
            do_change_type(&st);
        } else if (c == 'a') {
            do_toggle_bootable(&st);
        } else if (c == 'w') {
            if (do_write_table(&dev, &st) == 0) {
                device_close(&dev);
                return 0;
            }
        } else if (c == 'q') {
            break;
        } else {
            printf("%c: unknown command\n", c);
        }
    }

    device_close(&dev);
    return 0;
}
