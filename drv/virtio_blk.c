/*
 * Project Tsukasa — VirtIO-blk storage driver (VirtIO 1.2 modern + legacy fallback)
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

#include "virtio_blk.h"
#include "blockdev.h"
#include "acpi.h"
#include "ioapic.h"
#include "irq.h"
#include "../dev/pci.h"
#include "../include/io.h"
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

#define VIRTIO_PCI_VENDOR_ID         0x1AF4u
#define VIRTIO_PCI_DEV_BLK_LEGACY    0x1001u
#define VIRTIO_PCI_DEV_BLK_MODERN    0x1042u

#define PCI_CAP_ID_VNDR              0x09u

/* VirtIO 1.2 PCI capability cfg_type values (§4.1.4). */
#define VIRTIO_PCI_CAP_COMMON_CFG    1u
#define VIRTIO_PCI_CAP_NOTIFY_CFG    2u
#define VIRTIO_PCI_CAP_ISR_CFG       3u
#define VIRTIO_PCI_CAP_DEVICE_CFG    4u

/* VirtIO status bits (§2.1). */
#define VIRTIO_STATUS_ACK            0x01u
#define VIRTIO_STATUS_DRIVER         0x02u
#define VIRTIO_STATUS_DRIVER_OK      0x04u
#define VIRTIO_STATUS_FEATURES_OK    0x08u
#define VIRTIO_STATUS_FAILED         0x80u

/* VirtIO-blk feature bits (§5.2.3). */
#define VIRTIO_BLK_F_RO              (1u << 5)
#define VIRTIO_BLK_F_BLK_SIZE        (1u << 6)
#define VIRTIO_BLK_F_FLUSH           (1u << 9)
#define VIRTIO_F_VERSION_1_HI        (1u << 0) /* bit 32 */

/* Modern Common Config offsets (§4.1.4.3). */
#define VIO_COM_DEV_FEAT_SEL         0x00u
#define VIO_COM_DEV_FEAT             0x04u
#define VIO_COM_DRV_FEAT_SEL         0x08u
#define VIO_COM_DRV_FEAT             0x0Cu
#define VIO_COM_MSIX_CFG             0x10u
#define VIO_COM_NUM_QUEUES           0x12u
#define VIO_COM_STATUS               0x14u
#define VIO_COM_CFG_GEN              0x15u
#define VIO_COM_Q_SEL                0x16u
#define VIO_COM_Q_SIZE               0x18u
#define VIO_COM_Q_MSIX_VEC           0x1Au
#define VIO_COM_Q_ENABLE             0x1Cu
#define VIO_COM_Q_NOTIFY_OFF         0x1Eu
#define VIO_COM_Q_DESC_LO            0x20u
#define VIO_COM_Q_DESC_HI            0x24u
#define VIO_COM_Q_AVAIL_LO           0x28u
#define VIO_COM_Q_AVAIL_HI           0x2Cu
#define VIO_COM_Q_USED_LO            0x30u
#define VIO_COM_Q_USED_HI            0x34u

/* Legacy I/O offsets (§4.1.4.8). */
#define VIO_LEG_HOST_FEAT            0x00u
#define VIO_LEG_GUEST_FEAT           0x04u
#define VIO_LEG_Q_PFN                0x08u
#define VIO_LEG_Q_SIZE               0x0Cu
#define VIO_LEG_Q_SEL                0x0Eu
#define VIO_LEG_Q_NOTIFY             0x10u
#define VIO_LEG_STATUS               0x12u
#define VIO_LEG_ISR                  0x13u
#define VIO_LEG_DEV_CFG              0x14u

/* Virtqueue descriptor flags (§2.7.5). */
#define VRING_DESC_F_NEXT            1u
#define VRING_DESC_F_WRITE           2u

/* VirtIO-blk request types (§5.2.6). */
#define VIRTIO_BLK_T_IN              0u
#define VIRTIO_BLK_T_OUT             1u
#define VIRTIO_BLK_T_FLUSH           4u

#define VIRTIO_BLK_S_OK              0u

#define VIRTIO_BLK_MAX_DEVS          4
#define VIRTIO_BLK_MAX_DATA_DESCS    16
#define VIRTIO_BLK_MAX_CHUNK_SECTORS 128u

typedef struct {
    uint64_t addr;
    uint32_t len;
    uint16_t flags;
    uint16_t next;
} __attribute__((packed)) vq_desc_t;

