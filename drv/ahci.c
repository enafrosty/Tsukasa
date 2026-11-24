/*
 * Project Tsukasa — AHCI (SATA) PCIe/DMA storage driver (AHCI 1.3.1)
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

#include "ahci.h"
#include "blockdev.h"
#include "acpi.h"
#include "ioapic.h"
#include "irq.h"
#include "../dev/pci.h"
#include "../include/kprintf.h"
#include "../include/kutils.h"
#include "../include/lapic.h"
#include "../include/spinlock.h"
#include "../mm/pmm.h"
#include "../mm/vmm_x64.h"
#include "../proc/process.h"
#include "../sys/wait_queue.h"

#include <stddef.h>
#include <stdint.h>

#ifdef __x86_64__

#define PCI_CLASS_MASS_STORAGE 0x01u
#define PCI_SUBCLASS_SATA      0x06u
#define PCI_PROGIF_AHCI        0x01u

/* AHCI 1.3.1 Generic Host Control registers (ABAR offsets). */
#define AHCI_REG_CAP           0x00u
#define AHCI_REG_GHC           0x04u
#define AHCI_REG_IS            0x08u
#define AHCI_REG_PI            0x0Cu
#define AHCI_REG_VS            0x10u
#define AHCI_REG_CAP2          0x24u
#define AHCI_REG_BOHC          0x28u

#define AHCI_CAP_S64A          (1u << 31)
#define AHCI_CAP2_BOH          (1u << 0)
#define AHCI_BOHC_BOS          (1u << 0)
#define AHCI_BOHC_OOS          (1u << 1)

#define AHCI_GHC_HR            (1u << 0)
#define AHCI_GHC_IE            (1u << 1)
#define AHCI_GHC_AE            (1u << 31)

/* AHCI 1.3.1 Port registers (base = 0x100 + port * 0x80). */
#define AHCI_PORT_BASE(p)      (0x100u + (uint32_t)(p) * 0x80u)
#define AHCI_PxCLB             0x00u
#define AHCI_PxCLBU            0x04u
#define AHCI_PxFB              0x08u
#define AHCI_PxFBU             0x0Cu
#define AHCI_PxIS              0x10u
#define AHCI_PxIE              0x14u
#define AHCI_PxCMD             0x18u
#define AHCI_PxTFD             0x20u
#define AHCI_PxSIG             0x24u
#define AHCI_PxSSTS            0x28u
#define AHCI_PxSCTL            0x2Cu
#define AHCI_PxSERR            0x30u
#define AHCI_PxSACT            0x34u
#define AHCI_PxCI              0x38u

#define AHCI_PxCMD_ST          (1u << 0)
#define AHCI_PxCMD_SUD         (1u << 1)
#define AHCI_PxCMD_POD         (1u << 2)
#define AHCI_PxCMD_FRE         (1u << 4)
#define AHCI_PxCMD_FR          (1u << 14)
#define AHCI_PxCMD_CR          (1u << 15)

#define AHCI_PxIS_DHRS         (1u << 0)
#define AHCI_PxIS_PSS          (1u << 1)
#define AHCI_PxIS_DSS          (1u << 2)
#define AHCI_PxIS_SDBS         (1u << 3)
#define AHCI_PxIS_IFS          (1u << 27)
#define AHCI_PxIS_HBDS         (1u << 28)
#define AHCI_PxIS_HBFS         (1u << 29)
#define AHCI_PxIS_TFES         (1u << 30)

#define AHCI_PxIE_MASK         (AHCI_PxIS_DHRS | AHCI_PxIS_PSS | AHCI_PxIS_DSS | \
                                AHCI_PxIS_SDBS | AHCI_PxIS_IFS | AHCI_PxIS_HBDS | \
                                AHCI_PxIS_HBFS | AHCI_PxIS_TFES)

#define AHCI_PxTFD_ERR         (1u << 0)
#define AHCI_PxTFD_DRQ         (1u << 3)
#define AHCI_PxTFD_BSY         (1u << 7)

#define AHCI_SIG_ATA           0x00000101u

