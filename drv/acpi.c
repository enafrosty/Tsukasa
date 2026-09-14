/*
 * Project Tsukasa — ACPI: RSDP/XSDT/RSDT walk, FADT/MADT parse, S5 poweroff
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

#include <stddef.h>
#include <stdint.h>

#include "acpi.h"
#include "../include/boot_info.h"
#include "../include/io.h"
#include "../include/kprintf.h"
#include "../mm/vmm_x64.h"

static const struct acpi_rsdp *g_rsdp;
static const acpi_fadt_t *g_fadt;
static const struct acpi_madt *g_madt;
static int g_acpi_ok;

static uint64_t g_lapic_base;

static struct {
    uint8_t  id;
    uint32_t addr;
    uint32_t gsi_base;
} g_ioapics[ACPI_MAX_IOAPIC];
static int g_ioapic_count;

static struct {
    uint8_t  source;
    uint32_t gsi;
    uint16_t flags;
} g_iso_table[ACPI_MAX_ISO];
static int g_iso_count;

/* _S5_ sleep type values, pre-shifted into PM1 SLP_TYP position (bits 10-12). */
static uint16_t g_slp_typa;
static uint16_t g_slp_typb;
static int g_s5_from_dsdt;

/* Freestanding helpers (no libc; lib/ has no memcmp). */
static int bytes_equal(const void *a, const void *b, size_t n)
{
    const uint8_t *pa = (const uint8_t *)a;
    const uint8_t *pb = (const uint8_t *)b;
    for (size_t i = 0; i < n; i++) {
        if (pa[i] != pb[i])
            return 0;
    }
    return 1;
}

/* Map an ACPI physical range into the kernel address space and return its HHDM-consistent virtual address. */
static const void *acpi_map(uint64_t phys, size_t size)
{
    uintptr_t virt = 0;
    if (!phys || !size)
        return NULL;
    if (vmm_map_io_region(phys, size, &virt) != 0)
        return NULL;
    return (const void *)virt;
}

/* Map a full SDT: header first (to learn its length), then the whole table. */
static const struct acpi_sdt *acpi_map_sdt(uint64_t phys)
{
    const struct acpi_sdt *hdr =
        (const struct acpi_sdt *)acpi_map(phys, sizeof(struct acpi_sdt));
    if (!hdr)
        return NULL;
    if (hdr->length < sizeof(struct acpi_sdt))
        return NULL;
    return (const struct acpi_sdt *)acpi_map(phys, hdr->length);
}

/* Every ACPI table carries an 8-bit checksum: all bytes of the table (including the checksum byte) must sum... */
static int acpi_checksum(const void *ptr, size_t len)
{
    const uint8_t *p = (const uint8_t *)ptr;
    uint8_t sum = 0;
    for (size_t i = 0; i < len; i++)
        sum += p[i];
    return sum == 0;
}

