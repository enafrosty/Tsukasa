/*
 * Project Tsukasa — NVMe 2.0 PCIe storage driver
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

#include "nvme.h"
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
#define PCI_SUBCLASS_NVME      0x08u
#define PCI_PROGIF_NVME        0x02u

/* NVMe 2.0 Controller Register offsets (§3.1.3). */
#define NVME_REG_CAP_LO        0x0000u
#define NVME_REG_CAP_HI        0x0004u
#define NVME_REG_VS            0x0008u
#define NVME_REG_INTMS         0x000Cu
#define NVME_REG_INTMC         0x0010u
#define NVME_REG_CC            0x0014u
#define NVME_REG_CSTS          0x001Cu
#define NVME_REG_AQA           0x0024u
#define NVME_REG_ASQ_LO        0x0028u
#define NVME_REG_ASQ_HI        0x002Cu
#define NVME_REG_ACQ_LO        0x0030u
#define NVME_REG_ACQ_HI        0x0034u
#define NVME_REG_DBS           0x1000u

#define NVME_CC_EN             (1u << 0)
#define NVME_CC_CSS_NVM        (0u << 4)
#define NVME_CC_MPS_4K         (0u << 7)
#define NVME_CC_IOSQES_64      (6u << 16)
#define NVME_CC_IOCQES_16      (4u << 20)

#define NVME_CSTS_RDY          (1u << 0)
#define NVME_CSTS_CFS          (1u << 1)

/* Admin opcodes */
#define NVME_ADMIN_CREATE_SQ   0x01u
#define NVME_ADMIN_CREATE_CQ   0x05u
#define NVME_ADMIN_IDENTIFY    0x06u

/* NVM I/O opcodes */
#define NVME_CMD_FLUSH         0x00u
#define NVME_CMD_WRITE         0x01u
#define NVME_CMD_READ          0x02u

#define NVME_Q_DEPTH           64u
#define NVME_MAX_CTRLS         4
#define NVME_MAX_NS_PER_CTRL   4
#define NVME_MAX_CHUNK_BYTES   (64u * 1024u)
#define NVME_BOUNCE_PAGES      16

typedef struct {
    uint32_t cdw0;
    uint32_t nsid;
    uint64_t rsvd2;
    uint64_t mptr;
    uint64_t prp1;
    uint64_t prp2;
    uint32_t cdw10;
    uint32_t cdw11;
    uint32_t cdw12;
    uint32_t cdw13;
    uint32_t cdw14;
    uint32_t cdw15;
} __attribute__((packed)) nvme_sqe_t;

_Static_assert(sizeof(nvme_sqe_t) == 64, "NVMe SQE is 64 bytes");

typedef struct {
    uint32_t dw0;
    uint32_t dw1;
    uint16_t sq_head;
    uint16_t sq_id;
    uint16_t cid;
    uint16_t status;
} __attribute__((packed)) nvme_cqe_t;

_Static_assert(sizeof(nvme_cqe_t) == 16, "NVMe CQE is 16 bytes");

struct nvme_ctrl;

typedef struct nvme_queue {
    struct nvme_ctrl *ctrl;
    uint16_t qid;
    uint16_t depth;

    uintptr_t sq_phys;
    uintptr_t cq_phys;
    nvme_sqe_t *sq;
    volatile nvme_cqe_t *cq;

    uint16_t sq_tail;
    uint16_t cq_head;
    uint8_t cq_phase;
    uint16_t next_cid;

    spinlock_t lock;
    spinlock_t cq_lock;
    int busy;
    wait_queue_head_t avail_wq;
    wait_queue_head_t io_wq;

    volatile int req_done;
    volatile uint16_t req_status;
    volatile uint16_t req_cid;
} nvme_queue_t;

typedef struct nvme_ns {
    struct nvme_ctrl *ctrl;
    uint32_t nsid;
    uint32_t sector_size;
    uint64_t sector_count;
    block_dev_t bdev;
} nvme_ns_t;

typedef struct nvme_ctrl {
    int active;
    uint32_t ctrl_idx;
    uintptr_t regs;
    uint32_t dstrd;

    nvme_queue_t admin_q;
    nvme_queue_t io_q;

    uintptr_t prp_list_phys;
    uint64_t *prp_list;
    uintptr_t bounce_phys;
    uint8_t *bounce;

    uint8_t irq_vec;
    int use_msix;
    int use_msi;

    nvme_ns_t namespaces[NVME_MAX_NS_PER_CTRL];
    int ns_count;
} nvme_ctrl_t;