/* ATA commands */
#define ATA_CMD_IDENTIFY       0xECu
#define ATA_CMD_READ_DMA_EXT   0x25u
#define ATA_CMD_WRITE_DMA_EXT  0x35u
#define ATA_CMD_FLUSH_EXT      0xEAu

#define FIS_TYPE_REG_H2D       0x27u

#define AHCI_MAX_CTRLS         4
#define AHCI_MAX_PORTS         32
#define AHCI_MAX_PRDT          128
#define AHCI_BOUNCE_PAGES      16 /* 64 KiB */
#define AHCI_MAX_CHUNK_SECTORS 128u

typedef struct {
    uint16_t flags;
    uint16_t prdtl;
    uint32_t prdbc;
    uint32_t ctba;
    uint32_t ctbau;
    uint32_t rsv[4];
} __attribute__((packed)) hba_cmd_hdr_t;

_Static_assert(sizeof(hba_cmd_hdr_t) == 32, "AHCI command header is 32 bytes");

typedef struct {
    uint32_t dba;
    uint32_t dbau;
    uint32_t rsv0;
    uint32_t dbc;
} __attribute__((packed)) hba_prdt_entry_t;

_Static_assert(sizeof(hba_prdt_entry_t) == 16, "AHCI PRDT entry is 16 bytes");

typedef struct {
    uint8_t cfis[64];
    uint8_t acmd[16];
    uint8_t rsv[48];
    hba_prdt_entry_t prdt[AHCI_MAX_PRDT];
} __attribute__((packed)) hba_cmd_tbl_t;

typedef struct {
    uint8_t fis_type;
    uint8_t pmport_c;
    uint8_t command;
    uint8_t featurel;
    uint8_t lba0;
    uint8_t lba1;
    uint8_t lba2;
    uint8_t device;
    uint8_t lba3;
    uint8_t lba4;
    uint8_t lba5;
    uint8_t featureh;
    uint8_t countl;
    uint8_t counth;
    uint8_t icc;
    uint8_t control;
    uint8_t rsv[4];
} __attribute__((packed)) fis_reg_h2d_t;

_Static_assert(sizeof(fis_reg_h2d_t) == 20, "H2D Register FIS is 20 bytes");

struct ahci_ctrl;

typedef struct ahci_port {
    struct ahci_ctrl *ctrl;
    uint32_t port_num;
    int active;

    uintptr_t clb_phys;
    uintptr_t fb_phys;
    uintptr_t ctbl_phys;
    uintptr_t bounce_phys;

    hba_cmd_hdr_t *clb;
    uint8_t *fb;
    hba_cmd_tbl_t *ctbl;
    uint8_t *bounce;

    spinlock_t lock;
    int busy;
    wait_queue_head_t avail_wq;
    wait_queue_head_t io_wq;
    volatile int cmd_done;
    volatile int cmd_err;

    block_dev_t bdev;
} ahci_port_t;

typedef struct ahci_ctrl {
    uintptr_t abar;
    uint32_t cap;
    int s64a;
    uint8_t irq_vec;
    uint8_t irq_line;
    int use_msi;
    ahci_port_t ports[AHCI_MAX_PORTS];
} ahci_ctrl_t;

static ahci_ctrl_t g_ahci_ctrls[AHCI_MAX_CTRLS];
static int g_ahci_ctrl_count;

static inline uint32_t ahci_read32(uintptr_t base, uint32_t off)
{
    return *(volatile uint32_t *)(base + off);
}

static inline void ahci_write32(uintptr_t base, uint32_t off, uint32_t val)
{
    *(volatile uint32_t *)(base + off) = val;
}

static inline uint32_t port_read32(const ahci_port_t *p, uint32_t reg)
{
    return ahci_read32(p->ctrl->abar, AHCI_PORT_BASE(p->port_num) + reg);
}

static inline void port_write32(const ahci_port_t *p, uint32_t reg, uint32_t val)
{
    ahci_write32(p->ctrl->abar, AHCI_PORT_BASE(p->port_num) + reg, val);
}

static inline int ahci_irq_enabled_cpu(void)
{
    uint64_t rflags;
    __asm__ volatile ("pushfq\n\tpopq %0" : "=r"(rflags));
    return (rflags & (1ULL << 9)) != 0;
}

