/*
 * Project Tsukasa — disk installer core (guide 13)
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

#include "installer.h"

#include "../drv/diskmgr.h"
#include "../fs/bootfs.h"
#include "../fs/fat32.h"
#include "../fs/mkfs_fat32.h"
#include "../include/kprintf.h"
#include "../include/kutils.h"
#include "../mm/heap.h"

#include <stddef.h>
#include <stdint.h>

#define BOOTBLOB_SECTORS 2048u   /* sectors 0..2047 captured at build time */
#define PART_START       2048u

static const char *const g_source_modules[] = {
    "/bootblob.bin",
    "/tsukasa_x64.elf",
    "/initrd.img",
    "/limine-bios.sys",
};

static void report(installer_progress_fn cb, const char *stage, int pct)
{
    if (cb)
        cb(stage, pct);
    kprintf("[installer] %3d%% %s\n", pct, stage);
}

static void seterr(char *err, int errlen, const char *msg)
{
    int i = 0;
    if (!err || errlen <= 0)
        return;
    while (msg[i] && i < errlen - 1) {
        err[i] = msg[i];
        i++;
    }
    err[i] = '\0';
}

int installer_preflight(block_dev_t *bd, char *err, int errlen)
{
    vfs_stat_t st;

    if (!bd) {
        seterr(err, errlen, "no target device");
        return -1;
    }
    if (bd->is_partition) {
        seterr(err, errlen, "target is a partition, not a whole disk");
        return -1;
    }
    if (!bd->write) {
        seterr(err, errlen, "target is not writable");
        return -1;
    }
    if (bd->sector_count < INSTALLER_MIN_SECTORS) {
        seterr(err, errlen, "target smaller than 64 MiB");
        return -1;
    }
    for (unsigned i = 0;
         i < sizeof(g_source_modules) / sizeof(g_source_modules[0]); i++) {
        if (bootfs_stat(g_source_modules[i], &st) != 0 || st.size == 0) {
            seterr(err, errlen,
                   "live medium lacks an install source module "
                   "(bootblob/kernel/initrd/limine-bios.sys)");
            return -1;
        }
    }
    return 0;
}

/* Copy one bootfs module onto the target volume as /<target_name>. */
static int install_file(fat32_volume_t *vol, const char *module_path,
                        const char *target_name)
{
    const void *data = NULL;
    size_t size = 0;
    int owns = 0;
    char path[80];
    int i, rc;

    if (bootfs_read_file(module_path, &data, &size, &owns) != 0 || !data)
        return -1;

    path[0] = '/';
    for (i = 0; target_name[i] && i < (int)sizeof(path) - 2; i++)
        path[i + 1] = target_name[i];
    path[i + 1] = '\0';

    rc = fat32_vol_create_file(vol, path);
    if (rc == 0)
        rc = fat32_vol_write_file(vol, path, data, size);
    if (owns)
        kfree((void *)data);
    return rc;
}

static int install_generated(fat32_volume_t *vol, const char *path,
                             const char *text)
{
    int len = k_strlen((char *)text);
    if (fat32_vol_create_file(vol, path) != 0)
        return -1;
    return fat32_vol_write_file(vol, path, text, (size_t)len);
}

int installer_run(block_dev_t *bd, installer_progress_fn cb)
{
    char err[96];
    const void *blob = NULL;
    size_t blob_size = 0;
    int owns = 0;
    uint32_t part_count;
    block_dev_t *part = NULL;
    fat32_volume_t *vol;
    char part_name[BLOCKDEV_NAME_MAX];
    int n;

    if (installer_preflight(bd, err, sizeof(err)) != 0) {
        kprintf("[installer] preflight failed: %s\n", err);
        return -1;
    }

    report(cb, "writing Limine BIOS boot stages", 10);
    if (bootfs_read_file("/bootblob.bin", &blob, &blob_size, &owns) != 0 ||
        !blob || blob_size == 0 ||
        blob_size > BOOTBLOB_SECTORS * 512u) {
        kprintf("[installer] bootblob unavailable or oversized\n");
        return -1;
    }
    {
        uint8_t *bounce = (uint8_t *)kmalloc(512);
        size_t off = 0;
        uint64_t lba = 0;
        int rc = 0;
        if (!bounce) {
            if (owns)
                kfree((void *)blob);
            return -1;
        }
        while (off < blob_size && rc == 0) {
            size_t chunk = blob_size - off;
            if (chunk >= 512) {
                rc = bd->write(bd, lba, 1, (const uint8_t *)blob + off);
                off += 512;
            } else {
                k_memset(bounce, 0, 512);
                k_memcpy(bounce, (const uint8_t *)blob + off, chunk);
                rc = bd->write(bd, lba, 1, bounce);
                off = blob_size;
            }
            lba++;
        }
        kfree(bounce);
        if (owns)
            kfree((void *)blob);
        if (rc != 0) {
            kprintf("[installer] boot blob write failed\n");
            return -1;
        }
    }

    report(cb, "writing MBR partition table", 25);
    {
        uint64_t avail = bd->sector_count - PART_START;
        part_count = (avail > 0xFFFFFFFFull) ? 0xFFFFFFFFu : (uint32_t)avail;
    }
    if (diskmgr_write_mbr(bd, PART_START, part_count) != 0)
        return -1;

    report(cb, "rescanning target disk", 35);
    diskmgr_rescan(bd);
    n = 0;
    while (bd->name[n] && n < BLOCKDEV_NAME_MAX - 2) {
        part_name[n] = bd->name[n];
        n++;
    }
    part_name[n] = '1';
    part_name[n + 1] = '\0';
    part = blockdev_find(part_name);
    if (!part || !part->is_partition || part->parent != bd) {
        kprintf("[installer] partition %s not found after rescan\n",
                part_name);
        return -1;
    }

    report(cb, "formatting target partition (FAT32)", 45);
    if (mkfs_fat32_format(part, "TSUKASA") != 0)
        return -1;
    vol = fat32_vol_mount(part);
    if (!vol) {
        kprintf("[installer] mkfs succeeded but volume mount failed\n");
        return -1;
    }

    report(cb, "copying limine-bios.sys", 55);
    if (install_file(vol, "/limine-bios.sys", "limine-bios.sys") != 0)
        return -1;
    report(cb, "copying kernel (tsukasa_x64.elf)", 65);
    if (install_file(vol, "/tsukasa_x64.elf", "tsukasa_x64.elf") != 0)
        return -1;
    report(cb, "copying initrd.img", 80);
    if (install_file(vol, "/initrd.img", "initrd.img") != 0)
        return -1;

    /* 6 — boot config (mirrors the live limine.conf semantics; the installed system boots exactly like the ISO,... */
    report(cb, "writing limine.conf", 90);
    if (install_generated(vol, "/limine.conf",
                          "timeout: 2\n"
                          "verbose: yes\n"
                          "\n"
                          "/Tsukasa x86_64 (installed)\n"
                          "    protocol: limine\n"
                          "    kernel_path: boot():/tsukasa_x64.elf\n"
                          "    cmdline: arch=x86_64 single_core=1\n"
                          "    module_path: boot():/initrd.img\n") != 0)
        return -1;

    report(cb, "syncing target disk", 95);
    if (bd->sync && bd->sync(bd) != 0)
        kprintf("[installer] WARN: device sync failed\n");

    report(cb, "installation complete", 100);
    kprintf("[installer] %s: Tsukasa installed (boot partition %s)\n",
            bd->name, part->name);
    return 0;
}