static nvme_ctrl_t g_nvme_ctrls[NVME_MAX_CTRLS];
static int g_nvme_ctrl_count;

static inline uint32_t nvme_read32(const nvme_ctrl_t *c, uint32_t off)
{
    return *(volatile uint32_t *)(c->regs + off);
}

static inline void nvme_write32(const nvme_ctrl_t *c, uint32_t off, uint32_t val)
{
    *(volatile uint32_t *)(c->regs + off) = val;
}

static inline uint64_t nvme_read64(const nvme_ctrl_t *c, uint32_t off)
{
    uint32_t lo = nvme_read32(c, off);
    uint32_t hi = nvme_read32(c, off + 4u);
    return ((uint64_t)hi << 32) | lo;
}

static inline void nvme_write64(const nvme_ctrl_t *c, uint32_t off, uint64_t val)
{
    nvme_write32(c, off, (uint32_t)(val & 0xFFFFFFFFu));
    nvme_write32(c, off + 4u, (uint32_t)(val >> 32));
}

static inline void nvme_ring_sq_db(const nvme_queue_t *q)
{
    uint32_t stride = 4u << q->ctrl->dstrd;
    uint32_t off = NVME_REG_DBS + (2u * q->qid) * stride;
    __asm__ volatile ("sfence" ::: "memory");
    nvme_write32(q->ctrl, off, q->sq_tail);
}

static inline void nvme_ring_cq_db(const nvme_queue_t *q)
{
    uint32_t stride = 4u << q->ctrl->dstrd;
    uint32_t off = NVME_REG_DBS + (2u * q->qid + 1u) * stride;
    nvme_write32(q->ctrl, off, q->cq_head);
}

static inline int nvme_irq_enabled_cpu(void)
{
    uint64_t rflags;
    __asm__ volatile ("pushfq\n\tpopq %0" : "=r"(rflags));
    return (rflags & (1ULL << 9)) != 0;
}

static void nvme_q_acquire(nvme_queue_t *q)
{
    for (;;) {
        unsigned long flags = spin_lock_irqsave(&q->lock);
        if (!q->busy) {
            q->busy = 1;
            spin_unlock_irqrestore(&q->lock, flags);
            return;
        }
        spin_unlock_irqrestore(&q->lock, flags);

        process_t *self = process_current();
        if (nvme_irq_enabled_cpu() && self) {
            wait_queue_entry_t entry;
            entry.proc = self;
            entry.next = NULL;
            wait_queue_prepare_to_wait(&q->avail_wq, &entry);
            if (!q->busy) {
                wait_queue_finish_wait(&q->avail_wq, &entry);
                continue;
            }
            process_yield();
            wait_queue_finish_wait(&q->avail_wq, &entry);
        } else {
            __asm__ volatile ("pause");
        }
    }
}

static void nvme_q_release(nvme_queue_t *q)
{
    unsigned long flags = spin_lock_irqsave(&q->lock);
    q->busy = 0;
    spin_unlock_irqrestore(&q->lock, flags);
    wait_queue_wake_all(&q->avail_wq);
}

/* Reap completed CQEs on queue `q`. Safe from both IRQ and polled paths. */
static int nvme_reap_cq(nvme_queue_t *q)
{
    int reaped = 0;
    unsigned long flags = spin_lock_irqsave(&q->cq_lock);

    for (;;) {
        volatile nvme_cqe_t *cqe = &q->cq[q->cq_head];
        uint16_t st = cqe->status;
        if ((st & 1u) != q->cq_phase)
            break;

        uint16_t cid = cqe->cid;
        q->cq_head++;
        if (q->cq_head >= q->depth) {
            q->cq_head = 0;
            q->cq_phase ^= 1u;
        }
        if (cid == q->req_cid) {
            q->req_status = (uint16_t)((st >> 1) & 0x7FFu);
            q->req_done = 1;
        }
        reaped++;
    }

    if (reaped > 0)
        nvme_ring_cq_db(q);

    spin_unlock_irqrestore(&q->cq_lock, flags);

    if (reaped > 0)
        wait_queue_wake_all(&q->io_wq);
    return reaped;
}