static void ahci_port_acquire(ahci_port_t *p)
{
    for (;;) {
        unsigned long flags = spin_lock_irqsave(&p->lock);
        if (!p->busy) {
            p->busy = 1;
            spin_unlock_irqrestore(&p->lock, flags);
            return;
        }
        spin_unlock_irqrestore(&p->lock, flags);

        process_t *self = process_current();
        if (ahci_irq_enabled_cpu() && self) {
            wait_queue_entry_t entry;
            entry.proc = self;
            entry.next = NULL;
            wait_queue_prepare_to_wait(&p->avail_wq, &entry);
            if (!p->busy) {
                wait_queue_finish_wait(&p->avail_wq, &entry);
                continue;
            }
            process_yield();
            wait_queue_finish_wait(&p->avail_wq, &entry);
        } else {
            __asm__ volatile ("pause");
        }
    }
}

static void ahci_port_release(ahci_port_t *p)
{
    unsigned long flags = spin_lock_irqsave(&p->lock);
    p->busy = 0;
    spin_unlock_irqrestore(&p->lock, flags);
    wait_queue_wake_all(&p->avail_wq);
}

/* Strictly non-blocking IRQ handler: clear PxIS + GHC.IS, wake waiters. */
static void ahci_handle_irq_common(ahci_ctrl_t *ctrl)
{
    uint32_t hba_is;
    if (!ctrl || !ctrl->abar)
        return;

    hba_is = ahci_read32(ctrl->abar, AHCI_REG_IS);
    if (!hba_is)
        return;

    for (uint32_t i = 0; i < AHCI_MAX_PORTS; i++) {
        if (!(hba_is & (1u << i)))
            continue;
        ahci_port_t *p = &ctrl->ports[i];
        uint32_t px_is = port_read32(p, AHCI_PxIS);
        if (px_is)
            port_write32(p, AHCI_PxIS, px_is);
        if (p->active) {
            if (px_is & (AHCI_PxIS_TFES | AHCI_PxIS_HBFS |
                         AHCI_PxIS_HBDS | AHCI_PxIS_IFS)) {
                p->cmd_err = 1;
                p->cmd_done = 1;
                wait_queue_wake_all(&p->io_wq);
            } else if ((port_read32(p, AHCI_PxCI) & 1u) == 0 ||
                       (px_is & (AHCI_PxIS_DHRS | AHCI_PxIS_PSS |
                                 AHCI_PxIS_DSS | AHCI_PxIS_SDBS))) {
                p->cmd_done = 1;
                wait_queue_wake_all(&p->io_wq);
            }
        }
    }

    ahci_write32(ctrl->abar, AHCI_REG_IS, hba_is);
}

static void ahci_vec_irq(uint8_t vec, void *ctx)
{
    (void)vec;
    ahci_handle_irq_common((ahci_ctrl_t *)ctx);
}

static void ahci_pic_irq(uint8_t irq, void *ctx)
{
    (void)irq;
    ahci_handle_irq_common((ahci_ctrl_t *)ctx);
}

static int ahci_port_stop(ahci_port_t *p)
{
    uint32_t cmd = port_read32(p, AHCI_PxCMD);
    uint32_t timeout = 1000000u;

    cmd &= ~AHCI_PxCMD_ST;
    port_write32(p, AHCI_PxCMD, cmd);
    while ((port_read32(p, AHCI_PxCMD) & AHCI_PxCMD_CR) && timeout--)
        __asm__ volatile ("pause");
    if (timeout == 0)
        return -1;

    cmd = port_read32(p, AHCI_PxCMD);
    cmd &= ~AHCI_PxCMD_FRE;
    port_write32(p, AHCI_PxCMD, cmd);
    timeout = 1000000u;
    while ((port_read32(p, AHCI_PxCMD) & AHCI_PxCMD_FR) && timeout--)
        __asm__ volatile ("pause");
    return (timeout > 0) ? 0 : -1;
}

