/*
 * Project Tsukasa - Unified BIOS+UEFI GPT/ext4 OS Installer (installer)
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
#include <dirent.h>
#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#ifndef _WIN32
#include <sys/mount.h>
#endif

#define MIN_INSTALL_SECTORS  (96ULL * 2048ULL) /* 96 MiB minimum (1M BIOS + 64M ESP + >=31M root) */
#define BIOS_BOOT_SECTORS    2048ULL           /* 1 MiB */
#define ESP_SECTORS          (64ULL * 2048ULL) /* 64 MiB */
#define COPY_BUF_SIZE        32768u

static const char *const g_candidate_devices[] = {
    "/dev/sda",
    "/dev/vda",
    "/dev/nvme0n1",
    "/dev/hda",
    "/dev/sdb",
    "/dev/vdb",
    "/dev/nvme1n1",
    "/dev/hdb",
};

static const char *const g_bootx64_paths[] = {
    "/usr/share/limine/BOOTX64.EFI",
    "/boot/EFI/BOOT/BOOTX64.EFI",
};

static const char *const g_limine_sys_paths[] = {
    "/usr/share/limine/limine-bios.sys",
    "/boot/limine/limine-bios.sys",
    "/boot/limine-bios.sys",
    "/limine-bios.sys",
};

static const char *const g_bootblob_paths[] = {
    "/usr/share/limine/bootblob.bin",
    "/boot/bootblob.bin",
    "/bootblob.bin",
};

static const char *const g_kernel_paths[] = {
    "/boot/tsukasa_x64.elf",
    "/tsukasa_x64.elf",
};

static void log_msg(const char *fmt, ...)
{
    char buf[512];
    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    if (n <= 0)
        return;

    fputs(buf, stdout);
    fflush(stdout);

    int tty = open("/dev/tty0", O_WRONLY);
    if (tty >= 0) {
        size_t len = strlen(buf);
        (void)write(tty, buf, len);
        close(tty);
    }
}

static int token_is_erase(const char *s)
{
    static const char t[] = "erase";
    int i;
    if (!s)
        return 0;
    for (i = 0; t[i]; i++) {
        char c = s[i];
        if (c >= 'A' && c <= 'Z')
            c = (char)(c + 32);
        if (c != t[i])
            return 0;
    }
    return s[i] == '\0';
}

static int find_existing_file(const char *const paths[], size_t count,
                              char *out_path, size_t max)
{
    for (size_t i = 0; i < count; i++) {
        struct stat st;
        if (stat(paths[i], &st) == 0 && S_ISREG(st.st_mode) && st.st_size > 0) {
            strncpy(out_path, paths[i], max - 1);
            out_path[max - 1] = '\0';
            return 0;
        }
    }
    return -1;
}

static int read_entire_file(const char *path, uint8_t **out_buf, size_t *out_size)
{
    int fd = open(path, O_RDONLY);
    if (fd < 0)
        return -1;

    off_t sz = lseek(fd, 0, SEEK_END);
    if (sz < 0) {
        close(fd);
        return -1;
    }
    lseek(fd, 0, SEEK_SET);

    uint8_t *buf = (uint8_t *)malloc((size_t)(sz ? sz : 1));
    if (!buf) {
        close(fd);
        return -1;
    }

    size_t done = 0;
    while (done < (size_t)sz) {
        ssize_t n = read(fd, buf + done, (size_t)sz - done);
        if (n <= 0) {
            free(buf);
            close(fd);
            return -1;
        }
        done += (size_t)n;
    }

    close(fd);
    *out_buf = buf;
    *out_size = (size_t)sz;
    return 0;
}

static int copy_file_mode(const char *src, const char *dst, mode_t mode, uint8_t *buf)
{
    int in_fd = open(src, O_RDONLY);
    if (in_fd < 0)
        return -1;

    int out_fd = open(dst, O_WRONLY | O_CREAT | O_TRUNC, mode & 07777);
    if (out_fd < 0) {
        close(in_fd);
        return -1;
    }

    int rc = 0;
    for (;;) {
        ssize_t r = read(in_fd, buf, COPY_BUF_SIZE);
        if (r < 0) {
            rc = -1;
            break;
        }
        if (r == 0)
            break;
        size_t off = 0;
        while (off < (size_t)r) {
            ssize_t w = write(out_fd, buf + off, (size_t)r - off);
            if (w <= 0) {
                rc = -1;
                break;
            }
            off += (size_t)w;
        }
        if (rc != 0)
            break;
    }

    close(out_fd);
    close(in_fd);
    if (rc == 0)
        (void)chmod(dst, mode & 07777);
    return rc;
}

