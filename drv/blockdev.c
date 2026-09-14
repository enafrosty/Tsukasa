/*
 * Project Tsukasa — block-device registry + guide 11 boot selftests
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

#include "blockdev.h"
#include "ata.h"

#include "../include/kprintf.h"
#include "../include/kutils.h"
#include "../include/spinlock.h"
#include "../mm/heap.h"
#include "../fs/devfs.h"
#include "../fs/vfs.h"

#include <stddef.h>
#include <stdint.h>

static int blockdev_devfs_open(void *priv, int flags)
{
    (void)priv;
    (void)flags;
    return 0;
}

static void blockdev_devfs_close(void *priv)
{
    (void)priv;
}

static size_t blockdev_devfs_read(void *priv, size_t pos, void *buf, size_t count, int flags)
{
    (void)flags;
    block_dev_t *bd = (block_dev_t *)priv;
    if (!bd || !bd->read || !buf || count == 0)
        return 0;

    uint32_t ss = bd->sector_size ? bd->sector_size : 512;
    uint64_t total_bytes = bd->sector_count * ss;
    if (pos >= total_bytes)
        return 0;
    if (pos + count > total_bytes)
        count = total_bytes - pos;

    uint64_t start_lba = pos / ss;
    size_t offset_in_sec = pos % ss;
    size_t bytes_read = 0;
    uint8_t bounce[512];

    while (bytes_read < count) {
        if (offset_in_sec == 0 && (count - bytes_read) >= ss) {
            uint32_t sectors = (uint32_t)((count - bytes_read) / ss);
            if (bd->read(bd, start_lba, sectors, (uint8_t *)buf + bytes_read) != 0)
                break;
            bytes_read += (size_t)sectors * ss;
            start_lba += sectors;
        } else {
            if (bd->read(bd, start_lba, 1, bounce) != 0)
                break;
            size_t chunk = ss - offset_in_sec;
            if (chunk > count - bytes_read)
                chunk = count - bytes_read;
            for (size_t i = 0; i < chunk; i++)
                ((uint8_t *)buf)[bytes_read + i] = bounce[offset_in_sec + i];
            bytes_read += chunk;
            offset_in_sec = 0;
            start_lba++;
        }
    }
    return bytes_read;
}

static size_t blockdev_devfs_write(void *priv, size_t pos, const void *buf, size_t count)
{
    block_dev_t *bd = (block_dev_t *)priv;
    if (!bd || !bd->write || !buf || count == 0)
        return 0;

    uint32_t ss = bd->sector_size ? bd->sector_size : 512;
    uint64_t total_bytes = bd->sector_count * ss;
    if (pos >= total_bytes)
        return 0;
    if (pos + count > total_bytes)
        count = total_bytes - pos;

    uint64_t start_lba = pos / ss;
    size_t offset_in_sec = pos % ss;
    size_t bytes_written = 0;
    uint8_t bounce[512];

    while (bytes_written < count) {
        if (offset_in_sec == 0 && (count - bytes_written) >= ss) {
            uint32_t sectors = (uint32_t)((count - bytes_written) / ss);
            if (bd->write(bd, start_lba, sectors, (const uint8_t *)buf + bytes_written) != 0)
                break;
            bytes_written += (size_t)sectors * ss;
            start_lba += sectors;
        } else {
            if (bd->read && bd->read(bd, start_lba, 1, bounce) != 0)
                k_memset(bounce, 0, ss);
            size_t chunk = ss - offset_in_sec;
            if (chunk > count - bytes_written)
                chunk = count - bytes_written;
            for (size_t i = 0; i < chunk; i++)
                bounce[offset_in_sec + i] = ((const uint8_t *)buf)[bytes_written + i];
            if (bd->write(bd, start_lba, 1, bounce) != 0)
                break;
            bytes_written += chunk;
            offset_in_sec = 0;
            start_lba++;
        }
    }
    if (bd->sync)
        bd->sync(bd);
    return bytes_written;
}

static size_t blockdev_devfs_size(void *priv)
{
    block_dev_t *bd = (block_dev_t *)priv;
    if (!bd)
        return 0;
    uint32_t ss = bd->sector_size ? bd->sector_size : 512;
    return (size_t)(bd->sector_count * ss);
}

static const devfs_ops_t g_blockdev_devfs_ops = {
    .open = blockdev_devfs_open,
    .close = blockdev_devfs_close,
    .read = blockdev_devfs_read,
    .write = blockdev_devfs_write,
    .ioctl = NULL,
    .mmap = NULL,
    .munmap = NULL,
    .poll = NULL,
    .size = blockdev_devfs_size,
};

static block_dev_t *g_devs[BLOCKDEV_MAX_DEVICES];
static int g_dev_count;
static spinlock_t g_blockdev_lock = SPINLOCK_INIT;

static block_dev_t g_part_pool[BLOCKDEV_MAX_PARTS];
static int g_part_count;
static int g_next_sd_index;   /* sda, sdb, ... (SATA) */
static int g_next_hd_index;   /* hda, hdb, ... (IDE)  */