static int ahci_port_start(ahci_port_t *p)
{
    uint32_t timeout = 1000000u;
    uint32_t cmd;

    while ((port_read32(p, AHCI_PxCMD) & AHCI_PxCMD_CR) && timeout--)
        __asm__ volatile ("pause");
    if (timeout == 0)
        return -1;

    cmd = port_read32(p, AHCI_PxCMD);
    cmd |= AHCI_PxCMD_FRE | AHCI_PxCMD_SUD | AHCI_PxCMD_POD;
    port_write32(p, AHCI_PxCMD, cmd);
    cmd |= AHCI_PxCMD_ST;
    port_write32(p, AHCI_PxCMD, cmd);
    return 0;
}

static int ahci_wait_cmd(ahci_port_t *p)
{
    process_t *self = process_current();

    if (ahci_irq_enabled_cpu() && self) {
        wait_queue_entry_t entry;
        entry.proc = self;
        entry.next = NULL;

        while (!p->cmd_done) {
            wait_queue_prepare_to_wait(&p->io_wq, &entry);
            if (p->cmd_done) {
                wait_queue_finish_wait(&p->io_wq, &entry);
                break;
            }
            process_yield();
            wait_queue_finish_wait(&p->io_wq, &entry);
        }
    } else {
        uint32_t timeout = 20000000u;
        while ((port_read32(p, AHCI_PxCI) & 1u) && timeout--) {
            uint32_t px_is = port_read32(p, AHCI_PxIS);
            if (px_is & (AHCI_PxIS_TFES | AHCI_PxIS_HBFS |
                         AHCI_PxIS_HBDS | AHCI_PxIS_IFS)) {
                p->cmd_err = 1;
                break;
            }
            __asm__ volatile ("pause");
        }
        uint32_t px_is = port_read32(p, AHCI_PxIS);
        if (px_is) {
            port_write32(p, AHCI_PxIS, px_is);
            ahci_write32(p->ctrl->abar, AHCI_REG_IS, (1u << p->port_num));
        }
        if (timeout == 0)
            p->cmd_err = 1;
        p->cmd_done = 1;
    }

    if (p->cmd_err || (port_read32(p, AHCI_PxTFD) & AHCI_PxTFD_ERR))
        return -1;
    return 0;
}