typedef struct {
    uint16_t flags;
    uint16_t idx;
    uint16_t ring[];
} __attribute__((packed)) vq_avail_t;

typedef struct {
    uint32_t id;
    uint32_t len;
} __attribute__((packed)) vq_used_elem_t;

typedef struct {
    uint16_t flags;
    uint16_t idx;
    vq_used_elem_t ring[];
} __attribute__((packed)) vq_used_t;

typedef struct {
    uint32_t type;
    uint32_t reserved;
    uint64_t sector;
} __attribute__((packed)) virtio_blk_req_hdr_t;

_Static_assert(sizeof(virtio_blk_req_hdr_t) == 16, "virtio_blk_req_hdr_t is 16 bytes");

typedef struct virtio_blk_dev {
    int active;
    int is_modern;
    int read_only;
    int has_flush;

    /* Modern MMIO regions */
    uintptr_t common_cfg;
    uintptr_t notify_base;
    uint32_t notify_off_mult;
    uint16_t q_notify_off;
    uintptr_t isr_cfg;
    uintptr_t device_cfg;

    /* Legacy I/O base */
    uint16_t io_base;

    /* Virtqueue 0 */
    uint16_t q_size;
    uintptr_t q_phys;
    vq_desc_t *desc;
    vq_avail_t *avail;
    vq_used_t *used;
    uint16_t last_used_idx;
    uint16_t avail_idx;

    /* Dedicated DMA page for request header, status byte, and bounce fallback */
    uintptr_t req_phys;
    virtio_blk_req_hdr_t *req_hdr;
    volatile uint8_t *req_status;
    uint8_t *bounce;
    uintptr_t bounce_phys;

    /* IRQ */
    uint8_t irq_vec;
    int use_msix;
    int use_msi;

    spinlock_t lock;
    int busy;
    wait_queue_head_t avail_wq;
    wait_queue_head_t io_wq;

    uint32_t sector_size;
    uint64_t sector_count;
    block_dev_t bdev;
} virtio_blk_dev_t;

static virtio_blk_dev_t g_vblk_devs[VIRTIO_BLK_MAX_DEVS];
static int g_vblk_count;

static inline uint8_t mmio_read8(uintptr_t addr)
{
    return *(volatile uint8_t *)addr;
}

static inline uint16_t mmio_read16(uintptr_t addr)
{
    return *(volatile uint16_t *)addr;
}

static inline uint32_t mmio_read32(uintptr_t addr)
{
    return *(volatile uint32_t *)addr;
}

static inline void mmio_write8(uintptr_t addr, uint8_t val)
{
    *(volatile uint8_t *)addr = val;
}

static inline void mmio_write16(uintptr_t addr, uint16_t val)
{
    *(volatile uint16_t *)addr = val;
}

static inline void mmio_write32(uintptr_t addr, uint32_t val)
{
    *(volatile uint32_t *)addr = val;
}

static inline int vblk_irq_enabled_cpu(void)
{
    uint64_t rflags;
    __asm__ volatile ("pushfq\n\tpopq %0" : "=r"(rflags));
    return (rflags & (1ULL << 9)) != 0;
}

static void vblk_acquire(virtio_blk_dev_t *d)
{
    for (;;) {
        unsigned long flags = spin_lock_irqsave(&d->lock);
        if (!d->busy) {
            d->busy = 1;
            spin_unlock_irqrestore(&d->lock, flags);
            return;
        }
        spin_unlock_irqrestore(&d->lock, flags);

        process_t *self = process_current();
        if (vblk_irq_enabled_cpu() && self) {
            wait_queue_entry_t entry;
            entry.proc = self;
            entry.next = NULL;
            wait_queue_prepare_to_wait(&d->avail_wq, &entry);
            if (!d->busy) {
                wait_queue_finish_wait(&d->avail_wq, &entry);
                continue;
            }
            process_yield();
            wait_queue_finish_wait(&d->avail_wq, &entry);
        } else {
            __asm__ volatile ("pause");
        }
    }
}

static void vblk_release(virtio_blk_dev_t *d)
{
    unsigned long flags = spin_lock_irqsave(&d->lock);
    d->busy = 0;
    spin_unlock_irqrestore(&d->lock, flags);
    wait_queue_wake_all(&d->avail_wq);
}

