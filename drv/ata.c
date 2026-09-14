/*
 * Project Tsukasa — ATA PIO 28-bit LBA driver (primary bus, master drive, polling)
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

#include "../drv/ata.h"
#include "../drv/blockdev.h"
#include <stdint.h>
#include <stddef.h>

/* Primary bus I/O base and control base. */
#define ATA_BASE   0x1F0u
#define ATA_CTRL   0x3F6u

/* Register offsets from ATA_BASE. */
#define ATA_DATA   0u
#define ATA_ERR    1u
#define ATA_COUNT  2u
#define ATA_LBA0   3u
#define ATA_LBA1   4u
#define ATA_LBA2   5u
#define ATA_DRIVE  6u
#define ATA_STATUS 7u
#define ATA_CMD    7u

/* Status bits. */
#define ATA_SR_BSY  0x80u
#define ATA_SR_DRDY 0x40u
#define ATA_SR_DRQ  0x08u
#define ATA_SR_ERR  0x01u

/* Commands. */
#define ATA_CMD_READ    0x20u
#define ATA_CMD_WRITE   0x30u
#define ATA_CMD_FLUSH   0xE7u
#define ATA_CMD_IDENT   0xECu

static int  g_drive_present = 0;
static uint32_t g_sector_count = 0;

/* The wrappers chunk 32-bit sector counts into the <=255-sector transfers this LBA28 driver supports. */
static int ata_bd_read(block_dev_t *bd, uint64_t lba, uint32_t count,
                       void *buf);
static int ata_bd_write(block_dev_t *bd, uint64_t lba, uint32_t count,
                        const void *buf);
static int ata_bd_sync(block_dev_t *bd);

static block_dev_t g_ata_bdev = {
    .name = "",
    .type = BLOCKDEV_TYPE_IDE,
    .label = "IDE PIO drive",
    .sector_size = 512,
    .sector_count = 0,
    .read = ata_bd_read,
    .write = ata_bd_write,
    .sync = ata_bd_sync,
    .drv_data = NULL,
};

static inline void outb(uint16_t port, uint8_t val)
{
    __asm__ volatile ("outb %0, %1" :: "a"(val), "Nd"(port));
}

static inline void outw(uint16_t port, uint16_t val)
{
    __asm__ volatile ("outw %0, %1" :: "a"(val), "Nd"(port));
}

static inline uint8_t inb(uint16_t port)
{
    uint8_t v;
    __asm__ volatile ("inb %1, %0" : "=a"(v) : "Nd"(port));
    return v;
}

static inline uint16_t inw(uint16_t port)
{
    uint16_t v;
    __asm__ volatile ("inw %1, %0" : "=a"(v) : "Nd"(port));
    return v;
}

/* Software reset via control port. */
static void ata_soft_reset(void)
{
    outb(ATA_CTRL, 0x04u);
    outb(ATA_CTRL, 0x00u);
    inb(ATA_CTRL); inb(ATA_CTRL); inb(ATA_CTRL); inb(ATA_CTRL);
}

/* Poll until BSY clears.  Returns 0 on timeout, 1 on success. */
static int ata_wait_bsy(void)
{
    uint32_t timeout = 0x100000u;
    while ((inb(ATA_BASE + ATA_STATUS) & ATA_SR_BSY) && timeout--)
        __asm__ volatile ("pause");
    return (timeout > 0u) ? 1 : 0;
}

/* Poll until DRQ or ERR is set (data ready for transfer). */
static int ata_wait_drq(void)
{
    uint8_t status;
    uint32_t timeout = 0x100000u;
    do {
        status = inb(ATA_BASE + ATA_STATUS);
        if (status & ATA_SR_ERR) return -1;
        if (status & ATA_SR_DRQ) return 0;
        __asm__ volatile ("pause");
    } while (--timeout);
    return -1;
}