static int ahci_issue_rw(ahci_port_t *p, uint8_t ata_cmd, uint64_t lba,
                         uint32_t sectors, void *buf, int is_write)
{
    uint32_t timeout = 1000000u;
    size_t total_bytes = (size_t)sectors * 512u;
    int used_bounce = 0;
    const uint8_t *src_buf = (const uint8_t *)buf;
    uint8_t *dma_buf = (uint8_t *)buf;
    uint16_t prdt_idx = 0;

    while ((port_read32(p, AHCI_PxTFD) & (AHCI_PxTFD_BSY | AHCI_PxTFD_DRQ)) &&
           timeout--)
        __asm__ volatile ("pause");
    if (timeout == 0)
        return -1;

    /* AHCI PRDT requires even byte counts and word-aligned physical addresses. */
    if (((uintptr_t)dma_buf & 1u) != 0) {
        used_bounce = 1;
    } else {
        size_t rem = total_bytes;
        uintptr_t va = (uintptr_t)dma_buf;
        while (rem > 0) {
            if (prdt_idx >= AHCI_MAX_PRDT) {
                used_bounce = 1;
                break;
            }
            uint64_t pa = vmm_virt_to_phys(va);
            if (!p->ctrl->s64a && (pa >= 0x100000000ULL)) {
                used_bounce = 1;
                break;
            }
            size_t page_off = va & 0xFFFULL;
            size_t chunk = 4096u - page_off;
            if (chunk > rem)
                chunk = rem;

            p->ctbl->prdt[prdt_idx].dba = (uint32_t)(pa & 0xFFFFFFFFu);
            p->ctbl->prdt[prdt_idx].dbau = (uint32_t)(pa >> 32);
            p->ctbl->prdt[prdt_idx].rsv0 = 0;
            p->ctbl->prdt[prdt_idx].dbc = (uint32_t)(chunk - 1u);
            prdt_idx++;
            va += chunk;
            rem -= chunk;
        }
    }

    if (used_bounce) {
        if (is_write)
            k_memcpy(p->bounce, src_buf, total_bytes);
        prdt_idx = 0;
        size_t rem = total_bytes;
        uintptr_t pa = p->bounce_phys;
        while (rem > 0) {
            size_t chunk = (rem > 4096u) ? 4096u : rem;
            p->ctbl->prdt[prdt_idx].dba = (uint32_t)(pa & 0xFFFFFFFFu);
            p->ctbl->prdt[prdt_idx].dbau = (uint32_t)(pa >> 32);
            p->ctbl->prdt[prdt_idx].rsv0 = 0;
            p->ctbl->prdt[prdt_idx].dbc = (uint32_t)(chunk - 1u);
            prdt_idx++;
            pa += chunk;
            rem -= chunk;
        }
    }

    if (prdt_idx > 0)
        p->ctbl->prdt[prdt_idx - 1].dbc |= (1u << 31);

    hba_cmd_hdr_t *hdr = &p->clb[0];
    k_memset(hdr, 0, sizeof(*hdr));
    hdr->flags = (uint16_t)(5u | (is_write ? (1u << 6) : 0u));
    hdr->prdtl = prdt_idx;
    hdr->ctba = (uint32_t)(p->ctbl_phys & 0xFFFFFFFFu);
    hdr->ctbau = (uint32_t)(p->ctbl_phys >> 32);

    k_memset(p->ctbl->cfis, 0, sizeof(p->ctbl->cfis));
    fis_reg_h2d_t *fis = (fis_reg_h2d_t *)p->ctbl->cfis;
    fis->fis_type = FIS_TYPE_REG_H2D;
    fis->pmport_c = 0x80u;
    fis->command = ata_cmd;
    fis->device = (ata_cmd == ATA_CMD_IDENTIFY) ? 0x00u : 0x40u;
    fis->lba0 = (uint8_t)(lba & 0xFFu);
    fis->lba1 = (uint8_t)((lba >> 8) & 0xFFu);
    fis->lba2 = (uint8_t)((lba >> 16) & 0xFFu);
    fis->lba3 = (uint8_t)((lba >> 24) & 0xFFu);
    fis->lba4 = (uint8_t)((lba >> 32) & 0xFFu);
    fis->lba5 = (uint8_t)((lba >> 40) & 0xFFu);
    fis->countl = (uint8_t)(sectors & 0xFFu);
    fis->counth = (uint8_t)((sectors >> 8) & 0xFFu);

    p->cmd_done = 0;
    p->cmd_err = 0;
    port_write32(p, AHCI_PxIS, 0xFFFFFFFFu);
    __asm__ volatile ("mfence" ::: "memory");
    port_write32(p, AHCI_PxCI, 1u);

    int rc = ahci_wait_cmd(p);
    if (rc == 0 && used_bounce && !is_write)
        k_memcpy(buf, p->bounce, total_bytes);
    return rc;
}

static int ahci_issue_flush(ahci_port_t *p)
{
    uint32_t timeout = 1000000u;
    while ((port_read32(p, AHCI_PxTFD) & (AHCI_PxTFD_BSY | AHCI_PxTFD_DRQ)) &&
           timeout--)
        __asm__ volatile ("pause");
    if (timeout == 0)
        return -1;

    hba_cmd_hdr_t *hdr = &p->clb[0];
    k_memset(hdr, 0, sizeof(*hdr));
    hdr->flags = 5u;
    hdr->prdtl = 0;
    hdr->ctba = (uint32_t)(p->ctbl_phys & 0xFFFFFFFFu);
    hdr->ctbau = (uint32_t)(p->ctbl_phys >> 32);

    k_memset(p->ctbl->cfis, 0, sizeof(p->ctbl->cfis));
    fis_reg_h2d_t *fis = (fis_reg_h2d_t *)p->ctbl->cfis;
    fis->fis_type = FIS_TYPE_REG_H2D;
    fis->pmport_c = 0x80u;
    fis->command = ATA_CMD_FLUSH_EXT;
    fis->device = 0x40u;

    p->cmd_done = 0;
    p->cmd_err = 0;
    port_write32(p, AHCI_PxIS, 0xFFFFFFFFu);
    __asm__ volatile ("mfence" ::: "memory");
    port_write32(p, AHCI_PxCI, 1u);

    return ahci_wait_cmd(p);
}