/* Strictly non-blocking IRQ handler: drain CQEs and wake waiting threads. */
static void nvme_handle_irq(nvme_ctrl_t *c)
{
    if (!c || !c->active)
        return;
    nvme_reap_cq(&c->admin_q);
    if (c->io_q.cq)
        nvme_reap_cq(&c->io_q);
}

static void nvme_vec_irq(uint8_t vec, void *ctx)
{
    (void)vec;
    nvme_handle_irq((nvme_ctrl_t *)ctx);
}

static void nvme_pic_irq(uint8_t irq, void *ctx)
{
    (void)irq;
    nvme_handle_irq((nvme_ctrl_t *)ctx);
}

static int nvme_submit_sync(nvme_queue_t *q, nvme_sqe_t *cmd)
{
    uint16_t cid;
    process_t *self;

    nvme_q_acquire(q);

    cid = q->next_cid++;
    cmd->cdw0 = (cmd->cdw0 & 0x0000FFFFu) | ((uint32_t)cid << 16);

    q->req_cid = cid;
    q->req_status = 0xFFFFu;
    q->req_done = 0;

    q->sq[q->sq_tail] = *cmd;
    q->sq_tail = (uint16_t)((q->sq_tail + 1u) % q->depth);
    nvme_ring_sq_db(q);

    self = process_current();
    if (nvme_irq_enabled_cpu() && self) {
        wait_queue_entry_t entry;
        entry.proc = self;
        entry.next = NULL;

        while (!q->req_done) {
            wait_queue_prepare_to_wait(&q->io_wq, &entry);
            if (q->req_done) {
                wait_queue_finish_wait(&q->io_wq, &entry);
                break;
            }
            process_yield();
            wait_queue_finish_wait(&q->io_wq, &entry);
        }
    } else {
        uint32_t timeout = 20000000u;
        while (!q->req_done) {
            nvme_reap_cq(q);
            if (q->req_done)
                break;
            if (timeout-- == 0) {
                nvme_q_release(q);
                return -1;
            }
            __asm__ volatile ("pause");
        }
    }

    int rc = (q->req_status == 0u) ? 0 : -1;
    nvme_q_release(q);
    return rc;
}

static int nvme_build_prps(nvme_ctrl_t *c, void *buf, uint32_t bytes,
                           int is_write, uint64_t *prp1_out, uint64_t *prp2_out,
                           int *used_bounce_out)
{
    uint8_t *dma_buf = (uint8_t *)buf;
    int used_bounce = 0;

    *prp1_out = 0;
    *prp2_out = 0;
    *used_bounce_out = 0;

    if (bytes == 0)
        return 0;

    /* NVMe 2.0 §4.3.1: PRP1 offset must be DWORD aligned (bits 1:0 == 0). */
    if (((uintptr_t)dma_buf & 3u) != 0) {
        used_bounce = 1;
        dma_buf = c->bounce;
        if (is_write)
            k_memcpy(dma_buf, buf, bytes);
    }

    uintptr_t va = (uintptr_t)dma_buf;
    size_t off0 = va & 0xFFFULL;
    size_t first_chunk = 4096u - off0;
    if (first_chunk > bytes)
        first_chunk = bytes;

    *prp1_out = vmm_virt_to_phys(va);
    size_t rem = bytes - first_chunk;
    va += first_chunk;

    if (rem == 0) {
        *prp2_out = 0;
    } else if (rem <= 4096u) {
        *prp2_out = vmm_virt_to_phys(va);
    } else {
        uint32_t list_idx = 0;
        while (rem > 0) {
            if (list_idx >= 512u)
                return -1;
            c->prp_list[list_idx++] = vmm_virt_to_phys(va);
            size_t step = (rem > 4096u) ? 4096u : rem;
            va += step;
            rem -= step;
        }
        *prp2_out = (uint64_t)c->prp_list_phys;
    }

    *used_bounce_out = used_bounce;
    return 0;
}