int ata_init(void)
{
    g_drive_present = 0;
    g_sector_count  = 0;

    ata_soft_reset();

    outb(ATA_BASE + ATA_DRIVE, 0xA0u);
    inb(ATA_CTRL); inb(ATA_CTRL); inb(ATA_CTRL); inb(ATA_CTRL);

    if (!ata_wait_bsy()) return 0;

    outb(ATA_BASE + ATA_COUNT, 0);
    outb(ATA_BASE + ATA_LBA0,  0);
    outb(ATA_BASE + ATA_LBA1,  0);
    outb(ATA_BASE + ATA_LBA2,  0);
    outb(ATA_BASE + ATA_CMD,   ATA_CMD_IDENT);

    uint8_t status = inb(ATA_BASE + ATA_STATUS);
    if (status == 0) return 0;

    if (!ata_wait_bsy()) return 0;

    if (inb(ATA_BASE + ATA_LBA1) != 0 || inb(ATA_BASE + ATA_LBA2) != 0)
        return 0;

    if (ata_wait_drq() < 0) return 0;

    uint16_t ident[256];
    for (int i = 0; i < 256; i++)
        ident[i] = inw(ATA_BASE + ATA_DATA);

    g_sector_count = ((uint32_t)ident[61] << 16) | (uint32_t)ident[60];
    g_drive_present = 1;

    /* Registration order is priority order: vfs_init() calls ata_init() before ahci_init(), keeping the IDE boot... */
    g_ata_bdev.sector_count = g_sector_count;
    blockdev_register(&g_ata_bdev);
    return 1;
}

uint32_t ata_sector_count(void)
{
    return g_sector_count;
}

static void ata_setup_lba(uint32_t lba, uint8_t count)
{
    outb(ATA_BASE + ATA_DRIVE,  (uint8_t)(0xE0u | ((lba >> 24) & 0x0Fu)));
    outb(ATA_BASE + ATA_COUNT,  count);
    outb(ATA_BASE + ATA_LBA0,   (uint8_t)(lba));
    outb(ATA_BASE + ATA_LBA1,   (uint8_t)(lba >> 8));
    outb(ATA_BASE + ATA_LBA2,   (uint8_t)(lba >> 16));
}

int ata_read_sectors(uint32_t lba, uint8_t count, void *buf)
{
    if (!g_drive_present || !buf || count == 0) return -1;
    if (!ata_wait_bsy()) return -1;

    ata_setup_lba(lba, count);
    outb(ATA_BASE + ATA_CMD, ATA_CMD_READ);

    uint16_t *dst = (uint16_t *)buf;
    for (uint8_t s = 0; s < count; s++) {
        if (ata_wait_drq() < 0) return -1;
        for (int w = 0; w < 256; w++)
            dst[s * 256 + w] = inw(ATA_BASE + ATA_DATA);
        inb(ATA_CTRL); inb(ATA_CTRL); inb(ATA_CTRL); inb(ATA_CTRL);
    }
    return 0;
}

int ata_write_sectors(uint32_t lba, uint8_t count, const void *buf)
{
    if (!g_drive_present || !buf || count == 0) return -1;
    if (!ata_wait_bsy()) return -1;

    ata_setup_lba(lba, count);
    outb(ATA_BASE + ATA_CMD, ATA_CMD_WRITE);

    const uint16_t *src = (const uint16_t *)buf;
    for (uint8_t s = 0; s < count; s++) {
        if (ata_wait_drq() < 0) return -1;
        for (int w = 0; w < 256; w++)
            outw(ATA_BASE + ATA_DATA, src[s * 256 + w]);
    }

    outb(ATA_BASE + ATA_CMD, ATA_CMD_FLUSH);
    if (!ata_wait_bsy()) return -1;
    return 0;
}

static int ata_bd_read(block_dev_t *bd, uint64_t lba, uint32_t count,
                       void *buf)
{
    uint8_t *p = (uint8_t *)buf;

    (void)bd;
    if (!buf || count == 0)
        return -1;
    if (lba + count < lba || lba + count > (uint64_t)g_sector_count)
        return -1;

    while (count > 0) {
        uint8_t n = (count > 255u) ? 255u : (uint8_t)count;
        if (ata_read_sectors((uint32_t)lba, n, p) != 0)
            return -1;
        lba += n;
        p += (size_t)n * 512u;
        count -= n;
    }
    return 0;
}

static int ata_bd_write(block_dev_t *bd, uint64_t lba, uint32_t count,
                        const void *buf)
{
    const uint8_t *p = (const uint8_t *)buf;

    (void)bd;
    if (!buf || count == 0)
        return -1;
    if (lba + count < lba || lba + count > (uint64_t)g_sector_count)
        return -1;

    while (count > 0) {
        uint8_t n = (count > 255u) ? 255u : (uint8_t)count;
        if (ata_write_sectors((uint32_t)lba, n, p) != 0)
            return -1;
        lba += n;
        p += (size_t)n * 512u;
        count -= n;
    }
    return 0;
}

static int ata_bd_sync(block_dev_t *bd)
{
    (void)bd;
    return 0;
}