static int bd_streq(const char *a, const char *b)
{
    int i = 0;
    if (!a || !b)
        return 0;
    while (a[i] && b[i] && a[i] == b[i])
        i++;
    return a[i] == b[i];
}

static void blockdev_assign_name(block_dev_t *bd)
{
    int idx;
    char c1, c2;

    if (bd->type == BLOCKDEV_TYPE_IDE) {
        c1 = 'h';
        c2 = 'd';
        idx = g_next_hd_index++;
    } else {
        c1 = 's';
        c2 = 'd';
        idx = g_next_sd_index++;
    }
    bd->name[0] = c1;
    bd->name[1] = c2;
    bd->name[2] = (char)('a' + (idx % 26));
    bd->name[3] = '\0';
}

int blockdev_register(block_dev_t *bd)
{
    unsigned long flags;
    int i;

    if (!bd || !bd->read || bd->sector_size == 0)
        return -1;

    flags = spin_lock_irqsave(&g_blockdev_lock);
    if (!bd->name[0])
        blockdev_assign_name(bd);
    for (i = 0; i < g_dev_count; i++) {
        if (bd_streq(g_devs[i]->name, bd->name)) {
            spin_unlock_irqrestore(&g_blockdev_lock, flags);
            kprintf("[blockdev] duplicate name %s rejected\n", bd->name);
            return -1;
        }
    }
    if (g_dev_count >= BLOCKDEV_MAX_DEVICES) {
        spin_unlock_irqrestore(&g_blockdev_lock, flags);
        kprintf("[blockdev] registry full, %s rejected\n", bd->name);
        return -1;
    }
    g_devs[g_dev_count++] = bd;
    spin_unlock_irqrestore(&g_blockdev_lock, flags);

    kprintf("[blockdev] registered %s: sectors=0x%08x%08x (%u MiB)\n",
            bd->name,
            (uint32_t)(bd->sector_count >> 32),
            (uint32_t)(bd->sector_count & 0xFFFFFFFFu),
            (uint32_t)(bd->sector_count / 2048u));
    devfs_register_device(bd->name, VFS_TYPE_BLOCK, &g_blockdev_devfs_ops, bd);
    return 0;
}

int blockdev_count(void)
{
    return g_dev_count;
}

block_dev_t *blockdev_at(int index)
{
    if (index < 0 || index >= g_dev_count)
        return NULL;
    return g_devs[index];
}

block_dev_t *blockdev_find(const char *name)
{
    for (int i = 0; i < g_dev_count; i++) {
        if (bd_streq(g_devs[i]->name, name))
            return g_devs[i];
    }
    return NULL;
}

block_dev_t *blockdev_primary(void)
{
    return (g_dev_count > 0) ? g_devs[0] : NULL;
}