/* Strictly non-blocking IRQ handler: ack ISR if INTx and wake io_wq. */
static void vblk_handle_irq(virtio_blk_dev_t *d)
{
    if (!d || !d->active)
        return;

    if (!d->use_msix && !d->use_msi) {
        if (d->is_modern)
            (void)mmio_read8(d->isr_cfg);
        else
            (void)io_inb((uint16_t)(d->io_base + VIO_LEG_ISR));
    }

    __asm__ volatile ("lfence" ::: "memory");
    if (d->used && d->used->idx != d->last_used_idx)
        wait_queue_wake_all(&d->io_wq);
}

static void vblk_vec_irq(uint8_t vec, void *ctx)
{
    (void)vec;
    vblk_handle_irq((virtio_blk_dev_t *)ctx);
}

static void vblk_pic_irq(uint8_t irq, void *ctx)
{
    (void)irq;
    vblk_handle_irq((virtio_blk_dev_t *)ctx);
}

static void vblk_notify_queue0(virtio_blk_dev_t *d)
{
    __asm__ volatile ("mfence" ::: "memory");
    if (d->is_modern) {
        uintptr_t addr = d->notify_base + (uintptr_t)d->q_notify_off * d->notify_off_mult;
        mmio_write16(addr, 0);
    } else {
        io_outw((uint16_t)(d->io_base + VIO_LEG_Q_NOTIFY), 0);
    }
}

static int vblk_wait_used(virtio_blk_dev_t *d)
{
    process_t *self = process_current();

    if (vblk_irq_enabled_cpu() && self) {
        wait_queue_entry_t entry;
        entry.proc = self;
        entry.next = NULL;

        for (;;) {
            __asm__ volatile ("lfence" ::: "memory");
            if (d->used->idx != d->last_used_idx)
                break;
            wait_queue_prepare_to_wait(&d->io_wq, &entry);
            __asm__ volatile ("lfence" ::: "memory");
            if (d->used->idx != d->last_used_idx) {
                wait_queue_finish_wait(&d->io_wq, &entry);
                break;
            }
            process_yield();
            wait_queue_finish_wait(&d->io_wq, &entry);
        }
    } else {
        uint32_t timeout = 20000000u;
        for (;;) {
            __asm__ volatile ("lfence" ::: "memory");
            if (d->used->idx != d->last_used_idx)
                break;
            if (timeout-- == 0)
                return -1;
            __asm__ volatile ("pause");
        }
    }

    d->last_used_idx = d->used->idx;
    return (*d->req_status == VIRTIO_BLK_S_OK) ? 0 : -1;
}

static int vblk_submit_rw(virtio_blk_dev_t *d, uint32_t req_type, uint64_t lba512,
                          uint32_t bytes, void *buf, int is_read)
{
    uint16_t desc_idx = 0;
    int use_bounce = 0;
    uint16_t status_desc;

    d->req_hdr->type = req_type;
    d->req_hdr->reserved = 0;
    d->req_hdr->sector = lba512;
    *d->req_status = 0xFFu;

    /* Descriptor 0: 16-byte virtio_blk request header (device-readable). */
    d->desc[desc_idx].addr = (uint64_t)d->req_phys;
    d->desc[desc_idx].len = sizeof(virtio_blk_req_hdr_t);
    d->desc[desc_idx].flags = VRING_DESC_F_NEXT;
    d->desc[desc_idx].next = (uint16_t)(desc_idx + 1u);
    desc_idx++;

    if (bytes > 0) {
        size_t rem = bytes;
        uintptr_t va = (uintptr_t)buf;
        uint16_t saved_idx = desc_idx;

        while (rem > 0) {
            if (desc_idx >= (uint16_t)(d->q_size - 1u) ||
                (desc_idx - saved_idx) >= VIRTIO_BLK_MAX_DATA_DESCS) {
                use_bounce = 1;
                break;
            }
            uint64_t pa = vmm_virt_to_phys(va);
            size_t page_off = va & 0xFFFULL;
            size_t chunk = 4096u - page_off;
            if (chunk > rem)
                chunk = rem;

            d->desc[desc_idx].addr = pa;
            d->desc[desc_idx].len = (uint32_t)chunk;
            d->desc[desc_idx].flags = VRING_DESC_F_NEXT | (is_read ? VRING_DESC_F_WRITE : 0u);
            d->desc[desc_idx].next = (uint16_t)(desc_idx + 1u);
            desc_idx++;
            va += chunk;
            rem -= chunk;
        }

        if (use_bounce) {
            if (!is_read)
                k_memcpy(d->bounce, buf, bytes);
            desc_idx = saved_idx;
            d->desc[desc_idx].addr = (uint64_t)d->bounce_phys;
            d->desc[desc_idx].len = bytes;
            d->desc[desc_idx].flags = VRING_DESC_F_NEXT | (is_read ? VRING_DESC_F_WRITE : 0u);
            d->desc[desc_idx].next = (uint16_t)(desc_idx + 1u);
            desc_idx++;
        }
    }

    /* Final descriptor: 1-byte status (device-writable). */
    status_desc = desc_idx;
    d->desc[status_desc].addr = (uint64_t)(d->req_phys + 64u);
    d->desc[status_desc].len = 1u;
    d->desc[status_desc].flags = VRING_DESC_F_WRITE;
    d->desc[status_desc].next = 0;

    d->avail->ring[d->avail_idx % d->q_size] = 0u;
    __asm__ volatile ("sfence" ::: "memory");
    d->avail_idx++;
    d->avail->idx = d->avail_idx;

    vblk_notify_queue0(d);

    int rc = vblk_wait_used(d);
    if (rc == 0 && use_bounce && is_read && bytes > 0)
        k_memcpy(buf, d->bounce, bytes);
    return rc;
}