/* Find a table by signature. */
static const struct acpi_sdt *acpi_get_sdt(const char signature[4])
{
    if (!g_rsdp)
        return NULL;

    if (g_rsdp->revision >= 2 && g_rsdp->xsdt_address) {
        const struct acpi_xsdt *xsdt =
            (const struct acpi_xsdt *)acpi_map_sdt(g_rsdp->xsdt_address);
        if (xsdt && acpi_checksum(xsdt, xsdt->header.length)) {
            size_t entries = (xsdt->header.length - sizeof(struct acpi_sdt)) / 8;
            for (size_t i = 0; i < entries; i++) {
                const struct acpi_sdt *tbl = (const struct acpi_sdt *)
                    acpi_map(xsdt->tables[i], sizeof(struct acpi_sdt));
                if (!tbl)
                    continue;
                if (bytes_equal(tbl->signature, signature, 4)) {
                    tbl = acpi_map_sdt(xsdt->tables[i]);
                    if (!tbl || !acpi_checksum(tbl, tbl->length)) {
                        kprintf("[acpi] ERROR: %c%c%c%c checksum bad (xsdt)\n",
                                signature[0], signature[1], signature[2], signature[3]);
                        return NULL;
                    }
                    return tbl;
                }
            }
        } else if (xsdt) {
            kprintf("[acpi] ERROR: XSDT checksum bad, trying RSDT\n");
        }
    }

    if (!g_rsdp->rsdt_address)
        return NULL;

    const struct acpi_sdt *rsdt = acpi_map_sdt(g_rsdp->rsdt_address);
    if (!rsdt || !acpi_checksum(rsdt, rsdt->length)) {
        kprintf("[acpi] ERROR: RSDT checksum bad\n");
        return NULL;
    }

    const uint32_t *tables =
        (const uint32_t *)((const uint8_t *)rsdt + sizeof(struct acpi_sdt));
    size_t entries = (rsdt->length - sizeof(struct acpi_sdt)) / 4;
    for (size_t i = 0; i < entries; i++) {
        const struct acpi_sdt *tbl = (const struct acpi_sdt *)
            acpi_map((uint64_t)tables[i], sizeof(struct acpi_sdt));
        if (!tbl)
            continue;
        if (bytes_equal(tbl->signature, signature, 4)) {
            tbl = acpi_map_sdt((uint64_t)tables[i]);
            if (!tbl || !acpi_checksum(tbl, tbl->length)) {
                kprintf("[acpi] ERROR: %c%c%c%c checksum bad (rsdt)\n",
                        signature[0], signature[1], signature[2], signature[3]);
                return NULL;
            }
            return tbl;
        }
    }

    return NULL;
}

/* DSDT via FADT: prefer the 64-bit x_dsdt when the FADT is long enough to carry it (x_dsdt sits at byte... */
static const struct acpi_sdt *acpi_get_dsdt(void)
{
    if (!g_fadt)
        return NULL;
    if (g_fadt->header.length >= 148 && g_fadt->x_dsdt)
        return acpi_map_sdt(g_fadt->x_dsdt);
    if (g_fadt->dsdt)
        return acpi_map_sdt((uint64_t)g_fadt->dsdt);
    return NULL;
}

static void acpi_parse_s5(void)
{
    const struct acpi_sdt *dsdt = acpi_get_dsdt();
    if (!dsdt)
        return;
    if (!bytes_equal(dsdt->signature, "DSDT", 4))
        return;
    if (dsdt->length < sizeof(struct acpi_sdt) + 5)
        return;

    const uint8_t *ptr = (const uint8_t *)dsdt + sizeof(struct acpi_sdt);
    const uint8_t *end = (const uint8_t *)dsdt + dsdt->length;

    while (ptr + 4 + 5 < end) {
        if (bytes_equal(ptr, "_S5_", 4)) {
            ptr += 4;
            if (*ptr == 0x12) {
                ptr += 3;
                if (*ptr == 0x0A)
                    ptr++;
                g_slp_typa = (uint16_t)(*ptr << 10);
                ptr++;
                if (*ptr == 0x0A)
                    ptr++;
                g_slp_typb = (uint16_t)(*ptr << 10);
                g_s5_from_dsdt = 1;
                return;
            }
        }
        ptr++;
    }
}

int acpi_available(void)
{
    return g_acpi_ok;
}

uint64_t acpi_get_lapic_base(void)
{
    return g_lapic_base;
}

int acpi_get_ioapic_count(void)
{
    return g_ioapic_count;
}

int acpi_get_ioapic_info(int idx, uint8_t *id_out, uint32_t *addr_out,
                         uint32_t *gsi_base_out)
{
    if (idx < 0 || idx >= g_ioapic_count)
        return -1;
    if (id_out)
        *id_out = g_ioapics[idx].id;
    if (addr_out)
        *addr_out = g_ioapics[idx].addr;
    if (gsi_base_out)
        *gsi_base_out = g_ioapics[idx].gsi_base;
    return 0;
}