block_dev_t *blockdev_find_type(block_dev_type_t type)
{
    for (int i = 0; i < g_dev_count; i++) {
        if (!g_devs[i]->is_partition && g_devs[i]->type == type)
            return g_devs[i];
    }
    return NULL;
}

block_dev_t *blockdev_first_raw_fat32(void)
{
    for (int i = 0; i < g_dev_count; i++) {
        if (!g_devs[i]->is_partition && g_devs[i]->is_fat32)
            return g_devs[i];
    }
    return NULL;
}

/* The LBA offset is applied HERE, in one generic wrapper, instead of */

/* ahci_disk_read_sector pattern) — drivers stay partition-ignorant. */

static int blockdev_part_read(block_dev_t *bd, uint64_t lba, uint32_t count,
                              void *buf)
{
    block_dev_t *parent = bd->parent;
    if (!parent || lba + count < lba || lba + count > bd->sector_count)
        return -1;
    return parent->read(parent, bd->lba_offset + lba, count, buf);
}

static int blockdev_part_write(block_dev_t *bd, uint64_t lba, uint32_t count,
                               const void *buf)
{
    block_dev_t *parent = bd->parent;
    if (!parent || !parent->write ||
        lba + count < lba || lba + count > bd->sector_count)
        return -1;
    return parent->write(parent, bd->lba_offset + lba, count, buf);
}

static int blockdev_part_sync(block_dev_t *bd)
{
    block_dev_t *parent = bd->parent;
    if (!parent)
        return -1;
    return parent->sync ? parent->sync(parent) : 0;
}

block_dev_t *blockdev_register_partition(block_dev_t *parent, int part_num,
                                         uint64_t lba_offset,
                                         uint64_t sector_count,
                                         int is_fat32, int is_esp)
{
    block_dev_t *bd;
    int n, i;

    if (!parent || parent->is_partition || part_num < 1 || part_num > 9 ||
        sector_count == 0)
        return NULL;
    if (lba_offset + sector_count < lba_offset ||
        lba_offset + sector_count > parent->sector_count)
        return NULL;
    if (g_part_count >= BLOCKDEV_MAX_PARTS)
        return NULL;

    bd = &g_part_pool[g_part_count];
    k_memset(bd, 0, sizeof(*bd));

    n = 0;
    while (parent->name[n] && n < BLOCKDEV_NAME_MAX - 2) {
        bd->name[n] = parent->name[n];
        n++;
    }
    bd->name[n] = (char)('0' + part_num);
    bd->name[n + 1] = '\0';

    bd->type = parent->type;
    bd->sector_size = parent->sector_size;
    bd->sector_count = sector_count;
    bd->is_partition = 1;
    bd->is_fat32 = is_fat32 ? 1 : 0;
    bd->is_esp = is_esp ? 1 : 0;
    bd->lba_offset = lba_offset;
    bd->parent = parent;
    bd->read = blockdev_part_read;
    bd->write = parent->write ? blockdev_part_write : NULL;
    bd->sync = blockdev_part_sync;
    bd->drv_data = parent->drv_data;

    {
        const char *l = is_esp ? "EFI System Partition"
                               : (is_fat32 ? "FAT32 Partition"
                                           : "Unknown Partition");
        for (i = 0; l[i] && i < BLOCKDEV_LABEL_MAX - 1; i++)
            bd->label[i] = l[i];
        bd->label[i] = '\0';
    }

    if (blockdev_register(bd) != 0)
        return NULL;
    g_part_count++;
    return bd;
}

/* Boot-time, inline, pre-sti (polled disk I/O needs no interrupts — */
/* same placement rationale as mm/slab.c's [guide09] suite). Every test */
/* that needs hardware SKIPS loudly instead of failing when it is */
/* absent, so gates without an AHCI disk stay green. Writes touch ONLY */
/* the AHCI scratch disk, at a high LBA, and the original content is */
/* restored afterward (see BOOT_QEMU.md "AHCI gate"). */