static int vblk_bd_read(block_dev_t *bd, uint64_t lba, uint32_t count, void *buf)
{
    virtio_blk_dev_t *d = (virtio_blk_dev_t *)bd->drv_data;
    uint8_t *dst = (uint8_t *)buf;
    uint32_t sec_per_512 = d->sector_size / 512u;

    if (!d || !buf || count == 0 || sec_per_512 == 0)
        return -1;
    if (lba + count < lba || lba + count > bd->sector_count)
        return -1;

    vblk_acquire(d);
    while (count > 0) {
        uint32_t max_secs = VIRTIO_BLK_MAX_CHUNK_SECTORS / sec_per_512;
        if (max_secs == 0)
            max_secs = 1;
        uint32_t chunk = (count > max_secs) ? max_secs : count;
        if (vblk_submit_rw(d, VIRTIO_BLK_T_IN, lba * sec_per_512,
                           chunk * d->sector_size, dst, 1) != 0) {
            vblk_release(d);
            return -1;
        }
        lba += chunk;
        dst += (size_t)chunk * d->sector_size;
        count -= chunk;
    }
    vblk_release(d);
    return 0;
}

static int vblk_bd_write(block_dev_t *bd, uint64_t lba, uint32_t count, const void *buf)
{
    virtio_blk_dev_t *d = (virtio_blk_dev_t *)bd->drv_data;
    const uint8_t *src = (const uint8_t *)buf;
    uint32_t sec_per_512 = d->sector_size / 512u;

    if (!d || d->read_only || !buf || count == 0 || sec_per_512 == 0)
        return -1;
    if (lba + count < lba || lba + count > bd->sector_count)
        return -1;

    vblk_acquire(d);
    while (count > 0) {
        uint32_t max_secs = VIRTIO_BLK_MAX_CHUNK_SECTORS / sec_per_512;
        if (max_secs == 0)
            max_secs = 1;
        uint32_t chunk = (count > max_secs) ? max_secs : count;
        if (vblk_submit_rw(d, VIRTIO_BLK_T_OUT, lba * sec_per_512,
                           chunk * d->sector_size, (void *)src, 0) != 0) {
            vblk_release(d);
            return -1;
        }
        lba += chunk;
        src += (size_t)chunk * d->sector_size;
        count -= chunk;
    }
    vblk_release(d);
    return 0;
}

static int vblk_bd_sync(block_dev_t *bd)
{
    virtio_blk_dev_t *d = (virtio_blk_dev_t *)bd->drv_data;
    int rc = 0;

    if (!d)
        return -1;
    if (!d->has_flush)
        return 0;

    vblk_acquire(d);
    rc = vblk_submit_rw(d, VIRTIO_BLK_T_FLUSH, 0, 0, NULL, 0);
    vblk_release(d);
    return rc;
}