static int write_text_file(const char *path, const char *text)
{
    int fd = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd < 0)
        return -1;
    size_t len = strlen(text);
    size_t off = 0;
    while (off < len) {
        ssize_t w = write(fd, text + off, len - off);
        if (w <= 0) {
            close(fd);
            return -1;
        }
        off += (size_t)w;
    }
    close(fd);
    return 0;
}

static int ensure_dir(const char *path, mode_t mode)
{
    if (mkdir(path, mode) == 0)
        return 0;
    if (errno == EEXIST)
        return 0;
    struct stat st;
    if (stat(path, &st) == 0 && S_ISDIR(st.st_mode))
        return 0;
    return -1;
}

static int should_skip_root_entry(const char *name)
{
    static const char *const skip[] = {
        "dev", "proc", "sys", "tmp", "mnt", "boot", "disk", "pkg",
    };
    for (size_t i = 0; i < sizeof(skip) / sizeof(skip[0]); i++) {
        if (strcmp(name, skip[i]) == 0)
            return 1;
    }
    return 0;
}

static int copy_tree_recursive(const char *src_dir, const char *dst_dir,
                               int is_root_level, uint8_t *buf)
{
    DIR *d = opendir(src_dir);
    if (!d)
        return -1;

    struct dirent *de;
    while ((de = readdir(d)) != NULL) {
        if (strcmp(de->d_name, ".") == 0 || strcmp(de->d_name, "..") == 0)
            continue;
        if (is_root_level && should_skip_root_entry(de->d_name))
            continue;

        char src_path[512];
        char dst_path[512];
        if (strcmp(src_dir, "/") == 0)
            snprintf(src_path, sizeof(src_path), "/%s", de->d_name);
        else
            snprintf(src_path, sizeof(src_path), "%s/%s", src_dir, de->d_name);
        snprintf(dst_path, sizeof(dst_path), "%s/%s", dst_dir, de->d_name);

        /* Skip /var/run contents because /var/run is a runtime tmpfs mount. */
        if (strcmp(src_path, "/var/run") == 0) {
            if (ensure_dir(dst_path, 0755) != 0) {
                closedir(d);
                return -1;
            }
            continue;
        }

        struct stat st;
        if (lstat(src_path, &st) != 0) {
            fprintf(stderr, "installer: lstat failed for %s\n", src_path);
            closedir(d);
            return -1;
        }

        if (S_ISLNK(st.st_mode)) {
            char target[256];
            ssize_t tlen = readlink(src_path, target, sizeof(target) - 1);
            if (tlen < 0) {
                fprintf(stderr, "installer: readlink failed for %s\n", src_path);
                closedir(d);
                return -1;
            }
            target[tlen] = '\0';
            if (symlink(target, dst_path) != 0 && errno != EEXIST) {
                fprintf(stderr, "installer: symlink failed for %s -> %s\n", dst_path, target);
                closedir(d);
                return -1;
            }
        } else if (S_ISDIR(st.st_mode)) {
            mode_t dmode = (st.st_mode & 07777) ? (st.st_mode & 07777) : 0755;
            if (ensure_dir(dst_path, dmode) != 0) {
                fprintf(stderr, "installer: mkdir failed for %s\n", dst_path);
                closedir(d);
                return -1;
            }
            if (copy_tree_recursive(src_path, dst_path, 0, buf) != 0) {
                closedir(d);
                return -1;
            }
            (void)chmod(dst_path, dmode);
        } else if (S_ISREG(st.st_mode)) {
            mode_t fmode = (st.st_mode & 07777) ? (st.st_mode & 07777) : 0644;
            if (copy_file_mode(src_path, dst_path, fmode, buf) != 0) {
                fprintf(stderr, "installer: copy failed for %s -> %s\n", src_path, dst_path);
                closedir(d);
                return -1;
            }
        }
    }

    closedir(d);
    return 0;
}

