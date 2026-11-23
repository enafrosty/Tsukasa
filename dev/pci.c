/*
 * Project Tsukasa - PCI config-space scan, ECAM, BAR decode, capabilities, MSI/MSI-X
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

#include "pci.h"

#include "../drv/acpi.h"
#include "../include/io.h"
#include "../include/kprintf.h"
#include "../include/spinlock.h"
#include "../mm/vmm_x64.h"

#include <stddef.h>

#ifdef __x86_64__

#define PCI_CONFIG_ADDRESS 0xCF8u
#define PCI_CONFIG_DATA    0xCFCu
#define PCI_ECAM_MAX_RANGES 8

typedef struct pci_driver_slot {
    int used;
    pci_driver_t driver;
} pci_driver_slot_t;

typedef struct pci_ecam_range {
    uint64_t base_addr;
    uint8_t  start_bus;
    uint8_t  end_bus;
} pci_ecam_range_t;

static pci_device_info_t g_pci_devices[PCI_MAX_DEVICES];
static int g_pci_device_count;
static pci_driver_slot_t g_pci_drivers[PCI_MAX_DRIVERS];
static int g_pci_driver_count;
static spinlock_t g_pci_lock = SPINLOCK_INIT;
static spinlock_t g_pci_cfg_lock = SPINLOCK_INIT;
static spinlock_t g_cf8_lock = SPINLOCK_INIT;
static spinlock_t g_ecam_lock = SPINLOCK_INIT;

static pci_ecam_range_t g_ecam_ranges[PCI_ECAM_MAX_RANGES];
static int g_ecam_range_count;
static uintptr_t g_ecam_bus_virt[256];

static int kstrcmp(const char *a, const char *b)
{
    int i = 0;
    if (!a || !b)
        return (a == b) ? 0 : 1;
    while (a[i] && b[i] && a[i] == b[i])
        i++;
    return (unsigned char)a[i] - (unsigned char)b[i];
}

static void kstrcpy_cap(char *dst, const char *src, int cap)
{
    int i = 0;
    if (!dst || cap <= 0)
        return;
    if (!src) {
        dst[0] = '\0';
        return;
    }
    while (src[i] && i < cap - 1) {
        dst[i] = src[i];
        i++;
    }
    dst[i] = '\0';
}

static void pci_ecam_init(void)
{
    g_ecam_range_count = 0;
    for (int i = 0; i < 256; i++)
        __atomic_store_n(&g_ecam_bus_virt[i], 0u, __ATOMIC_RELAXED);

    int mcfg_cnt = acpi_get_mcfg_count();
    for (int i = 0; i < mcfg_cnt && g_ecam_range_count < PCI_ECAM_MAX_RANGES; i++) {
        uint64_t base = 0;
        uint16_t seg = 0;
        uint8_t start_bus = 0;
        uint8_t end_bus = 0;
        if (acpi_get_mcfg_info(i, &base, &seg, &start_bus, &end_bus) != 0)
            continue;
        if (seg != 0 || !base || end_bus < start_bus)
            continue;
        g_ecam_ranges[g_ecam_range_count].base_addr = base;
        g_ecam_ranges[g_ecam_range_count].start_bus = start_bus;
        g_ecam_ranges[g_ecam_range_count].end_bus = end_bus;
        g_ecam_range_count++;
    }

    if (g_ecam_range_count > 0)
        kprintf("[pci] ECAM enabled (ranges=%d)\n", g_ecam_range_count);
    else
        kprintf("[pci] ECAM absent, using 0xCF8/0xCFC\n");
}

int pci_ecam_active(void)
{
    return g_ecam_range_count > 0;
}

static volatile uint32_t *pci_ecam_reg_ptr(uint8_t bus, uint8_t device,
                                           uint8_t function, uint16_t offset)
{
    if (g_ecam_range_count == 0 || device >= 32u || function >= 8u || offset >= 4096u)
        return NULL;

    const pci_ecam_range_t *range = NULL;
    for (int i = 0; i < g_ecam_range_count; i++) {
        if (bus >= g_ecam_ranges[i].start_bus && bus <= g_ecam_ranges[i].end_bus) {
            range = &g_ecam_ranges[i];
            break;
        }
    }
    if (!range)
        return NULL;

    uintptr_t bus_va = __atomic_load_n(&g_ecam_bus_virt[bus], __ATOMIC_ACQUIRE);
    if (!bus_va) {
        unsigned long flags = spin_lock_irqsave(&g_ecam_lock);
        bus_va = g_ecam_bus_virt[bus];
        if (!bus_va) {
            uint64_t bus_phys = range->base_addr + ((uint64_t)(bus - range->start_bus) << 20);
            uintptr_t mapped = 0;
            if (vmm_map_io_region(bus_phys, (size_t)(1u << 20), &mapped) == 0 && mapped) {
                bus_va = mapped;
                __atomic_store_n(&g_ecam_bus_virt[bus], bus_va, __ATOMIC_RELEASE);
            }
        }
        spin_unlock_irqrestore(&g_ecam_lock, flags);
        if (!bus_va)
            return NULL;
    }

    return (volatile uint32_t *)(bus_va +
                                 ((uintptr_t)device << 15) +
                                 ((uintptr_t)function << 12) +
                                 ((uintptr_t)offset & 0xFFCu));
}

static uint32_t pci_cfg_addr(uint8_t bus, uint8_t device, uint8_t function, uint8_t offset)
{
    return (uint32_t)(0x80000000u |
                      ((uint32_t)bus << 16) |
                      ((uint32_t)device << 11) |
                      ((uint32_t)function << 8) |
                      ((uint32_t)offset & 0xFCu));
}

uint32_t pci_read_ext32(uint8_t bus, uint8_t device, uint8_t function, uint16_t offset)
{
    if (device >= 32u || function >= 8u || offset >= 4096u)
        return 0xFFFFFFFFu;

    volatile uint32_t *ecam = pci_ecam_reg_ptr(bus, device, function, offset);
    if (ecam)
        return *ecam;

    if (offset >= 256u)
        return 0xFFFFFFFFu;

    unsigned long flags = spin_lock_irqsave(&g_cf8_lock);
    io_outl(PCI_CONFIG_ADDRESS, pci_cfg_addr(bus, device, function, (uint8_t)offset));
    uint32_t val = io_inl(PCI_CONFIG_DATA);
    spin_unlock_irqrestore(&g_cf8_lock, flags);
    return val;
}

void pci_write_ext32(uint8_t bus, uint8_t device, uint8_t function, uint16_t offset, uint32_t value)
{
    if (device >= 32u || function >= 8u || offset >= 4096u)
        return;

    volatile uint32_t *ecam = pci_ecam_reg_ptr(bus, device, function, offset);
    if (ecam) {
        *ecam = value;
        return;
    }

    if (offset >= 256u)
        return;

    unsigned long flags = spin_lock_irqsave(&g_cf8_lock);
    io_outl(PCI_CONFIG_ADDRESS, pci_cfg_addr(bus, device, function, (uint8_t)offset));
    io_outl(PCI_CONFIG_DATA, value);
    spin_unlock_irqrestore(&g_cf8_lock, flags);
}

uint32_t pci_read32(uint8_t bus, uint8_t device, uint8_t function, uint8_t offset)
{
    return pci_read_ext32(bus, device, function, offset);
}

uint16_t pci_read16(uint8_t bus, uint8_t device, uint8_t function, uint8_t offset)
{
    uint32_t value = pci_read32(bus, device, function, offset);
    return (uint16_t)((value >> ((offset & 2u) * 8u)) & 0xFFFFu);
}

uint8_t pci_read8(uint8_t bus, uint8_t device, uint8_t function, uint8_t offset)
{
    uint32_t value = pci_read32(bus, device, function, offset);
    return (uint8_t)((value >> ((offset & 3u) * 8u)) & 0xFFu);
}

void pci_write32(uint8_t bus, uint8_t device, uint8_t function, uint8_t offset, uint32_t value)
{
    pci_write_ext32(bus, device, function, offset, value);
}

static void pci_write16_unlocked(uint8_t bus, uint8_t device, uint8_t function, uint8_t offset, uint16_t value)
{
    uint32_t aligned = pci_read32(bus, device, function, (uint8_t)(offset & 0xFCu));
    uint32_t shift = (uint32_t)((offset & 2u) * 8u);
    uint32_t mask = (uint32_t)(0xFFFFu << shift);
    uint32_t merged = (aligned & ~mask) | ((uint32_t)value << shift);
    pci_write32(bus, device, function, (uint8_t)(offset & 0xFCu), merged);
}

void pci_write16(uint8_t bus, uint8_t device, uint8_t function, uint8_t offset, uint16_t value)
{
    unsigned long flags = spin_lock_irqsave(&g_pci_cfg_lock);
    pci_write16_unlocked(bus, device, function, offset, value);
    spin_unlock_irqrestore(&g_pci_cfg_lock, flags);
}

static int pci_device_present(uint8_t bus, uint8_t device, uint8_t function)
{
    return pci_read16(bus, device, function, 0x00) != 0xFFFFu;
}

static void pci_fill_device(pci_device_info_t *out, uint8_t bus, uint8_t device, uint8_t function)
{
    uint32_t class_rev = pci_read32(bus, device, function, 0x08);
    uint32_t hdr = pci_read32(bus, device, function, 0x0C);
    uint16_t irq = pci_read16(bus, device, function, 0x3C);

    if (!out)
        return;

    out->bus = bus;
    out->device = device;
    out->function = function;
    out->vendor_id = pci_read16(bus, device, function, 0x00);
    out->device_id = pci_read16(bus, device, function, 0x02);
    out->revision = (uint8_t)(class_rev & 0xFFu);
    out->prog_if = (uint8_t)((class_rev >> 8) & 0xFFu);
    out->subclass = (uint8_t)((class_rev >> 16) & 0xFFu);
    out->class_code = (uint8_t)((class_rev >> 24) & 0xFFu);
    out->header_type = (uint8_t)((hdr >> 16) & 0xFFu);
    out->irq_line = (uint8_t)(irq & 0xFFu);
    out->irq_pin = (uint8_t)((irq >> 8) & 0xFFu);
    out->bound_driver[0] = '\0';

    for (int i = 0; i < 6; i++)
        out->bars[i] = pci_read32(bus, device, function, (uint8_t)(0x10 + i * 4));
}

static int pci_scan_locked(void)
{
    int count = 0;
    for (uint16_t bus = 0; bus < 256 && count < PCI_MAX_DEVICES; bus++) {
        for (uint8_t dev = 0; dev < 32 && count < PCI_MAX_DEVICES; dev++) {
            uint8_t max_fn = 1;
            if (!pci_device_present((uint8_t)bus, dev, 0))
                continue;

            if (pci_read8((uint8_t)bus, dev, 0, 0x0E) & 0x80u)
                max_fn = 8;

            for (uint8_t fn = 0; fn < max_fn && count < PCI_MAX_DEVICES; fn++) {
                if (!pci_device_present((uint8_t)bus, dev, fn))
                    continue;
                pci_fill_device(&g_pci_devices[count], (uint8_t)bus, dev, fn);
                count++;
            }
        }
    }
    g_pci_device_count = count;
    return count;
}

int pci_rescan(void)
{
    int count;
    spin_lock(&g_pci_lock);
    count = pci_scan_locked();
    spin_unlock(&g_pci_lock);
    return count;
}

void pci_init(void)
{
    pci_ecam_init();
    int count = pci_rescan();
    kprintf("[pci] discovered devices=%d\n", count);
}

int pci_device_count(void)
{
    return g_pci_device_count;
}

const pci_device_info_t *pci_device_at(int index)
{
    if (index < 0 || index >= g_pci_device_count)
        return NULL;
    return &g_pci_devices[index];
}

const pci_device_info_t *pci_find_device(uint16_t vendor_id, uint16_t device_id)
{
    for (int i = 0; i < g_pci_device_count; i++) {
        const pci_device_info_t *dev = &g_pci_devices[i];
        if (dev->vendor_id == vendor_id && dev->device_id == device_id)
            return dev;
    }
    return NULL;
}

const pci_device_info_t *pci_find_class(uint8_t class_code, uint8_t subclass)
{
    for (int i = 0; i < g_pci_device_count; i++) {
        const pci_device_info_t *dev = &g_pci_devices[i];
        if (dev->class_code == class_code && dev->subclass == subclass)
            return dev;
    }
    return NULL;
}

int pci_register_driver(const pci_driver_t *driver)
{
    if (!driver || !driver->name || !driver->match || !driver->attach)
        return -1;

    spin_lock(&g_pci_lock);
    for (int i = 0; i < g_pci_driver_count; i++) {
        if (g_pci_drivers[i].used &&
            kstrcmp(g_pci_drivers[i].driver.name, driver->name) == 0) {
            g_pci_drivers[i].driver = *driver;
            spin_unlock(&g_pci_lock);
            return 0;
        }
    }

    if (g_pci_driver_count >= PCI_MAX_DRIVERS) {
        spin_unlock(&g_pci_lock);
        return -1;
    }

    g_pci_drivers[g_pci_driver_count].used = 1;
    g_pci_drivers[g_pci_driver_count].driver = *driver;
    g_pci_driver_count++;
    spin_unlock(&g_pci_lock);
    return 0;
}

int pci_probe_and_attach(void)
{
    int attached = 0;
    spin_lock(&g_pci_lock);
    for (int di = 0; di < g_pci_device_count; di++) {
        pci_device_info_t *dev = &g_pci_devices[di];
        for (int i = 0; i < g_pci_driver_count; i++) {
            const pci_driver_t *drv;
            if (!g_pci_drivers[i].used)
                continue;
            drv = &g_pci_drivers[i].driver;
            if (drv->match(dev) != 0)
                continue;
            if (drv->attach(dev) == 0) {
                kstrcpy_cap(dev->bound_driver, drv->name, (int)sizeof(dev->bound_driver));
                attached++;
                break;
            }
        }
    }
    spin_unlock(&g_pci_lock);
    return attached;
}

static void pci_cmd_set_bits(const pci_device_info_t *dev, uint16_t bits)
{
    if (!dev)
        return;
    unsigned long flags = spin_lock_irqsave(&g_pci_cfg_lock);
    uint16_t cmd = pci_read16(dev->bus, dev->device, dev->function, 0x04u);
    cmd |= bits;
    pci_write16_unlocked(dev->bus, dev->device, dev->function, 0x04u, cmd);
    spin_unlock_irqrestore(&g_pci_cfg_lock, flags);
}

void pci_enable_bus_mastering(const pci_device_info_t *dev)
{
    pci_cmd_set_bits(dev, (1u << 2));
}

void pci_enable_io(const pci_device_info_t *dev)
{
    pci_cmd_set_bits(dev, (1u << 0));
}

void pci_enable_mmio(const pci_device_info_t *dev)
{
    pci_cmd_set_bits(dev, (1u << 1));
}

int pci_get_bar(const pci_device_info_t *dev, int bar_idx,
                uint64_t *base_out, uint64_t *size_out, int *is_mmio_out)
{
    if (!dev || bar_idx < 0)
        return -1;

    uint8_t htype = dev->header_type & 0x7Fu;
    int max_bars = (htype == 0x00u) ? 6 : ((htype == 0x01u) ? 2 : 0);
    if (bar_idx >= max_bars)
        return -1;

    if (base_out)
        *base_out = 0;
    if (size_out)
        *size_out = 0;
    if (is_mmio_out)
        *is_mmio_out = 0;

    uint8_t off = (uint8_t)(0x10u + bar_idx * 4);
    unsigned long flags = spin_lock_irqsave(&g_pci_cfg_lock);

    uint32_t bar_lo = pci_read32(dev->bus, dev->device, dev->function, off);
    if (bar_lo == 0xFFFFFFFFu) {
        spin_unlock_irqrestore(&g_pci_cfg_lock, flags);
        return -1;
    }

    int is_io = (bar_lo & 0x1u) != 0;
    int is_64 = (!is_io) && ((bar_lo & 0x6u) == 0x4u);

    if (!is_io && ((bar_lo & 0x6u) == 0x2u || (bar_lo & 0x6u) == 0x6u)) {
        spin_unlock_irqrestore(&g_pci_cfg_lock, flags);
        return -1;
    }
    if (is_64 && bar_idx + 1 >= max_bars) {
        spin_unlock_irqrestore(&g_pci_cfg_lock, flags);
        return -1;
    }

    uint32_t bar_hi = is_64
        ? pci_read32(dev->bus, dev->device, dev->function, (uint8_t)(off + 4u))
        : 0u;

    uint16_t cmd = pci_read16(dev->bus, dev->device, dev->function, 0x04u);
    pci_write16_unlocked(dev->bus, dev->device, dev->function, 0x04u,
                         (uint16_t)(cmd & ~(uint16_t)0x3u));

    pci_write32(dev->bus, dev->device, dev->function, off, 0xFFFFFFFFu);
    uint32_t mask_lo = pci_read32(dev->bus, dev->device, dev->function, off);
    pci_write32(dev->bus, dev->device, dev->function, off, bar_lo);

    uint32_t mask_hi = 0u;
    if (is_64) {
        pci_write32(dev->bus, dev->device, dev->function, (uint8_t)(off + 4u), 0xFFFFFFFFu);
        mask_hi = pci_read32(dev->bus, dev->device, dev->function, (uint8_t)(off + 4u));
        pci_write32(dev->bus, dev->device, dev->function, (uint8_t)(off + 4u), bar_hi);
    }

    pci_write16_unlocked(dev->bus, dev->device, dev->function, 0x04u, cmd);
    spin_unlock_irqrestore(&g_pci_cfg_lock, flags);

    uint64_t base = 0;
    uint64_t size = 0;
    if (is_io) {
        base = (uint64_t)(bar_lo & ~0x3u);
        uint32_t m = mask_lo & ~0x3u;
        if (m != 0u && m != 0xFFFFFFFFu) {
            if ((m & 0xFFFF0000u) == 0u)
                m |= 0xFFFF0000u;
            size = (uint64_t)((~m) + 1u);
        }
    } else if (is_64) {
        base = ((uint64_t)bar_hi << 32) | (uint64_t)(bar_lo & ~0xFu);
        uint64_t m = ((uint64_t)mask_hi << 32) | (uint64_t)(mask_lo & ~0xFu);
        if (m != 0ULL)
            size = (~m) + 1ULL;
    } else {
        base = (uint64_t)(bar_lo & ~0xFu);
        uint32_t m = mask_lo & ~0xFu;
        if (m != 0u)
            size = (uint64_t)((~m) + 1u);
    }

    if (size == 0ULL)
        return -1;

    if (base_out)
        *base_out = base;
    if (size_out)
        *size_out = size;
    if (is_mmio_out)
        *is_mmio_out = is_io ? 0 : 1;
    return 0;
}

uint8_t pci_find_capability(const pci_device_info_t *dev, uint8_t cap_id)
{
    if (!dev)
        return 0;

    uint8_t htype = dev->header_type & 0x7Fu;
    if (htype != 0x00u && htype != 0x01u)
        return 0;

    uint16_t status = pci_read16(dev->bus, dev->device, dev->function, 0x06u);
    if (!(status & (1u << 4)))
        return 0;

    uint8_t ptr = (uint8_t)(pci_read8(dev->bus, dev->device, dev->function, 0x34u) & 0xFCu);
    for (int hop = 0; hop < 48 && ptr >= 0x40u; hop++) {
        uint8_t id = pci_read8(dev->bus, dev->device, dev->function, ptr);
        if (id == cap_id)
            return ptr;
        uint8_t next = (uint8_t)(pci_read8(dev->bus, dev->device, dev->function, (uint8_t)(ptr + 1u)) & 0xFCu);
        if (next == ptr)
            break;
        ptr = next;
    }
    return 0;
}

int pci_enable_msi(const pci_device_info_t *dev, uint8_t vector, uint8_t dest_lapic_id)
{
    if (!dev)
        return -1;

    uint8_t cap = pci_find_capability(dev, PCI_CAP_ID_MSI);
    if (!cap)
        return -1;

    unsigned long flags = spin_lock_irqsave(&g_pci_cfg_lock);
    uint16_t ctrl = pci_read16(dev->bus, dev->device, dev->function, (uint8_t)(cap + 2u));
    uint32_t msg_addr = 0xFEE00000u | ((uint32_t)dest_lapic_id << 12);
    pci_write32(dev->bus, dev->device, dev->function, (uint8_t)(cap + 4u), msg_addr);

    if (ctrl & (1u << 7)) {
        pci_write32(dev->bus, dev->device, dev->function, (uint8_t)(cap + 8u), 0u);
        pci_write16_unlocked(dev->bus, dev->device, dev->function, (uint8_t)(cap + 12u), (uint16_t)vector);
        if (ctrl & (1u << 8))
            pci_write32(dev->bus, dev->device, dev->function, (uint8_t)(cap + 16u), 0u);
    } else {
        pci_write16_unlocked(dev->bus, dev->device, dev->function, (uint8_t)(cap + 8u), (uint16_t)vector);
        if (ctrl & (1u << 8))
            pci_write32(dev->bus, dev->device, dev->function, (uint8_t)(cap + 12u), 0u);
    }

    ctrl = (uint16_t)((ctrl & ~(uint16_t)(0x7u << 4)) | 0x1u);
    pci_write16_unlocked(dev->bus, dev->device, dev->function, (uint8_t)(cap + 2u), ctrl);

    uint16_t cmd = pci_read16(dev->bus, dev->device, dev->function, 0x04u);
    cmd |= (1u << 10);
    pci_write16_unlocked(dev->bus, dev->device, dev->function, 0x04u, cmd);

    spin_unlock_irqrestore(&g_pci_cfg_lock, flags);
    return 0;
}

int pci_enable_msix(const pci_device_info_t *dev, uint16_t entry_idx,
                    uint8_t vector, uint8_t dest_lapic_id)
{
    if (!dev)
        return -1;

    uint8_t cap = pci_find_capability(dev, PCI_CAP_ID_MSIX);
    if (!cap)
        return -1;

    uint16_t ctrl = pci_read16(dev->bus, dev->device, dev->function, (uint8_t)(cap + 2u));
    uint16_t table_size = (uint16_t)((ctrl & 0x7FFu) + 1u);
    if (entry_idx >= table_size)
        return -1;

    uint32_t table_reg = pci_read32(dev->bus, dev->device, dev->function, (uint8_t)(cap + 4u));
    uint8_t bir = (uint8_t)(table_reg & 0x7u);
    uint32_t table_off = table_reg & ~0x7u;

    uint64_t bar_base = 0;
    uint64_t bar_size = 0;
    int is_mmio = 0;
    if (pci_get_bar(dev, (int)bir, &bar_base, &bar_size, &is_mmio) != 0 || !is_mmio || !bar_base)
        return -1;
    if ((uint64_t)table_off + (uint64_t)table_size * 16ULL > bar_size)
        return -1;

    uintptr_t table_va = 0;
    if (vmm_map_io_region(bar_base + (uint64_t)table_off,
                          (size_t)table_size * 16u, &table_va) != 0 || !table_va)
        return -1;

    unsigned long flags = spin_lock_irqsave(&g_pci_cfg_lock);
    uint16_t cmd = pci_read16(dev->bus, dev->device, dev->function, 0x04u);
    cmd |= (uint16_t)((1u << 1) | (1u << 10));
    pci_write16_unlocked(dev->bus, dev->device, dev->function, 0x04u, cmd);

    volatile uint32_t *entry = (volatile uint32_t *)(table_va + (uintptr_t)entry_idx * 16u);
    entry[0] = 0xFEE00000u | ((uint32_t)dest_lapic_id << 12);
    entry[1] = 0u;
    entry[2] = (uint32_t)vector;
    entry[3] = 0u;

    ctrl = pci_read16(dev->bus, dev->device, dev->function, (uint8_t)(cap + 2u));
    ctrl = (uint16_t)((ctrl & ~(uint16_t)(1u << 14)) | (1u << 15));
    pci_write16_unlocked(dev->bus, dev->device, dev->function, (uint8_t)(cap + 2u), ctrl);

    spin_unlock_irqrestore(&g_pci_cfg_lock, flags);
    return 0;
}

#else

void pci_init(void) {}
int pci_rescan(void) { return 0; }
int pci_device_count(void) { return 0; }
const pci_device_info_t *pci_device_at(int index) { (void)index; return NULL; }
const pci_device_info_t *pci_find_device(uint16_t vendor_id, uint16_t device_id)
{
    (void)vendor_id;
    (void)device_id;
    return NULL;
}
const pci_device_info_t *pci_find_class(uint8_t class_code, uint8_t subclass)
{
    (void)class_code;
    (void)subclass;
    return NULL;
}
int pci_register_driver(const pci_driver_t *driver) { (void)driver; return -1; }
int pci_probe_and_attach(void) { return 0; }
uint8_t pci_read8(uint8_t bus, uint8_t device, uint8_t function, uint8_t offset)
{
    (void)bus; (void)device; (void)function; (void)offset;
    return 0xFF;
}
uint16_t pci_read16(uint8_t bus, uint8_t device, uint8_t function, uint8_t offset)
{
    (void)bus; (void)device; (void)function; (void)offset;
    return 0xFFFF;
}
uint32_t pci_read32(uint8_t bus, uint8_t device, uint8_t function, uint8_t offset)
{
    (void)bus; (void)device; (void)function; (void)offset;
    return 0xFFFFFFFFu;
}
uint32_t pci_read_ext32(uint8_t bus, uint8_t device, uint8_t function, uint16_t offset)
{
    (void)bus; (void)device; (void)function; (void)offset;
    return 0xFFFFFFFFu;
}
void pci_write16(uint8_t bus, uint8_t device, uint8_t function, uint8_t offset, uint16_t value)
{
    (void)bus; (void)device; (void)function; (void)offset; (void)value;
}
void pci_write32(uint8_t bus, uint8_t device, uint8_t function, uint8_t offset, uint32_t value)
{
    (void)bus; (void)device; (void)function; (void)offset; (void)value;
}
void pci_write_ext32(uint8_t bus, uint8_t device, uint8_t function, uint16_t offset, uint32_t value)
{
    (void)bus; (void)device; (void)function; (void)offset; (void)value;
}
int pci_ecam_active(void) { return 0; }
int pci_get_bar(const pci_device_info_t *dev, int bar_idx,
                uint64_t *base_out, uint64_t *size_out, int *is_mmio_out)
{
    (void)dev; (void)bar_idx; (void)base_out; (void)size_out; (void)is_mmio_out;
    return -1;
}
uint8_t pci_find_capability(const pci_device_info_t *dev, uint8_t cap_id)
{
    (void)dev; (void)cap_id;
    return 0;
}
int pci_enable_msi(const pci_device_info_t *dev, uint8_t vector, uint8_t dest_lapic_id)
{
    (void)dev; (void)vector; (void)dest_lapic_id;
    return -1;
}
int pci_enable_msix(const pci_device_info_t *dev, uint16_t entry_idx,
                    uint8_t vector, uint8_t dest_lapic_id)
{
    (void)dev; (void)entry_idx; (void)vector; (void)dest_lapic_id;
    return -1;
}
void pci_enable_bus_mastering(const pci_device_info_t *dev) { (void)dev; }
void pci_enable_io(const pci_device_info_t *dev) { (void)dev; }
void pci_enable_mmio(const pci_device_info_t *dev) { (void)dev; }

#endif /* __x86_64__ */