static int vblk_alloc_queue(virtio_blk_dev_t *d, uint16_t qsize)
{
    size_t desc_bytes = (size_t)qsize * sizeof(vq_desc_t);
    size_t avail_bytes = 6u + (size_t)qsize * sizeof(uint16_t);
    size_t used_off = (desc_bytes + avail_bytes + 4095u) & ~(size_t)4095u;
    size_t used_bytes = 6u + (size_t)qsize * sizeof(vq_used_elem_t);
    size_t total_pages = (used_off + used_bytes + 4095u) / 4096u;
    uintptr_t q_va;

    d->q_phys = pmm_alloc_pages(total_pages);
    if (!d->q_phys)
        return -1;

    q_va = vmm_phys_to_virt(d->q_phys);
    k_memset((void *)q_va, 0, total_pages * 4096u);

    d->q_size = qsize;
    d->desc = (vq_desc_t *)q_va;
    d->avail = (vq_avail_t *)(q_va + desc_bytes);
    d->used = (vq_used_t *)(q_va + used_off);
    d->last_used_idx = 0;
    d->avail_idx = 0;
    return 0;
}

static int vblk_setup_irq(const pci_device_info_t *dev, virtio_blk_dev_t *d)
{
    int vec = irq_alloc_vector();
    if (vec >= 0 && irq_register_vector_handler((uint8_t)vec, vblk_vec_irq, d) == 0) {
        uint8_t bsp = (uint8_t)lapic_read_id();
        d->irq_vec = (uint8_t)vec;

        if (d->is_modern && pci_enable_msix(dev, 0, d->irq_vec, bsp) == 0) {
            d->use_msix = 1;
            mmio_write16(d->common_cfg + VIO_COM_MSIX_CFG, 0xFFFFu);
            mmio_write16(d->common_cfg + VIO_COM_Q_SEL, 0);
            mmio_write16(d->common_cfg + VIO_COM_Q_MSIX_VEC, 0);
            return 0;
        }
        if (pci_enable_msi(dev, d->irq_vec, bsp) == 0) {
            d->use_msi = 1;
            return 0;
        }
        if (ioapic_active() && dev->irq_line != 0xFFu) {
            uint32_t gsi = acpi_irq_to_gsi(dev->irq_line);
            if (ioapic_route_gsi(gsi, d->irq_vec, bsp, 1, 1) == 0)
                return 0;
        }
        irq_unregister_vector_handler(d->irq_vec);
        irq_free_vector(d->irq_vec);
        d->irq_vec = 0;
    }

    if (!ioapic_active() && dev->irq_line < 16u) {
        irq_register_handler(dev->irq_line, vblk_pic_irq, d);
        return 0;
    }
    return -1;
}