static int ahci_bd_read(block_dev_t *bd, uint64_t lba, uint32_t count, void *buf)
{
    ahci_port_t *p = (ahci_port_t *)bd->drv_data;
    uint8_t *dst = (uint8_t *)buf;

    if (!p || !buf || count == 0)
        return -1;
    if (lba + count < lba || lba + count > bd->sector_count)
        return -1;

    ahci_port_acquire(p);
    while (count > 0) {
        uint32_t chunk = (count > AHCI_MAX_CHUNK_SECTORS) ? AHCI_MAX_CHUNK_SECTORS : count;
        if (ahci_issue_rw(p, ATA_CMD_READ_DMA_EXT, lba, chunk, dst, 0) != 0) {
            ahci_port_release(p);
            return -1;
        }
        lba += chunk;
        dst += (size_t)chunk * 512u;
        count -= chunk;
    }
    ahci_port_release(p);
    return 0;
}

static int ahci_bd_write(block_dev_t *bd, uint64_t lba, uint32_t count, const void *buf)
{
    ahci_port_t *p = (ahci_port_t *)bd->drv_data;
    const uint8_t *src = (const uint8_t *)buf;

    if (!p || !buf || count == 0)
        return -1;
    if (lba + count < lba || lba + count > bd->sector_count)
        return -1;

    ahci_port_acquire(p);
    while (count > 0) {
        uint32_t chunk = (count > AHCI_MAX_CHUNK_SECTORS) ? AHCI_MAX_CHUNK_SECTORS : count;
        if (ahci_issue_rw(p, ATA_CMD_WRITE_DMA_EXT, lba, chunk, (void *)src, 1) != 0) {
            ahci_port_release(p);
            return -1;
        }
        lba += chunk;
        src += (size_t)chunk * 512u;
        count -= chunk;
    }
    ahci_port_release(p);
    return 0;
}

static int ahci_bd_sync(block_dev_t *bd)
{
    ahci_port_t *p = (ahci_port_t *)bd->drv_data;
    int rc;
    if (!p)
        return -1;
    ahci_port_acquire(p);
    rc = ahci_issue_flush(p);
    ahci_port_release(p);
    return rc;
}