static void normalize_dev_path(const char *arg, char *out, size_t cap)
{
    if (!arg || !out || cap == 0)
        return;
    if (strncmp(arg, "/dev/", 5) == 0 || strchr(arg, '/') != NULL) {
        strncpy(out, arg, cap - 1);
        out[cap - 1] = '\0';
    } else {
        snprintf(out, cap, "/dev/%s", arg);
    }
}

static void make_part_path(const char *disk_path, int part_num, char *out, size_t cap)
{
    const char *base = strrchr(disk_path, '/');
    base = base ? base + 1 : disk_path;
    if (strncmp(base, "nvme", 4) == 0)
        snprintf(out, cap, "%sp%d", disk_path, part_num);
    else
        snprintf(out, cap, "%s%d", disk_path, part_num);
}

static void unmount_stale_disk_mounts(const char *disk_path)
{
    const char *base = strrchr(disk_path, '/');
    base = base ? base + 1 : disk_path;
    char mnt_path[160];
    for (int p = 1; p <= 8; p++) {
        if (strncmp(base, "nvme", 4) == 0)
            snprintf(mnt_path, sizeof(mnt_path), "/mnt/%sp%d", base, p);
        else
            snprintf(mnt_path, sizeof(mnt_path), "/mnt/%s%d", base, p);
        (void)umount2(mnt_path, MNT_DETACH);
    }
}

static int wait_for_partition(const char *part_path)
{
    struct stat st;
    if (stat(part_path, &st) == 0)
        return 0;
    int fd = open(part_path, O_RDONLY);
    if (fd >= 0) {
        close(fd);
        return 0;
    }
    return -1;
}

static int install_limine_bios_gpt(const char *disk_path, const char *bootblob_path,
                                   uint64_t bios_part_lba, uint64_t bios_part_sectors)
{
    uint8_t *blob = NULL;
    size_t blob_sz = 0;
    if (read_entire_file(bootblob_path, &blob, &blob_sz) != 0) {
        fprintf(stderr, "installer: failed to read %s\n", bootblob_path);
        return -1;
    }

    if (blob_sz < 1024u) {
        free(blob);
        fprintf(stderr, "installer: invalid bootblob size\n");
        return -1;
    }

    uint16_t stage2_size_a = 0;
    uint16_t stage2_size_b = 0;
    memcpy(&stage2_size_a, blob + 0x1a4, sizeof(uint16_t));
    memcpy(&stage2_size_b, blob + 0x1a6, sizeof(uint16_t));

    if (stage2_size_a == 0 || (stage2_size_a % SECTOR_SIZE) != 0 ||
        (stage2_size_b % SECTOR_SIZE) != 0 ||
        512u + (size_t)stage2_size_a + (size_t)stage2_size_b > blob_sz) {
        free(blob);
        fprintf(stderr, "installer: invalid stage2 sizes in bootblob.bin\n");
        return -1;
    }

    uint32_t stage2_secs_a = stage2_size_a / SECTOR_SIZE;
    uint32_t stage2_secs_b = stage2_size_b / SECTOR_SIZE;
    if ((uint64_t)stage2_secs_a + (uint64_t)stage2_secs_b > bios_part_sectors) {
        free(blob);
        fprintf(stderr, "installer: BIOS boot partition too small for stage2\n");
        return -1;
    }

    device_handle_t dev;
    if (device_open(disk_path, O_RDWR, &dev) != 0) {
        free(blob);
        fprintf(stderr, "installer: failed to reopen %s for Limine BIOS stages\n", disk_path);
        return -1;
    }

    uint64_t stage2_loc_a = bios_part_lba * SECTOR_SIZE;
    uint64_t stage2_loc_b = stage2_loc_a + (uint64_t)stage2_size_a;

    if (device_write_sectors(&dev, bios_part_lba, stage2_secs_a, blob + 512) != 0) {
        free(blob);
        device_close(&dev);
        fprintf(stderr, "installer: failed writing stage2 half A\n");
        return -1;
    }
    if (stage2_secs_b > 0 &&
        device_write_sectors(&dev, bios_part_lba + stage2_secs_a,
                             stage2_secs_b, blob + 512 + stage2_size_a) != 0) {
        free(blob);
        device_close(&dev);
        fprintf(stderr, "installer: failed writing stage2 half B\n");
        return -1;
    }

    uint8_t orig_mbr[SECTOR_SIZE];
    uint8_t sec0[SECTOR_SIZE];
    if (device_read_sectors(&dev, 0, 1, orig_mbr) != 0) {
        free(blob);
        device_close(&dev);
        return -1;
    }

    memcpy(sec0, blob, SECTOR_SIZE);
    memcpy(sec0 + 218, orig_mbr + 218, 6);
    memcpy(sec0 + 440, orig_mbr + 440, 70);
    memcpy(sec0 + 0x1a4, &stage2_size_a, sizeof(uint16_t));
    memcpy(sec0 + 0x1a6, &stage2_size_b, sizeof(uint16_t));
    memcpy(sec0 + 0x1a8, &stage2_loc_a, sizeof(uint64_t));
    memcpy(sec0 + 0x1b0, &stage2_loc_b, sizeof(uint64_t));
    sec0[510] = 0x55;
    sec0[511] = 0xAA;

    int rc = device_write_sectors(&dev, 0, 1, sec0);
    free(blob);
    device_sync(&dev);
    device_close(&dev);
    return rc;
}