static int nvme_rw_chunk(nvme_ns_t *ns, uint8_t opc, uint64_t lba,
                         uint32_t count, void *buf, int is_write)
{
    nvme_ctrl_t *c = ns->ctrl;
    uint32_t bytes = count * ns->sector_size;
    uint64_t prp1 = 0, prp2 = 0;
    int used_bounce = 0;
    nvme_sqe_t cmd;

    /* Serialize bounce/PRP list usage alongside I/O queue submission. */
    nvme_q_acquire(&c->io_q);

    if (nvme_build_prps(c, buf, bytes, is_write, &prp1, &prp2, &used_bounce) != 0) {
        nvme_q_release(&c->io_q);
        return -1;
    }

    k_memset(&cmd, 0, sizeof(cmd));
    uint16_t cid = c->io_q.next_cid++;
    cmd.cdw0 = (uint32_t)opc | ((uint32_t)cid << 16);
    cmd.nsid = ns->nsid;
    cmd.prp1 = prp1;
    cmd.prp2 = prp2;
    cmd.cdw10 = (uint32_t)(lba & 0xFFFFFFFFu);
    cmd.cdw11 = (uint32_t)(lba >> 32);
    cmd.cdw12 = (uint32_t)(count - 1u);

    c->io_q.req_cid = cid;
    c->io_q.req_status = 0xFFFFu;
    c->io_q.req_done = 0;

    c->io_q.sq[c->io_q.sq_tail] = cmd;
    c->io_q.sq_tail = (uint16_t)((c->io_q.sq_tail + 1u) % c->io_q.depth);
    nvme_ring_sq_db(&c->io_q);

    process_t *self = process_current();
    if (nvme_irq_enabled_cpu() && self) {
        wait_queue_entry_t entry;
        entry.proc = self;
        entry.next = NULL;

        while (!c->io_q.req_done) {
            wait_queue_prepare_to_wait(&c->io_q.io_wq, &entry);
            if (c->io_q.req_done) {
                wait_queue_finish_wait(&c->io_q.io_wq, &entry);
                break;
            }
            process_yield();
            wait_queue_finish_wait(&c->io_q.io_wq, &entry);
        }
    } else {
        uint32_t timeout = 20000000u;
        while (!c->io_q.req_done) {
            nvme_reap_cq(&c->io_q);
            if (c->io_q.req_done)
                break;
            if (timeout-- == 0) {
                nvme_q_release(&c->io_q);
                return -1;
            }
            __asm__ volatile ("pause");
        }
    }

    int rc = (c->io_q.req_status == 0u) ? 0 : -1;
    if (rc == 0 && used_bounce && !is_write)
        k_memcpy(buf, c->bounce, bytes);

    nvme_q_release(&c->io_q);
    return rc;
}

static int nvme_bd_read(block_dev_t *bd, uint64_t lba, uint32_t count, void *buf)
{
    nvme_ns_t *ns = (nvme_ns_t *)bd->drv_data;
    uint8_t *dst = (uint8_t *)buf;
    uint32_t max_secs;

    if (!ns || !buf || count == 0 || ns->sector_size == 0)
        return -1;
    if (lba + count < lba || lba + count > bd->sector_count)
        return -1;

    max_secs = NVME_MAX_CHUNK_BYTES / ns->sector_size;
    if (max_secs == 0)
        max_secs = 1;

    while (count > 0) {
        uint32_t chunk = (count > max_secs) ? max_secs : count;
        if (nvme_rw_chunk(ns, NVME_CMD_READ, lba, chunk, dst, 0) != 0)
            return -1;
        lba += chunk;
        dst += (size_t)chunk * ns->sector_size;
        count -= chunk;
    }
    return 0;
}

static int nvme_bd_write(block_dev_t *bd, uint64_t lba, uint32_t count, const void *buf)
{
    nvme_ns_t *ns = (nvme_ns_t *)bd->drv_data;
    const uint8_t *src = (const uint8_t *)buf;
    uint32_t max_secs;

    if (!ns || !buf || count == 0 || ns->sector_size == 0)
        return -1;
    if (lba + count < lba || lba + count > bd->sector_count)
        return -1;

    max_secs = NVME_MAX_CHUNK_BYTES / ns->sector_size;
    if (max_secs == 0)
        max_secs = 1;

    while (count > 0) {
        uint32_t chunk = (count > max_secs) ? max_secs : count;
        if (nvme_rw_chunk(ns, NVME_CMD_WRITE, lba, chunk, (void *)src, 1) != 0)
            return -1;
        lba += chunk;
        src += (size_t)chunk * ns->sector_size;
        count -= chunk;
    }
    return 0;
}

static int nvme_bd_sync(block_dev_t *bd)
{
    nvme_ns_t *ns = (nvme_ns_t *)bd->drv_data;
    nvme_sqe_t cmd;

    if (!ns)
        return -1;

    k_memset(&cmd, 0, sizeof(cmd));
    cmd.cdw0 = NVME_CMD_FLUSH;
    cmd.nsid = ns->nsid;
    return nvme_submit_sync(&ns->ctrl->io_q, &cmd);
}