static int ahci_probe_port(ahci_ctrl_t *ctrl, uint32_t port_num)
{
    ahci_port_t *p = &ctrl->ports[port_num];
    uint32_t ssts, det, ipm, sig;
    uint16_t *ident;
    uint64_t sectors;

    p->ctrl = ctrl;
    p->port_num = port_num;
    p->active = 0;

    ssts = port_read32(p, AHCI_PxSSTS);
    det = ssts & 0x0Fu;
    ipm = (ssts >> 8) & 0x0Fu;
    if (det != 3u || ipm != 1u)
        return 0;

    sig = port_read32(p, AHCI_PxSIG);
    if (sig != AHCI_SIG_ATA)
        return 0;

    if (ahci_port_stop(p) != 0)
        return 0;

    p->clb_phys = pmm_alloc_pages(1);
    p->fb_phys = pmm_alloc_pages(1);
    p->ctbl_phys = pmm_alloc_pages(1);
    p->bounce_phys = pmm_alloc_pages(AHCI_BOUNCE_PAGES);
    if (!p->clb_phys || !p->fb_phys || !p->ctbl_phys || !p->bounce_phys)
        return 0;

    p->clb = (hba_cmd_hdr_t *)vmm_phys_to_virt(p->clb_phys);
    p->fb = (uint8_t *)vmm_phys_to_virt(p->fb_phys);
    p->ctbl = (hba_cmd_tbl_t *)vmm_phys_to_virt(p->ctbl_phys);
    p->bounce = (uint8_t *)vmm_phys_to_virt(p->bounce_phys);

    k_memset(p->clb, 0, PAGE_SIZE);
    k_memset(p->fb, 0, PAGE_SIZE);
    k_memset(p->ctbl, 0, PAGE_SIZE);
    k_memset(p->bounce, 0, AHCI_BOUNCE_PAGES * PAGE_SIZE);

    port_write32(p, AHCI_PxCLB, (uint32_t)(p->clb_phys & 0xFFFFFFFFu));
    port_write32(p, AHCI_PxCLBU, ctrl->s64a ? (uint32_t)(p->clb_phys >> 32) : 0u);
    port_write32(p, AHCI_PxFB, (uint32_t)(p->fb_phys & 0xFFFFFFFFu));
    port_write32(p, AHCI_PxFBU, ctrl->s64a ? (uint32_t)(p->fb_phys >> 32) : 0u);

    port_write32(p, AHCI_PxSERR, 0xFFFFFFFFu);
    port_write32(p, AHCI_PxIS, 0xFFFFFFFFu);

    if (ahci_port_start(p) != 0)
        return 0;

    p->lock = SPINLOCK_INIT;
    p->busy = 0;
    wait_queue_init(&p->avail_wq);
    wait_queue_init(&p->io_wq);
    p->active = 1;

    port_write32(p, AHCI_PxIE, AHCI_PxIE_MASK);

    ident = (uint16_t *)p->bounce;
    k_memset(ident, 0, 512);
    if (ahci_issue_rw(p, ATA_CMD_IDENTIFY, 0, 1, ident, 0) != 0) {
        p->active = 0;
        port_write32(p, AHCI_PxIE, 0);
        return 0;
    }

    if (ident[83] & (1u << 10)) {
        sectors = ((uint64_t)ident[103] << 48) |
                  ((uint64_t)ident[102] << 32) |
                  ((uint64_t)ident[101] << 16) |
                  (uint64_t)ident[100];
    } else {
        sectors = ((uint64_t)ident[61] << 16) | (uint64_t)ident[60];
    }
    if (sectors == 0) {
        p->active = 0;
        port_write32(p, AHCI_PxIE, 0);
        return 0;
    }

    k_memset(&p->bdev, 0, sizeof(p->bdev));
    p->bdev.type = BLOCKDEV_TYPE_SATA;
    p->bdev.sector_size = 512u;
    p->bdev.sector_count = sectors;
    p->bdev.read = ahci_bd_read;
    p->bdev.write = ahci_bd_write;
    p->bdev.sync = ahci_bd_sync;
    p->bdev.drv_data = p;

    {
        char model[BLOCKDEV_LABEL_MAX];
        int m = 0;
        for (int w = 27; w <= 46 && m < BLOCKDEV_LABEL_MAX - 2; w++) {
            char hi = (char)(ident[w] >> 8);
            char lo = (char)(ident[w] & 0xFFu);
            if (hi >= 0x20 && hi <= 0x7E) model[m++] = hi;
            if (m < BLOCKDEV_LABEL_MAX - 1 && lo >= 0x20 && lo <= 0x7E) model[m++] = lo;
        }
        while (m > 0 && model[m - 1] == ' ')
            m--;
        model[m] = '\0';
        if (m > 0)
            k_strcpy(p->bdev.label, model);
        else
            k_strcpy(p->bdev.label, "AHCI SATA Disk");
    }

    if (blockdev_register(&p->bdev) != 0) {
        p->active = 0;
        return 0;
    }

    kprintf("[ahci] port %u online: %s sectors=%u (%u MiB)\n",
            port_num, p->bdev.name, (uint32_t)sectors,
            (uint32_t)(sectors / 2048u));
    return 1;
}