static int run_tui_select_disk(char *out_path, size_t cap)
{
    char found[8][64];
    uint64_t sizes[8];
    int count = 0;

    printf("Available target disks:\n");
    for (size_t i = 0; i < sizeof(g_candidate_devices) / sizeof(g_candidate_devices[0]); i++) {
        device_handle_t d;
        if (device_open(g_candidate_devices[i], O_RDONLY, &d) == 0) {
            if (d.sector_count >= MIN_INSTALL_SECTORS && count < 8) {
                char sz[32];
                format_size(device_get_size(&d), sz, sizeof(sz));
                printf("  [%d] %-14s (%s, %llu sectors)\n",
                       count + 1, g_candidate_devices[i], sz,
                       (unsigned long long)d.sector_count);
                strncpy(found[count], g_candidate_devices[i], sizeof(found[count]) - 1);
                found[count][sizeof(found[count]) - 1] = '\0';
                sizes[count] = d.sector_count;
                count++;
            }
            device_close(&d);
        }
    }

    if (count == 0) {
        fprintf(stderr, "installer: no suitable disk (>= 96 MiB) found\n");
        return -1;
    }
    (void)sizes;

    printf("\nSelect disk [1-%d] (default 1): ", count);
    fflush(stdout);
    char line[64];
    memset(line, 0, sizeof(line));
    ssize_t n = read(STDIN_FILENO, line, sizeof(line) - 1);
    if (n <= 0 || line[0] == '\n' || line[0] == '\r') {
        strncpy(out_path, found[0], cap - 1);
        out_path[cap - 1] = '\0';
        return 0;
    }

    char *nl = strpbrk(line, "\r\n");
    if (nl)
        *nl = '\0';
    if (isdigit((unsigned char)line[0])) {
        int idx = atoi(line) - 1;
        if (idx >= 0 && idx < count) {
            strncpy(out_path, found[idx], cap - 1);
            out_path[cap - 1] = '\0';
            return 0;
        }
    }
    normalize_dev_path(line, out_path, cap);
    return 0;
}

static void print_usage(const char *prog)
{
    fprintf(stderr,
            "Usage: %s [--auto /dev/<disk>] [--yes] [--no-reboot] [<disk> [ERASE]]\n",
            prog);
}