static int nvme_init_queue_struct(nvme_ctrl_t *c, nvme_queue_t *q,
                                  uint16_t qid, uint16_t depth)
{
    k_memset(q, 0, sizeof(*q));
    q->ctrl = c;
    q->qid = qid;
    q->depth = depth;
    q->cq_phase = 1u;
    q->next_cid = 1u;
    q->lock = SPINLOCK_INIT;
    q->cq_lock = SPINLOCK_INIT;
    wait_queue_init(&q->avail_wq);
    wait_queue_init(&q->io_wq);

    q->sq_phys = pmm_alloc_pages(1);
    q->cq_phys = pmm_alloc_pages(1);
    if (!q->sq_phys || !q->cq_phys)
        return -1;

    q->sq = (nvme_sqe_t *)vmm_phys_to_virt(q->sq_phys);
    q->cq = (volatile nvme_cqe_t *)vmm_phys_to_virt(q->cq_phys);
    k_memset(q->sq, 0, PAGE_SIZE);
    k_memset((void *)q->cq, 0, PAGE_SIZE);
    return 0;
}

static int nvme_fmt_name(char *out, uint32_t ctrl_idx, uint32_t nsid)
{
    char tmp[16];
    int p = 0, t = 0;
    uint32_t v;

    out[p++] = 'n';
    out[p++] = 'v';
    out[p++] = 'm';
    out[p++] = 'e';

    v = ctrl_idx;
    if (v == 0) {
        out[p++] = '0';
    } else {
        while (v > 0 && t < 12) {
            tmp[t++] = (char)('0' + (v % 10u));
            v /= 10u;
        }
        while (t > 0 && p < BLOCKDEV_NAME_MAX - 4)
            out[p++] = tmp[--t];
    }

    out[p++] = 'n';
    t = 0;
    v = nsid;
    if (v == 0) {
        out[p++] = '0';
    } else {
        while (v > 0 && t < 12) {
            tmp[t++] = (char)('0' + (v % 10u));
            v /= 10u;
        }
        while (t > 0 && p < BLOCKDEV_NAME_MAX - 1)
            out[p++] = tmp[--t];
    }
    out[p] = '\0';
    return 0;
}