static int vblk_init_modern(const pci_device_info_t *dev, virtio_blk_dev_t *d)
{
    uintptr_t bar_va[6] = {0};
    uint64_t bar_sz[6] = {0};
    int have_common = 0, have_notify = 0, have_isr = 0, have_dev = 0;

    uint16_t status = pci_read16(dev->bus, dev->device, dev->function, 0x06u);
    if (!(status & (1u << 4)))
        return -1;

    uint8_t ptr = (uint8_t)(pci_read8(dev->bus, dev->device, dev->function, 0x34u) & 0xFCu);
    for (int hop = 0; hop < 48 && ptr >= 0x40u; hop++) {
        uint8_t id = pci_read8(dev->bus, dev->device, dev->function, ptr);
        uint8_t next = (uint8_t)(pci_read8(dev->bus, dev->device, dev->function,
                                           (uint8_t)(ptr + 1u)) & 0xFCu);
        if (id == PCI_CAP_ID_VNDR) {
            uint8_t cap_len = pci_read8(dev->bus, dev->device, dev->function, (uint8_t)(ptr + 2u));
            uint8_t cfg_type = pci_read8(dev->bus, dev->device, dev->function, (uint8_t)(ptr + 3u));
            uint8_t bar = pci_read8(dev->bus, dev->device, dev->function, (uint8_t)(ptr + 4u));
            uint32_t off = pci_read32(dev->bus, dev->device, dev->function, (uint8_t)(ptr + 8u));
            uint32_t len = pci_read32(dev->bus, dev->device, dev->function, (uint8_t)(ptr + 12u));

            if (cap_len >= 16u && bar < 6u && cfg_type >= 1u && cfg_type <= 4u) {
                if (!bar_va[bar]) {
                    uint64_t base = 0, size = 0;
                    int is_mmio = 0;
                    if (pci_get_bar(dev, bar, &base, &size, &is_mmio) == 0 &&
                        is_mmio && base != 0) {
                        uintptr_t va = 0;
                        if (vmm_map_io_region(base, (size_t)size, &va) == 0 && va) {
                            bar_va[bar] = va;
                            bar_sz[bar] = size;
                        }
                    }
                }
                if (bar_va[bar] && (uint64_t)off + len <= bar_sz[bar]) {
                    uintptr_t addr = bar_va[bar] + off;
                    if (cfg_type == VIRTIO_PCI_CAP_COMMON_CFG) {
                        d->common_cfg = addr;
                        have_common = 1;
                    } else if (cfg_type == VIRTIO_PCI_CAP_NOTIFY_CFG && cap_len >= 20u) {
                        d->notify_base = addr;
                        d->notify_off_mult = pci_read32(dev->bus, dev->device,
                                                        dev->function, (uint8_t)(ptr + 16u));
                        have_notify = 1;
                    } else if (cfg_type == VIRTIO_PCI_CAP_ISR_CFG) {
                        d->isr_cfg = addr;
                        have_isr = 1;
                    } else if (cfg_type == VIRTIO_PCI_CAP_DEVICE_CFG) {
                        d->device_cfg = addr;
                        have_dev = 1;
                    }
                }
            }
        }
        if (next == ptr)
            break;
        ptr = next;
    }

    if (!have_common || !have_notify || !have_isr || !have_dev)
        return -1;

    pci_enable_mmio(dev);
    pci_enable_bus_mastering(dev);
    d->is_modern = 1;

    /* VirtIO 1.2 §3.1.1 initialization sequence. */
    mmio_write8(d->common_cfg + VIO_COM_STATUS, 0);
    while (mmio_read8(d->common_cfg + VIO_COM_STATUS) != 0)
        __asm__ volatile ("pause");

    mmio_write8(d->common_cfg + VIO_COM_STATUS, VIRTIO_STATUS_ACK);
    mmio_write8(d->common_cfg + VIO_COM_STATUS,
                VIRTIO_STATUS_ACK | VIRTIO_STATUS_DRIVER);

    mmio_write32(d->common_cfg + VIO_COM_DEV_FEAT_SEL, 0);
    uint32_t feat_lo = mmio_read32(d->common_cfg + VIO_COM_DEV_FEAT);
    mmio_write32(d->common_cfg + VIO_COM_DEV_FEAT_SEL, 1);
    uint32_t feat_hi = mmio_read32(d->common_cfg + VIO_COM_DEV_FEAT);

    if (!(feat_hi & VIRTIO_F_VERSION_1_HI)) {
        mmio_write8(d->common_cfg + VIO_COM_STATUS, VIRTIO_STATUS_FAILED);
        return -1;
    }

    d->read_only = (feat_lo & VIRTIO_BLK_F_RO) ? 1 : 0;
    d->has_flush = (feat_lo & VIRTIO_BLK_F_FLUSH) ? 1 : 0;

    uint32_t drv_lo = feat_lo & (VIRTIO_BLK_F_RO | VIRTIO_BLK_F_BLK_SIZE | VIRTIO_BLK_F_FLUSH);
    uint32_t drv_hi = VIRTIO_F_VERSION_1_HI;

    mmio_write32(d->common_cfg + VIO_COM_DRV_FEAT_SEL, 0);
    mmio_write32(d->common_cfg + VIO_COM_DRV_FEAT, drv_lo);
    mmio_write32(d->common_cfg + VIO_COM_DRV_FEAT_SEL, 1);
    mmio_write32(d->common_cfg + VIO_COM_DRV_FEAT, drv_hi);

    mmio_write8(d->common_cfg + VIO_COM_STATUS,
                VIRTIO_STATUS_ACK | VIRTIO_STATUS_DRIVER | VIRTIO_STATUS_FEATURES_OK);
    if (!(mmio_read8(d->common_cfg + VIO_COM_STATUS) & VIRTIO_STATUS_FEATURES_OK)) {
        mmio_write8(d->common_cfg + VIO_COM_STATUS, VIRTIO_STATUS_FAILED);
        return -1;
    }

    mmio_write16(d->common_cfg + VIO_COM_Q_SEL, 0);
    uint16_t qsize = mmio_read16(d->common_cfg + VIO_COM_Q_SIZE);
    if (qsize == 0) {
        mmio_write8(d->common_cfg + VIO_COM_STATUS, VIRTIO_STATUS_FAILED);
        return -1;
    }
    if (qsize > 128u) {
        qsize = 128u;
        mmio_write16(d->common_cfg + VIO_COM_Q_SIZE, qsize);
    }

    if (vblk_alloc_queue(d, qsize) != 0) {
        mmio_write8(d->common_cfg + VIO_COM_STATUS, VIRTIO_STATUS_FAILED);
        return -1;
    }

    size_t desc_bytes = (size_t)qsize * sizeof(vq_desc_t);
    size_t avail_bytes = 6u + (size_t)qsize * sizeof(uint16_t);
    size_t used_off = (desc_bytes + avail_bytes + 4095u) & ~(size_t)4095u;
    uint64_t desc_pa = (uint64_t)d->q_phys;
    uint64_t avail_pa = (uint64_t)(d->q_phys + desc_bytes);
    uint64_t used_pa = (uint64_t)(d->q_phys + used_off);

    mmio_write32(d->common_cfg + VIO_COM_Q_DESC_LO, (uint32_t)desc_pa);
    mmio_write32(d->common_cfg + VIO_COM_Q_DESC_HI, (uint32_t)(desc_pa >> 32));
    mmio_write32(d->common_cfg + VIO_COM_Q_AVAIL_LO, (uint32_t)avail_pa);
    mmio_write32(d->common_cfg + VIO_COM_Q_AVAIL_HI, (uint32_t)(avail_pa >> 32));
    mmio_write32(d->common_cfg + VIO_COM_Q_USED_LO, (uint32_t)used_pa);
    mmio_write32(d->common_cfg + VIO_COM_Q_USED_HI, (uint32_t)(used_pa >> 32));

    d->q_notify_off = mmio_read16(d->common_cfg + VIO_COM_Q_NOTIFY_OFF);
    vblk_setup_irq(dev, d);

    mmio_write16(d->common_cfg + VIO_COM_Q_SEL, 0);
    mmio_write16(d->common_cfg + VIO_COM_Q_ENABLE, 1);

    mmio_write8(d->common_cfg + VIO_COM_STATUS,
                VIRTIO_STATUS_ACK | VIRTIO_STATUS_DRIVER |
                VIRTIO_STATUS_FEATURES_OK | VIRTIO_STATUS_DRIVER_OK);

    uint64_t cap_lo = mmio_read32(d->device_cfg + 0x00u);
    uint64_t cap_hi = mmio_read32(d->device_cfg + 0x04u);
    uint64_t cap_512 = (cap_hi << 32) | cap_lo;

    d->sector_size = 512u;
    if (drv_lo & VIRTIO_BLK_F_BLK_SIZE) {
        uint32_t bsz = mmio_read32(d->device_cfg + 0x14u);
        if (bsz >= 512u && bsz <= 4096u && (bsz & (bsz - 1u)) == 0)
            d->sector_size = bsz;
    }
    d->sector_count = cap_512 / (d->sector_size / 512u);
    return 0;
}