int main(int argc, char *argv[])
{
    const char *raw_disk = NULL;
    int auto_mode = 0;
    int yes_mode = 0;
    int no_reboot = 0;

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--auto") == 0) {
            if (i + 1 >= argc) {
                print_usage(argv[0]);
                return 1;
            }
            auto_mode = 1;
            yes_mode = 1;
            no_reboot = 1;
            raw_disk = argv[++i];
        } else if (strcmp(argv[i], "--yes") == 0 || strcmp(argv[i], "-y") == 0) {
            yes_mode = 1;
        } else if (strcmp(argv[i], "--no-reboot") == 0) {
            no_reboot = 1;
        } else if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0) {
            print_usage(argv[0]);
            return 0;
        } else if (token_is_erase(argv[i])) {
            yes_mode = 1;
        } else if (argv[i][0] == '-') {
            fprintf(stderr, "installer: unknown option %s\n", argv[i]);
            print_usage(argv[0]);
            return 1;
        } else if (!raw_disk) {
            raw_disk = argv[i];
        }
    }

    (void)auto_mode;
    (void)no_reboot;

    printf("====================================================\n");
    printf("   Project Tsukasa - Unified BIOS+UEFI Installer    \n");
    printf("====================================================\n\n");

    char disk_path[128];
    if (raw_disk) {
        normalize_dev_path(raw_disk, disk_path, sizeof(disk_path));
    } else {
        if (run_tui_select_disk(disk_path, sizeof(disk_path)) != 0)
            return 1;
    }

    char bootx64_src[128], limine_sys_src[128], bootblob_src[128], kernel_src[128];
    if (find_existing_file(g_bootx64_paths, sizeof(g_bootx64_paths) / sizeof(g_bootx64_paths[0]),
                           bootx64_src, sizeof(bootx64_src)) != 0 ||
        find_existing_file(g_limine_sys_paths, sizeof(g_limine_sys_paths) / sizeof(g_limine_sys_paths[0]),
                           limine_sys_src, sizeof(limine_sys_src)) != 0 ||
        find_existing_file(g_bootblob_paths, sizeof(g_bootblob_paths) / sizeof(g_bootblob_paths[0]),
                           bootblob_src, sizeof(bootblob_src)) != 0 ||
        find_existing_file(g_kernel_paths, sizeof(g_kernel_paths) / sizeof(g_kernel_paths[0]),
                           kernel_src, sizeof(kernel_src)) != 0) {
        fprintf(stderr, "installer: missing required boot payload (BOOTX64.EFI, limine-bios.sys, bootblob.bin, or kernel)\n");
        return 1;
    }

    device_handle_t disk_dev;
    if (device_open(disk_path, O_RDWR, &disk_dev) != 0) {
        fprintf(stderr, "installer: cannot open target disk %s\n", disk_path);
        return 1;
    }

    char disk_sz[32];
    format_size(device_get_size(&disk_dev), disk_sz, sizeof(disk_sz));
    log_msg("[installer] target=%s (%s, %llu sectors)\n",
            disk_path, disk_sz, (unsigned long long)disk_dev.sector_count);

    if (disk_dev.sector_count < MIN_INSTALL_SECTORS) {
        fprintf(stderr, "installer: target disk smaller than 96 MiB minimum\n");
        device_close(&disk_dev);
        return 1;
    }

    if (!yes_mode) {
        printf("\nWARNING: All existing data on %s will be ERASED!\n", disk_path);
        printf("Layout: 1 MiB BIOS Boot + 64 MiB FAT32 ESP + ext4 Root\n");
        printf("Proceed with installation? (yes/N): ");
        fflush(stdout);
        char ans[32];
        memset(ans, 0, sizeof(ans));
        ssize_t n = read(STDIN_FILENO, ans, sizeof(ans) - 1);
        if (n <= 0 || (ans[0] != 'y' && ans[0] != 'Y')) {
            printf("Installation aborted.\n");
            device_close(&disk_dev);
            return 0;
        }
    }

    /* 1. Partition target disk as GPT. */
    log_msg("[installer] [1/7] writing GPT partition table...\n");
    unmount_stale_disk_mounts(disk_path);

    uint8_t disk_guid[16];
    gpt_gen_uuid(disk_guid);

    gpt_entry_t entries[GPT_MAX_ENTRIES];
    memset(entries, 0, sizeof(entries));

    uint64_t part1_lba = 2048ULL;
    uint64_t part1_secs = BIOS_BOOT_SECTORS;
    uint64_t part2_lba = part1_lba + part1_secs;
    uint64_t part2_secs = ESP_SECTORS;
    uint64_t part3_lba = part2_lba + part2_secs;
    uint64_t last_usable = disk_dev.sector_count - 1ULL - GPT_ARRAY_SECTORS - 1ULL;

    memcpy(entries[0].type_guid, GPT_GUID_BIOS_BOOT, 16);
    gpt_gen_uuid(entries[0].part_guid);
    entries[0].start_lba = part1_lba;
    entries[0].end_lba = part1_lba + part1_secs - 1ULL;
    gpt_set_name_ascii(&entries[0], "BIOS Boot");

    memcpy(entries[1].type_guid, GPT_GUID_ESP, 16);
    gpt_gen_uuid(entries[1].part_guid);
    entries[1].start_lba = part2_lba;
    entries[1].end_lba = part2_lba + part2_secs - 1ULL;
    gpt_set_name_ascii(&entries[1], "EFI System Partition");

    memcpy(entries[2].type_guid, GPT_GUID_LINUX_FS, 16);
    gpt_gen_uuid(entries[2].part_guid);
    entries[2].start_lba = part3_lba;
    entries[2].end_lba = last_usable;
    gpt_set_name_ascii(&entries[2], "Tsukasa Root");

    char root_partuuid[37];
    gpt_format_uuid(entries[2].part_guid, root_partuuid);

    if (gpt_write_table(&disk_dev, disk_guid, entries, 3u) != 0) {
        fprintf(stderr, "installer: failed to write GPT\n");
        device_close(&disk_dev);
        return 1;
    }
    if (device_reread_partitions(&disk_dev) != 0) {
        fprintf(stderr, "installer: BLKRRPART ioctl failed\n");
        device_close(&disk_dev);
        return 1;
    }
    device_close(&disk_dev);

    char part1_path[144], part2_path[144], part3_path[144];
    make_part_path(disk_path, 1, part1_path, sizeof(part1_path));
    make_part_path(disk_path, 2, part2_path, sizeof(part2_path));
    make_part_path(disk_path, 3, part3_path, sizeof(part3_path));

    if (wait_for_partition(part1_path) != 0 ||
        wait_for_partition(part2_path) != 0 ||
        wait_for_partition(part3_path) != 0) {
        fprintf(stderr, "installer: partitions (%s, %s, %s) did not appear after BLKRRPART\n",
                part1_path, part2_path, part3_path);
        return 1;
    }

    /* 2. Format ESP (FAT32) and Root (ext4). */
    log_msg("[installer] [2/7] formatting ESP (%s FAT32) and root (%s ext4)...\n",
            part2_path, part3_path);
    device_handle_t esp_dev, root_dev;
    if (device_open(part2_path, O_RDWR, &esp_dev) != 0) {
        fprintf(stderr, "installer: cannot open %s\n", part2_path);
        return 1;
    }
    int fmt_rc = fat32_format_device(&esp_dev, "TSUKASA_ESP", (uint32_t)part2_lba, 0, 0);
    device_close(&esp_dev);
    if (fmt_rc != 0) {
        fprintf(stderr, "installer: FAT32 format failed on %s\n", part2_path);
        return 1;
    }

    if (device_open(part3_path, O_RDWR, &root_dev) != 0) {
        fprintf(stderr, "installer: cannot open %s\n", part3_path);
        return 1;
    }
    fmt_rc = ext4_format_device(&root_dev, "TSUKASA_ROOT", 0);
    device_close(&root_dev);
    if (fmt_rc != 0) {
        fprintf(stderr, "installer: ext4 format failed on %s\n", part3_path);
        return 1;
    }

    /* 3. Mount ext4 root at /mnt and FAT32 ESP at /mnt/boot. */
    log_msg("[installer] [3/7] mounting %s -> /mnt and %s -> /mnt/boot...\n",
            part3_path, part2_path);
    if (mount(part3_path, "/mnt", "ext4", 0, NULL) != 0) {
        fprintf(stderr, "installer: failed to mount %s at /mnt\n", part3_path);
        return 1;
    }
    if (ensure_dir("/mnt/boot", 0755) != 0 ||
        mount(part2_path, "/mnt/boot", "fat32", 0, NULL) != 0) {
        fprintf(stderr, "installer: failed to mount %s at /mnt/boot\n", part2_path);
        (void)umount2("/mnt", MNT_DETACH);
        return 1;
    }

    uint8_t *copy_buf = (uint8_t *)malloc(COPY_BUF_SIZE);
    if (!copy_buf) {
        (void)umount2("/mnt/boot", MNT_DETACH);
        (void)umount2("/mnt", MNT_DETACH);
        return 1;
    }

    /* 4. Copy live rootfs to /mnt. */
    log_msg("[installer] [4/7] copying live root filesystem to /mnt...\n");
    if (copy_tree_recursive("/", "/mnt", 1, copy_buf) != 0 ||
        ensure_dir("/mnt/dev", 0755) != 0 ||
        ensure_dir("/mnt/proc", 0555) != 0 ||
        ensure_dir("/mnt/sys", 0555) != 0 ||
        ensure_dir("/mnt/tmp", 01777) != 0 ||
        ensure_dir("/mnt/var", 0755) != 0 ||
        ensure_dir("/mnt/var/run", 0755) != 0 ||
        ensure_dir("/mnt/mnt", 0755) != 0) {
        free(copy_buf);
        (void)umount2("/mnt/boot", MNT_DETACH);
        (void)umount2("/mnt", MNT_DETACH);
        return 1;
    }

    /* 5. Install UEFI + BIOS Limine boot files onto ESP (/mnt/boot). */
    log_msg("[installer] [5/7] installing UEFI & BIOS boot payload to /mnt/boot...\n");
    if (ensure_dir("/mnt/boot/EFI", 0755) != 0 ||
        ensure_dir("/mnt/boot/EFI/BOOT", 0755) != 0 ||
        ensure_dir("/mnt/boot/boot", 0755) != 0 ||
        ensure_dir("/mnt/boot/boot/limine", 0755) != 0) {
        free(copy_buf);
        (void)umount2("/mnt/boot", MNT_DETACH);
        (void)umount2("/mnt", MNT_DETACH);
        return 1;
    }

    if (copy_file_mode(bootx64_src, "/mnt/boot/EFI/BOOT/BOOTX64.EFI", 0644, copy_buf) != 0 ||
        copy_file_mode(limine_sys_src, "/mnt/boot/boot/limine/limine-bios.sys", 0644, copy_buf) != 0 ||
        copy_file_mode(limine_sys_src, "/mnt/boot/limine-bios.sys", 0644, copy_buf) != 0 ||
        copy_file_mode(kernel_src, "/mnt/boot/boot/tsukasa_x64.elf", 0644, copy_buf) != 0) {
        free(copy_buf);
        (void)umount2("/mnt/boot", MNT_DETACH);
        (void)umount2("/mnt", MNT_DETACH);
        return 1;
    }
    free(copy_buf);

    char limine_cfg[512];
    snprintf(limine_cfg, sizeof(limine_cfg),
             "timeout: 2\n"
             "verbose: yes\n"
             "\n"
             "/Tsukasa x86_64\n"
             "    protocol: limine\n"
             "    kernel_path: boot():/boot/tsukasa_x64.elf\n"
             "    cmdline: arch=x86_64 single_core=1 root=PARTUUID=%s\n",
             root_partuuid);

    if (write_text_file("/mnt/boot/limine.conf", limine_cfg) != 0 ||
        write_text_file("/mnt/boot/boot/limine/limine.conf", limine_cfg) != 0) {
        (void)umount2("/mnt/boot", MNT_DETACH);
        (void)umount2("/mnt", MNT_DETACH);
        return 1;
    }

    /* 6. Sync and cleanly unmount /mnt/boot and /mnt. */
    log_msg("[installer] [6/7] syncing and unmounting target filesystems...\n");
    sync();
    if (umount2("/mnt/boot", 0) != 0) {
        fprintf(stderr, "installer: failed to unmount /mnt/boot\n");
        (void)umount2("/mnt", MNT_DETACH);
        return 1;
    }
    if (umount2("/mnt", 0) != 0) {
        fprintf(stderr, "installer: failed to unmount /mnt\n");
        return 1;
    }

    /* 7. Install Limine BIOS stage1 + stage2 onto GPT disk (BIOS Boot partition). */
    log_msg("[installer] [7/7] installing Limine BIOS stage1+stage2 to %s (PARTUUID=%s)...\n",
            disk_path, root_partuuid);
    if (install_limine_bios_gpt(disk_path, bootblob_src, part1_lba, part1_secs) != 0) {
        fprintf(stderr, "installer: failed to install Limine BIOS stages\n");
        return 1;
    }

    log_msg("[installer] INSTALL COMPLETE\n");
    log_msg("Installation complete. Tsukasa installed to %s (root=PARTUUID=%s).\n",
            disk_path, root_partuuid);
    return 0;
}