static int bd_memcmp(const void *a, const void *b, uint32_t n)
{
    const uint8_t *x = (const uint8_t *)a;
    const uint8_t *y = (const uint8_t *)b;
    for (uint32_t i = 0; i < n; i++) {
        if (x[i] != y[i])
            return (int)x[i] - (int)y[i];
    }
    return 0;
}

static block_dev_t *bd_find_ahci(void)
{
    return blockdev_find_type(BLOCKDEV_TYPE_SATA);
}

void blockdev_run_selftests(void)
{
    int pass = 0, fail = 0;
    block_dev_t *ahci;

    kprintf("[guide11][test] start\n");

    {
        int ok = 1;
        if (blockdev_register(NULL) != -1) ok = 0;
        if (blockdev_find(NULL) != NULL) ok = 0;
        if (blockdev_find("no-such-dev") != NULL) ok = 0;
        if (blockdev_at(-1) != NULL) ok = 0;
        if (blockdev_at(g_dev_count) != NULL) ok = 0;
        if (g_dev_count > 0) {
            if (blockdev_primary() != g_devs[0]) ok = 0;
            if (blockdev_find(g_devs[0]->name) != g_devs[0]) ok = 0;
            {
                static block_dev_t dup;
                int precount = g_dev_count;
                dup = *g_devs[0];
                if (blockdev_register(&dup) != -1) ok = 0;
                if (g_dev_count != precount) ok = 0;
            }
        } else {
            if (blockdev_primary() != NULL) ok = 0;
        }
        if (ok) {
            kprintf("[guide11] registry API PASS (devices=%d)\n", g_dev_count);
            pass++;
        } else {
            kprintf("[guide11] registry API FAIL\n");
            fail++;
        }
    }

    for (int i = 0; i < g_dev_count; i++) {
        block_dev_t *bd = g_devs[i];
        kprintf("[guide11] dev%d: %s sectors=0x%08x%08x%s\n",
                i, bd->name,
                (uint32_t)(bd->sector_count >> 32),
                (uint32_t)(bd->sector_count & 0xFFFFFFFFu),
                (i == 0) ? " (primary)" : "");
    }

    ahci = bd_find_ahci();
    if (!ahci) {
        kprintf("[guide11] no AHCI device — DMA tests skipped "
                "(expected without an AHCI disk attached)\n");
        kprintf("[guide11][test] done pass=%d fail=%d\n", pass, fail);
        return;
    }

    if (ahci->sector_count > 0) {
        kprintf("[guide11] identify PASS (%s: %u MiB)\n",
                ahci->name, (uint32_t)(ahci->sector_count / 2048u));
        pass++;
    } else {
        kprintf("[guide11] identify FAIL (%s reports 0 sectors)\n", ahci->name);
        fail++;
    }

    if (ata_sector_count() == 0) {
        kprintf("[guide11] PIO+DMA pair not present — cross-check skipped "
                "(expected on q35: no legacy IDE)\n");
    } else {
        static const uint32_t lbas[4] = { 0u, 1u, 5u, 7u };
        uint8_t *pio = (uint8_t *)kmalloc(512);
        uint8_t *dma = (uint8_t *)kmalloc(512);
        int ok = (pio && dma) ? 1 : 0;
        for (int i = 0; ok && i < 4; i++) {
            if (ata_read_sectors(lbas[i], 1, pio) != 0) ok = 0;
            if (ok && ahci->read(ahci, lbas[i], 1, dma) != 0) ok = 0;
            if (ok && bd_memcmp(pio, dma, 512) != 0) {
                kprintf("[guide11] PIO/DMA mismatch at lba=%u\n", lbas[i]);
                ok = 0;
            }
        }
        if (ok) {
            kprintf("[guide11] PIO-vs-DMA memcmp PASS (lba 0,1,5,7)\n");
            pass++;
        } else {
            kprintf("[guide11] PIO-vs-DMA memcmp FAIL\n");
            fail++;
        }
        kfree(pio);
        kfree(dma);
    }

    if (ahci->sector_count < 4096u || !ahci->write) {
        kprintf("[guide11] write round-trip skipped (device too small "
                "or read-only)\n");
    } else {
        uint64_t lba = ahci->sector_count - 64u;
        uint8_t *save = (uint8_t *)kmalloc(3u * 512u);
        uint8_t *arena = (uint8_t *)kmalloc(8192);
        uint8_t *rb = (uint8_t *)kmalloc(3u * 512u);
        int ok = (save && arena && rb) ? 1 : 0;
        uint8_t *src = NULL;

        if (ok) {
            uintptr_t b = ((uintptr_t)arena + 256u + 4095u) & ~(uintptr_t)4095u;
            src = (uint8_t *)(b - 256u);
            for (uint32_t i = 0; i < 3u * 512u; i++)
                src[i] = (uint8_t)(0xA5u ^ (uint8_t)i ^ (uint8_t)(i >> 8));
        }

        if (ok && ahci->read(ahci, lba, 3, save) != 0) ok = 0;
        if (ok && ahci->write(ahci, lba, 3, src) != 0) ok = 0;
        if (ok && ahci->read(ahci, lba, 3, rb) != 0) ok = 0;
        if (ok && bd_memcmp(src, rb, 3u * 512u) != 0) ok = 0;

        if (ok) {
            for (uint32_t i = 0; i < 512u; i++)
                rb[i] = (uint8_t)(0x3Cu ^ (uint8_t)i);
            if (ahci->write(ahci, lba + 1u, 1, rb) != 0) ok = 0;
            if (ok && ahci->read(ahci, lba + 1u, 1, arena) != 0) ok = 0;
            if (ok && bd_memcmp(rb, arena, 512) != 0) ok = 0;
        }

        if (save && ahci->write(ahci, lba, 3, save) != 0) {
            kprintf("[guide11] WARN: scratch restore failed at lba hi=0x%x lo=0x%x\n",
                    (uint32_t)(lba >> 32), (uint32_t)(lba & 0xFFFFFFFFu));
        }

        if (ok) {
            kprintf("[guide11] DMA write round-trip PASS "
                    "(3 sectors, page-straddling source + 1 sector)\n");
            pass++;
        } else {
            kprintf("[guide11] DMA write round-trip FAIL\n");
            fail++;
        }
        kfree(save);
        kfree(arena);
        kfree(rb);
    }

    {
        uint8_t *big = (uint8_t *)kmalloc(16u * 512u);
        uint8_t *one = (uint8_t *)kmalloc(512);
        int ok = (big && one) ? 1 : 0;
        if (ok && ahci->read(ahci, 0, 16, big) != 0) ok = 0;
        for (uint32_t s = 0; ok && s < 16u; s++) {
            if (ahci->read(ahci, s, 1, one) != 0) ok = 0;
            if (ok && bd_memcmp(big + s * 512u, one, 512) != 0) {
                kprintf("[guide11] multi-PRDT mismatch at sector %u\n", s);
                ok = 0;
            }
        }
        if (ok) {
            kprintf("[guide11] multi-PRDT consistency PASS (16x512 vs 1x8192)\n");
            pass++;
        } else {
            kprintf("[guide11] multi-PRDT consistency FAIL\n");
            fail++;
        }
        kfree(big);
        kfree(one);
    }

    if (ahci->sync) {
        if (ahci->sync(ahci) == 0) {
            kprintf("[guide11] flush PASS\n");
            pass++;
        } else {
            kprintf("[guide11] flush FAIL\n");
            fail++;
        }
    }

    kprintf("[guide11][test] done pass=%d fail=%d\n", pass, fail);
}