uint32_t acpi_irq_to_gsi(uint32_t irq)
{
    for (int i = 0; i < g_iso_count; i++) {
        if (g_iso_table[i].source == irq)
            return g_iso_table[i].gsi;
    }
    return irq;
}

uint16_t acpi_irq_flags(uint32_t irq)
{
    for (int i = 0; i < g_iso_count; i++) {
        if (g_iso_table[i].source == irq)
            return g_iso_table[i].flags;
    }
    return 0;
}

void acpi_power_off(void)
{
    kprintf("[acpi] entering S5 (SLP_TYPa=0x%x SLP_TYPb=0x%x %s)\n",
            g_slp_typa, g_slp_typb,
            g_s5_from_dsdt ? "from DSDT" : "fallback guess");

    if (g_slp_typa == 0) {
        
        g_slp_typa = (uint16_t)(5u << 10);
    }

    if (g_fadt && g_fadt->pm1a_cnt_blk) {
        io_outw((uint16_t)g_fadt->pm1a_cnt_blk,
                (uint16_t)(g_slp_typa | ACPI_PM1_SLP_EN));
        if (g_fadt->pm1b_cnt_blk)
            io_outw((uint16_t)g_fadt->pm1b_cnt_blk,
                    (uint16_t)(g_slp_typb | ACPI_PM1_SLP_EN));
    }

    io_outw(0xB004, 0x2000);
    io_outw(0x4004, 0x3400);
    io_outw(0x604,  0x2000);
    io_outw(0x600,  0x34);

    kprintf("[acpi] S5 request did not power off; halting\n");
    __asm__ volatile ("cli");
    for (;;)
        __asm__ volatile ("hlt");
}