static int vblk_init_legacy(const pci_device_info_t *dev, virtio_blk_dev_t *d)
{
    uint64_t base = 0, size = 0;
    int is_mmio = 0;

    if (pci_get_bar(dev, 0, &base, &size, &is_mmio) != 0 || is_mmio || base == 0)
        return -1;

    pci_enable_io(dev);
    pci_enable_bus_mastering(dev);
    d->is_modern = 0;
    d->io_base = (uint16_t)base;

    io_outb((uint16_t)(d->io_base + VIO_LEG_STATUS), 0);
    io_outb((uint16_t)(d->io_base + VIO_LEG_STATUS), VIRTIO_STATUS_ACK);
    io_outb((uint16_t)(d->io_base + VIO_LEG_STATUS),
            VIRTIO_STATUS_ACK | VIRTIO_STATUS_DRIVER);

    uint32_t feat = io_inl((uint16_t)(d->io_base + VIO_LEG_HOST_FEAT));
    d->read_only = (feat & VIRTIO_BLK_F_RO) ? 1 : 0;
    d->has_flush = (feat & VIRTIO_BLK_F_FLUSH) ? 1 : 0;
    uint32_t drv_feat = feat & (VIRTIO_BLK_F_RO | VIRTIO_BLK_F_BLK_SIZE | VIRTIO_BLK_F_FLUSH);
    io_outl((uint16_t)(d->io_base + VIO_LEG_GUEST_FEAT), drv_feat);

    io_outw((uint16_t)(d->io_base + VIO_LEG_Q_SEL), 0);
    uint16_t qsize = io_inw((uint16_t)(d->io_base + VIO_LEG_Q_SIZE));
    if (qsize == 0) {
        io_outb((uint16_t)(d->io_base + VIO_LEG_STATUS), VIRTIO_STATUS_FAILED);
        return -1;
    }

    if (vblk_alloc_queue(d, qsize) != 0) {
        io_outb((uint16_t)(d->io_base + VIO_LEG_STATUS), VIRTIO_STATUS_FAILED);
        return -1;
    }

    io_outl((uint16_t)(d->io_base + VIO_LEG_Q_PFN), (uint32_t)(d->q_phys >> 12));
    vblk_setup_irq(dev, d);

    io_outb((uint16_t)(d->io_base + VIO_LEG_STATUS),
            VIRTIO_STATUS_ACK | VIRTIO_STATUS_DRIVER | VIRTIO_STATUS_DRIVER_OK);

    uint64_t cap_lo = io_inl((uint16_t)(d->io_base + VIO_LEG_DEV_CFG + 0x00u));
    uint64_t cap_hi = io_inl((uint16_t)(d->io_base + VIO_LEG_DEV_CFG + 0x04u));
    uint64_t cap_512 = (cap_hi << 32) | cap_lo;

    d->sector_size = 512u;
    if (drv_feat & VIRTIO_BLK_F_BLK_SIZE) {
        uint32_t bsz = io_inl((uint16_t)(d->io_base + VIO_LEG_DEV_CFG + 0x14u));
        if (bsz >= 512u && bsz <= 4096u && (bsz & (bsz - 1u)) == 0)
            d->sector_size = bsz;
    }
    d->sector_count = cap_512 / (d->sector_size / 512u);
    return 0;
}