static int nvme_attach_controller(const pci_device_info_t *dev)
{
    uint64_t bar_phys = 0, bar_size = 0;
    int is_mmio = 0;
    uintptr_t regs = 0;
    nvme_ctrl_t *c;
    uint64_t cap;
    uint32_t cc, timeout;
    uintptr_t id_phys;
    uint8_t *id_buf;
    int vec;
    int registered = 0;

    if (g_nvme_ctrl_count >= NVME_MAX_CTRLS)
        return 0;

    if (pci_get_bar(dev, 0, &bar_phys, &bar_size, &is_mmio) != 0 ||
        !is_mmio || !bar_phys)
        return 0;

    if (bar_size < 0x2000u)
        bar_size = 0x2000u;

    if (vmm_map_io_region(bar_phys, (size_t)bar_size, &regs) != 0 || !regs)
        return 0;

    pci_enable_mmio(dev);
    pci_enable_bus_mastering(dev);

    c = &g_nvme_ctrls[g_nvme_ctrl_count];
    k_memset(c, 0, sizeof(*c));
    c->ctrl_idx = (uint32_t)g_nvme_ctrl_count;
    c->regs = regs;

    cap = nvme_read64(c, NVME_REG_CAP_LO);
    c->dstrd = (uint32_t)((cap >> 32) & 0xFu);

    /* Disable controller and wait CSTS.RDY == 0. */
    cc = nvme_read32(c, NVME_REG_CC);
    if (cc & NVME_CC_EN) {
        nvme_write32(c, NVME_REG_CC, cc & ~NVME_CC_EN);
        timeout = 10000000u;
        while ((nvme_read32(c, NVME_REG_CSTS) & NVME_CSTS_RDY) && timeout--)
            __asm__ volatile ("pause");
        if (timeout == 0)
            return 0;
    }

    if (nvme_init_queue_struct(c, &c->admin_q, 0, NVME_Q_DEPTH) != 0)
        return 0;

    c->prp_list_phys = pmm_alloc_pages(1);
    c->bounce_phys = pmm_alloc_pages(NVME_BOUNCE_PAGES);
    id_phys = pmm_alloc_pages(1);
    if (!c->prp_list_phys || !c->bounce_phys || !id_phys)
        return 0;

    c->prp_list = (uint64_t *)vmm_phys_to_virt(c->prp_list_phys);
    c->bounce = (uint8_t *)vmm_phys_to_virt(c->bounce_phys);
    id_buf = (uint8_t *)vmm_phys_to_virt(id_phys);

    nvme_write32(c, NVME_REG_AQA,
                 ((NVME_Q_DEPTH - 1u) << 16) | (NVME_Q_DEPTH - 1u));
    nvme_write64(c, NVME_REG_ASQ_LO, (uint64_t)c->admin_q.sq_phys);
    nvme_write64(c, NVME_REG_ACQ_LO, (uint64_t)c->admin_q.cq_phys);

    cc = NVME_CC_IOCQES_16 | NVME_CC_IOSQES_64 |
         NVME_CC_MPS_4K | NVME_CC_CSS_NVM | NVME_CC_EN;
    nvme_write32(c, NVME_REG_CC, cc);

    timeout = 10000000u;
    while (!(nvme_read32(c, NVME_REG_CSTS) & NVME_CSTS_RDY) && timeout--)
        __asm__ volatile ("pause");
    if (timeout == 0 || (nvme_read32(c, NVME_REG_CSTS) & NVME_CSTS_CFS))
        return 0;

    c->active = 1;

    vec = irq_alloc_vector();
    if (vec >= 0 && irq_register_vector_handler((uint8_t)vec, nvme_vec_irq, c) == 0) {
        uint8_t bsp = (uint8_t)lapic_read_id();
        c->irq_vec = (uint8_t)vec;
        if (pci_enable_msix(dev, 0, c->irq_vec, bsp) == 0) {
            c->use_msix = 1;
        } else if (pci_enable_msi(dev, c->irq_vec, bsp) == 0) {
            c->use_msi = 1;
            nvme_write32(c, NVME_REG_INTMC, 0xFFFFFFFFu);
        } else if (ioapic_active() && dev->irq_line != 0xFFu) {
            uint32_t gsi = acpi_irq_to_gsi(dev->irq_line);
            if (ioapic_route_gsi(gsi, c->irq_vec, bsp, 1, 1) == 0) {
                nvme_write32(c, NVME_REG_INTMC, 0xFFFFFFFFu);
            } else {
                irq_unregister_vector_handler(c->irq_vec);
                irq_free_vector(c->irq_vec);
                c->irq_vec = 0;
            }
        } else {
            irq_unregister_vector_handler(c->irq_vec);
            irq_free_vector(c->irq_vec);
            c->irq_vec = 0;
        }
    }

    if (!c->use_msix && !c->use_msi && !ioapic_active() && dev->irq_line < 16u) {
        irq_register_handler(dev->irq_line, nvme_pic_irq, c);
        nvme_write32(c, NVME_REG_INTMC, 0xFFFFFFFFu);
    }

    /* Admin Identify Controller (CNS=1). */
    nvme_sqe_t cmd;
    k_memset(id_buf, 0, PAGE_SIZE);
    k_memset(&cmd, 0, sizeof(cmd));
    cmd.cdw0 = NVME_ADMIN_IDENTIFY;
    cmd.prp1 = (uint64_t)id_phys;
    cmd.cdw10 = 1u;
    if (nvme_submit_sync(&c->admin_q, &cmd) != 0) {
        c->active = 0;
        pmm_free(id_phys);
        return 0;
    }

    uint32_t nn = *(const uint32_t *)(id_buf + 516u);
    if (nn == 0)
        nn = 1;
    if (nn > NVME_MAX_NS_PER_CTRL)
        nn = NVME_MAX_NS_PER_CTRL;

    /* Create I/O CQ (QID=1) and I/O SQ (QID=1). */
    if (nvme_init_queue_struct(c, &c->io_q, 1u, NVME_Q_DEPTH) != 0) {
        c->active = 0;
        pmm_free(id_phys);
        return 0;
    }

    k_memset(&cmd, 0, sizeof(cmd));
    cmd.cdw0 = NVME_ADMIN_CREATE_CQ;
    cmd.prp1 = (uint64_t)c->io_q.cq_phys;
    cmd.cdw10 = ((NVME_Q_DEPTH - 1u) << 16) | 1u;
    cmd.cdw11 = (0u << 16) | (1u << 1) | 1u; /* IV=0, IEN=1, PC=1 */
    if (nvme_submit_sync(&c->admin_q, &cmd) != 0) {
        c->active = 0;
        pmm_free(id_phys);
        return 0;
    }

    k_memset(&cmd, 0, sizeof(cmd));
    cmd.cdw0 = NVME_ADMIN_CREATE_SQ;
    cmd.prp1 = (uint64_t)c->io_q.sq_phys;
    cmd.cdw10 = ((NVME_Q_DEPTH - 1u) << 16) | 1u;
    cmd.cdw11 = (1u << 16) | 1u; /* CQID=1, PC=1 */
    if (nvme_submit_sync(&c->admin_q, &cmd) != 0) {
        c->active = 0;
        pmm_free(id_phys);
        return 0;
    }

    kprintf("[nvme] ctrl%u %02x:%02x.%u BAR0=0x%x%08x %s vec=0x%x nn=%u\n",
            c->ctrl_idx, dev->bus, dev->device, dev->function,
            (uint32_t)(bar_phys >> 32), (uint32_t)bar_phys,
            c->use_msix ? "MSI-X" : (c->use_msi ? "MSI" : "INTx"),
            c->irq_vec, nn);

    for (uint32_t nsid = 1; nsid <= nn; nsid++) {
        k_memset(id_buf, 0, PAGE_SIZE);
        k_memset(&cmd, 0, sizeof(cmd));
        cmd.cdw0 = NVME_ADMIN_IDENTIFY;
        cmd.nsid = nsid;
        cmd.prp1 = (uint64_t)id_phys;
        cmd.cdw10 = 0u; /* CNS=0: Identify Namespace */
        if (nvme_submit_sync(&c->admin_q, &cmd) != 0)
            continue;

        uint64_t nsze = *(const uint64_t *)(id_buf + 0u);
        if (nsze == 0)
            continue;

        uint8_t flbas = id_buf[26];
        uint8_t lbaf_idx = (uint8_t)(flbas & 0x0Fu);
        uint32_t lbaf = *(const uint32_t *)(id_buf + 128u + (size_t)lbaf_idx * 4u);
        uint8_t lbads = (uint8_t)((lbaf >> 16) & 0xFFu);
        uint32_t ssz = (lbads >= 9u && lbads <= 12u) ? (1u << lbads) : 512u;

        nvme_ns_t *ns = &c->namespaces[c->ns_count++];
        k_memset(ns, 0, sizeof(*ns));
        ns->ctrl = c;
        ns->nsid = nsid;
        ns->sector_size = ssz;
        ns->sector_count = nsze;

        ns->bdev.type = BLOCKDEV_TYPE_NVME;
        nvme_fmt_name(ns->bdev.name, c->ctrl_idx, nsid);
        k_strcpy(ns->bdev.label, "NVMe Namespace");
        ns->bdev.sector_size = ssz;
        ns->bdev.sector_count = nsze;
        ns->bdev.read = nvme_bd_read;
        ns->bdev.write = nvme_bd_write;
        ns->bdev.sync = nvme_bd_sync;
        ns->bdev.drv_data = ns;

        if (blockdev_register(&ns->bdev) == 0) {
            registered++;
            kprintf("[nvme] ready %s: sectors=%u bsz=%u (%u MiB)\n",
                    ns->bdev.name, (uint32_t)nsze, ssz,
                    (uint32_t)((nsze * (ssz / 512u)) / 2048u));
        }
    }

    pmm_free(id_phys);
    if (registered > 0)
        g_nvme_ctrl_count++;
    else
        c->active = 0;

    return registered;
}

int nvme_init(void)
{
    int total = 0;
    int count = pci_device_count();

    g_nvme_ctrl_count = 0;
    for (int i = 0; i < count; i++) {
        const pci_device_info_t *dev = pci_device_at(i);
        if (!dev)
            continue;
        if (dev->class_code == PCI_CLASS_MASS_STORAGE &&
            dev->subclass == PCI_SUBCLASS_NVME &&
            dev->prog_if == PCI_PROGIF_NVME) {
            total += nvme_attach_controller(dev);
        }
    }
    return total;
}

#else

int nvme_init(void) { return 0; }

#endif /* __x86_64__ */