int acpi_init(const struct tsukasa_boot_info *boot_info)
{
    if (!boot_info || !boot_info->rsdp_addr) {
        kprintf("[acpi] no RSDP from bootloader, ACPI disabled\n");
        return -1;
    }

    const struct acpi_rsdp *rsdp = (const struct acpi_rsdp *)
        acpi_map(boot_info->rsdp_addr, sizeof(struct acpi_rsdp));
    if (!rsdp || !bytes_equal(rsdp->signature, "RSD PTR ", 8)) {
        kprintf("[acpi] ERROR: bad RSDP signature, ACPI disabled\n");
        return -1;
    }

    size_t rsdp_len = (rsdp->revision >= 2) ? rsdp->length : 20u;
    if (rsdp_len < 20u)
        rsdp_len = 20u;
    if (!acpi_checksum(rsdp, rsdp_len)) {
        kprintf("[acpi] ERROR: RSDP checksum bad, ACPI disabled\n");
        return -1;
    }
    g_rsdp = rsdp;

    kprintf("[acpi] RSDP rev=%u using %s\n", rsdp->revision,
            (rsdp->revision >= 2 && rsdp->xsdt_address) ? "XSDT" : "RSDT");

    g_fadt = (const acpi_fadt_t *)acpi_get_sdt("FACP");
    if (!g_fadt) {
        kprintf("[acpi] ERROR: FADT not found, ACPI disabled\n");
        g_rsdp = NULL;
        return -1;
    }
    kprintf("[acpi] FADT smi_cmd=0x%x acpi_enable=0x%x pm1a_cnt=0x%x pm1b_cnt=0x%x\n",
            g_fadt->smi_cmd, g_fadt->acpi_enable,
            g_fadt->pm1a_cnt_blk, g_fadt->pm1b_cnt_blk);

    if (g_fadt->pm1a_cnt_blk &&
        (io_inw((uint16_t)g_fadt->pm1a_cnt_blk) & ACPI_PM1_SCI_EN)) {
        kprintf("[acpi] already in ACPI mode\n");
    } else if (g_fadt->smi_cmd && g_fadt->acpi_enable) {
        io_outb((uint16_t)g_fadt->smi_cmd, g_fadt->acpi_enable);
        int timeout = 1000000;
        while (g_fadt->pm1a_cnt_blk &&
               !(io_inw((uint16_t)g_fadt->pm1a_cnt_blk) & ACPI_PM1_SCI_EN) &&
               timeout-- > 0)
            __asm__ volatile ("pause");
        if (timeout <= 0)
            kprintf("[acpi] WARN: SCI_EN never set after enable write\n");
        else
            kprintf("[acpi] ACPI mode enabled via SMI\n");
    } else {
        kprintf("[acpi] no SMI enable handshake (smi_cmd=0)\n");
    }

    g_madt = (const struct acpi_madt *)acpi_get_sdt("APIC");
    if (g_madt) {
        g_lapic_base = g_madt->lapic_addr;

        const uint8_t *ptr = g_madt->entries;
        const uint8_t *end = (const uint8_t *)g_madt + g_madt->header.length;
        int lapic_count = 0;

        while (ptr + sizeof(struct madt_entry_header) <= end) {
            const struct madt_entry_header *h =
                (const struct madt_entry_header *)ptr;
            if (h->length < sizeof(struct madt_entry_header))
                break;
            if (ptr + h->length > end)
                break;

            switch (h->type) {
            case 0:
                if (h->length >= sizeof(struct madt_lapic))
                    lapic_count++;
                break;
            case 1:
                if (h->length >= sizeof(struct madt_ioapic) &&
                    g_ioapic_count < ACPI_MAX_IOAPIC) {
                    const struct madt_ioapic *io =
                        (const struct madt_ioapic *)ptr;
                    g_ioapics[g_ioapic_count].id = io->ioapic_id;
                    g_ioapics[g_ioapic_count].addr = io->ioapic_addr;
                    g_ioapics[g_ioapic_count].gsi_base = io->gsi_base;
                    kprintf("[acpi] IOAPIC id=%u addr=0x%x gsi_base=%u\n",
                            io->ioapic_id, io->ioapic_addr, io->gsi_base);
                    g_ioapic_count++;
                }
                break;
            case 2:
                if (h->length >= sizeof(struct madt_iso) &&
                    g_iso_count < ACPI_MAX_ISO) {
                    const struct madt_iso *iso = (const struct madt_iso *)ptr;
                    g_iso_table[g_iso_count].source = iso->source;
                    g_iso_table[g_iso_count].gsi = iso->gsi;
                    g_iso_table[g_iso_count].flags = iso->flags;
                    kprintf("[acpi] ISO irq%u -> gsi%u flags=0x%x\n",
                            iso->source, iso->gsi, iso->flags);
                    g_iso_count++;
                }
                break;
            default:
                break;
            }

            ptr += h->length;
        }

        kprintf("[acpi] MADT lapic_base=0x%x%s lapics=%d ioapics=%d isos=%d\n",
                g_madt->lapic_addr,
                (g_madt->lapic_addr == 0xFEE00000u) ? " (matches default)" : " (OVERRIDE)",
                lapic_count, g_ioapic_count, g_iso_count);
    } else {
        kprintf("[acpi] WARN: MADT not found (LAPIC keeps 0xFEE00000 default)\n");
    }

    acpi_parse_s5();
    if (g_s5_from_dsdt)
        kprintf("[acpi] _S5_ SLP_TYPa=0x%x SLP_TYPb=0x%x\n",
                g_slp_typa, g_slp_typb);
    else
        kprintf("[acpi] WARN: _S5_ not found in DSDT (poweroff will guess)\n");

    g_acpi_ok = 1;
    kprintf("[guide07] acpi ready: rsdp rev=%u ioapics=%d isos=%d s5=%s\n",
            rsdp->revision, g_ioapic_count, g_iso_count,
            g_s5_from_dsdt ? "dsdt" : "fallback");
    return 0;
}