static int vblk_attach_device(const pci_device_info_t *dev)
{
    virtio_blk_dev_t *d;
    uintptr_t req_va;

    if (g_vblk_count >= VIRTIO_BLK_MAX_DEVS)
        return 0;

    d = &g_vblk_devs[g_vblk_count];
    k_memset(d, 0, sizeof(*d));

    d->req_phys = pmm_alloc_pages(1);
    d->bounce_phys = pmm_alloc_pages(16);
    if (!d->req_phys || !d->bounce_phys)
        return 0;

    req_va = vmm_phys_to_virt(d->req_phys);
    k_memset((void *)req_va, 0, PAGE_SIZE);
    d->req_hdr = (virtio_blk_req_hdr_t *)req_va;
    d->req_status = (volatile uint8_t *)(req_va + 64u);
    d->bounce = (uint8_t *)vmm_phys_to_virt(d->bounce_phys);

    d->lock = SPINLOCK_INIT;
    wait_queue_init(&d->avail_wq);
    wait_queue_init(&d->io_wq);

    if (vblk_init_modern(dev, d) != 0) {
        if (dev->device_id != VIRTIO_PCI_DEV_BLK_LEGACY ||
            vblk_init_legacy(dev, d) != 0) {
            return 0;
        }
    }

    if (d->sector_count == 0)
        return 0;

    d->active = 1;
    k_memset(&d->bdev, 0, sizeof(d->bdev));
    d->bdev.type = BLOCKDEV_TYPE_VIRTIO;
    k_strcpy(d->bdev.label, "VirtIO Block Device");
    d->bdev.sector_size = d->sector_size;
    d->bdev.sector_count = d->sector_count;
    d->bdev.read = vblk_bd_read;
    d->bdev.write = d->read_only ? NULL : vblk_bd_write;
    d->bdev.sync = vblk_bd_sync;
    d->bdev.drv_data = d;

    if (blockdev_register(&d->bdev) != 0) {
        d->active = 0;
        return 0;
    }

    g_vblk_count++;
    kprintf("[virtio-blk] %s (%s) sectors=%u bsz=%u %s vec=0x%x\n",
            d->bdev.name, d->is_modern ? "modern" : "legacy",
            (uint32_t)d->sector_count, d->sector_size,
            d->use_msix ? "MSI-X" : (d->use_msi ? "MSI" : "INTx"),
            d->irq_vec);
    return 1;
}

int virtio_blk_init(void)
{
    int total = 0;
    int count = pci_device_count();

    g_vblk_count = 0;
    for (int i = 0; i < count; i++) {
        const pci_device_info_t *dev = pci_device_at(i);
        if (!dev)
            continue;
        if (dev->vendor_id == VIRTIO_PCI_VENDOR_ID &&
            (dev->device_id == VIRTIO_PCI_DEV_BLK_LEGACY ||
             dev->device_id == VIRTIO_PCI_DEV_BLK_MODERN)) {
            total += vblk_attach_device(dev);
        }
    }
    return total;
}

#else

int virtio_blk_init(void) { return 0; }

#endif /* __x86_64__ */