static int ahci_init_controller(const pci_device_info_t *dev)
{
    uint64_t bar_phys = 0, bar_size = 0;
    int is_mmio = 0;
    uintptr_t abar = 0;
    ahci_ctrl_t *ctrl;
    uint32_t ghc, cap2, pi;
    int vec;
    int drives = 0;

    if (g_ahci_ctrl_count >= AHCI_MAX_CTRLS)
        return 0;

    if (pci_get_bar(dev, 5, &bar_phys, &bar_size, &is_mmio) != 0 ||
        !is_mmio || !bar_phys)
        return 0;

    if (bar_size < 0x1100u)
        bar_size = 0x1100u;

    if (vmm_map_io_region(bar_phys, (size_t)bar_size, &abar) != 0 || !abar)
        return 0;

    pci_enable_mmio(dev);
    pci_enable_bus_mastering(dev);

    ctrl = &g_ahci_ctrls[g_ahci_ctrl_count++];
    k_memset(ctrl, 0, sizeof(*ctrl));
    ctrl->abar = abar;
    ctrl->irq_line = dev->irq_line;

    /* Enable AHCI mode before touching controller registers (AHCI 1.3.1 §10.1.2). */
    ghc = ahci_read32(abar, AHCI_REG_GHC);
    if (!(ghc & AHCI_GHC_AE))
        ahci_write32(abar, AHCI_REG_GHC, ghc | AHCI_GHC_AE);

    cap2 = ahci_read32(abar, AHCI_REG_CAP2);
    if (cap2 & AHCI_CAP2_BOH) {
        uint32_t bohc = ahci_read32(abar, AHCI_REG_BOHC);
        ahci_write32(abar, AHCI_REG_BOHC, bohc | AHCI_BOHC_OOS);
        uint32_t t = 1000000u;
        while ((ahci_read32(abar, AHCI_REG_BOHC) & AHCI_BOHC_BOS) && t--)
            __asm__ volatile ("pause");
    }

    ctrl->cap = ahci_read32(abar, AHCI_REG_CAP);
    ctrl->s64a = (ctrl->cap & AHCI_CAP_S64A) ? 1 : 0;

    ahci_write32(abar, AHCI_REG_IS, 0xFFFFFFFFu);

    vec = irq_alloc_vector();
    if (vec >= 0 && irq_register_vector_handler((uint8_t)vec, ahci_vec_irq, ctrl) == 0) {
        ctrl->irq_vec = (uint8_t)vec;
        uint8_t bsp = (uint8_t)lapic_read_id();
        if (pci_enable_msi(dev, ctrl->irq_vec, bsp) == 0) {
            ctrl->use_msi = 1;
        } else if (ioapic_active() && dev->irq_line != 0xFFu) {
            uint32_t gsi = acpi_irq_to_gsi(dev->irq_line);
            if (ioapic_route_gsi(gsi, ctrl->irq_vec, bsp, 1, 1) != 0) {
                irq_unregister_vector_handler(ctrl->irq_vec);
                irq_free_vector(ctrl->irq_vec);
                ctrl->irq_vec = 0;
            }
        } else {
            irq_unregister_vector_handler(ctrl->irq_vec);
            irq_free_vector(ctrl->irq_vec);
            ctrl->irq_vec = 0;
        }
    }

    if (!ctrl->use_msi && !ioapic_active() && dev->irq_line < 16u)
        irq_register_handler(dev->irq_line, ahci_pic_irq, ctrl);

    ghc = ahci_read32(abar, AHCI_REG_GHC);
    ahci_write32(abar, AHCI_REG_GHC, ghc | AHCI_GHC_AE | AHCI_GHC_IE);

    pi = ahci_read32(abar, AHCI_REG_PI);
    kprintf("[ahci] ctrl %02x:%02x.%u ABAR=0x%x%08x PI=0x%08x %s vec=0x%x\n",
            dev->bus, dev->device, dev->function,
            (uint32_t)(bar_phys >> 32), (uint32_t)bar_phys, pi,
            ctrl->use_msi ? "MSI" : (ctrl->irq_vec ? "IOAPIC" : "PIC"),
            ctrl->irq_vec);

    for (uint32_t p = 0; p < AHCI_MAX_PORTS; p++) {
        if (pi & (1u << p))
            drives += ahci_probe_port(ctrl, p);
    }

    return drives;
}

int ahci_init(void)
{
    int total = 0;
    int count = pci_device_count();

    g_ahci_ctrl_count = 0;
    for (int i = 0; i < count; i++) {
        const pci_device_info_t *dev = pci_device_at(i);
        if (!dev)
            continue;
        if (dev->class_code == PCI_CLASS_MASS_STORAGE &&
            dev->subclass == PCI_SUBCLASS_SATA &&
            dev->prog_if == PCI_PROGIF_AHCI) {
            total += ahci_init_controller(dev);
        }
    }
    return total;
}

#else

int ahci_init(void) { return 0; }

#endif /* __x86_64__ */
