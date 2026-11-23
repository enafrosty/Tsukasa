/*
 * Project Tsukasa — I/O APIC driver: MADT-routed device interrupt delivery
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

/* redirection-table programming below is standard, spec-defined boilerplate written for Tsukasa against the... */

#include <stddef.h>
#include <stdint.h>

#include "ioapic.h"
#include "acpi.h"
#include "pic.h"
#include "../include/kprintf.h"
#include "../include/lapic.h"
#include "../include/spinlock.h"
#include "../mm/vmm_x64.h"

/* Register access is indirect: write the register index to IOREGSEL (byte offset 0x00), then read/write IOWIN (0x10). */
#define IOAPIC_REG_ID      0x00u
#define IOAPIC_REG_VER     0x01u
#define IOAPIC_REG_REDTBL  0x10u  /* entry n: low dword 0x10+2n, high 0x10+2n+1 */
#define IOAPIC_MAX_ENTRIES 120u

/* Redirection entry, low dword (82093AA §3.2.4): bits 0-7 vector, 8-10 delivery mode (000 = fixed). */
#define IOAPIC_LOW_POLARITY_LOW  (1u << 13)
#define IOAPIC_LOW_TRIGGER_LEVEL (1u << 15)
#define IOAPIC_LOW_MASKED        (1u << 16)

struct ioapic_unit {
    volatile uint32_t *base;
    uint8_t  id;
    uint32_t gsi_base;
    uint32_t entry_count;
};

static struct ioapic_unit g_ioapics[ACPI_MAX_IOAPIC];
static int g_ioapic_count;
static int g_active;
static spinlock_t g_ioapic_lock = SPINLOCK_INIT;

static uint32_t ioapic_read_unit(const struct ioapic_unit *unit, uint8_t reg)
{
    unit->base[0] = reg;
    return unit->base[4];
}

static void ioapic_write_unit(const struct ioapic_unit *unit, uint8_t reg, uint32_t val)
{
    unit->base[0] = reg;
    unit->base[4] = val;
}

/* Mask one redirection entry (safe contents: masked, no destination). */
static void ioapic_mask_entry(const struct ioapic_unit *unit, uint32_t entry)
{
    ioapic_write_unit(unit, (uint8_t)(IOAPIC_REG_REDTBL + entry * 2u + 1u), 0);
    ioapic_write_unit(unit, (uint8_t)(IOAPIC_REG_REDTBL + entry * 2u),
                      IOAPIC_LOW_MASKED);
}

/* Program one redirection entry. */
static void ioapic_set_redirection(const struct ioapic_unit *unit, uint32_t entry,
                                   uint8_t vector, uint8_t dest_lapic_id,
                                   int active_low, int level_triggered)
{
    uint32_t low = vector;
    if (active_low)
        low |= IOAPIC_LOW_POLARITY_LOW;
    if (level_triggered)
        low |= IOAPIC_LOW_TRIGGER_LEVEL;

    ioapic_write_unit(unit, (uint8_t)(IOAPIC_REG_REDTBL + entry * 2u + 1u),
                      (uint32_t)dest_lapic_id << 24);
    ioapic_write_unit(unit, (uint8_t)(IOAPIC_REG_REDTBL + entry * 2u), low);
}

static const struct ioapic_unit *ioapic_find_for_gsi(uint32_t gsi, uint32_t *entry_out)
{
    for (int i = 0; i < g_ioapic_count; i++) {
        const struct ioapic_unit *u = &g_ioapics[i];
        if (gsi >= u->gsi_base && (gsi - u->gsi_base) < u->entry_count) {
            if (entry_out)
                *entry_out = gsi - u->gsi_base;
            return u;
        }
    }
    return NULL;
}

int ioapic_active(void)
{
    return g_active;
}

int ioapic_route_gsi(uint32_t gsi, uint8_t vector, uint8_t dest_lapic_id,
                     int active_low, int level_triggered)
{
    if (!g_active || vector < 32u || vector == 255u)
        return -1;

    unsigned long flags = spin_lock_irqsave(&g_ioapic_lock);
    uint32_t entry = 0;
    const struct ioapic_unit *u = ioapic_find_for_gsi(gsi, &entry);
    if (!u) {
        spin_unlock_irqrestore(&g_ioapic_lock, flags);
        return -1;
    }
    ioapic_set_redirection(u, entry, vector, dest_lapic_id, active_low, level_triggered);
    spin_unlock_irqrestore(&g_ioapic_lock, flags);
    return 0;
}

int ioapic_mask_gsi(uint32_t gsi)
{
    if (!g_active)
        return -1;

    unsigned long flags = spin_lock_irqsave(&g_ioapic_lock);
    uint32_t entry = 0;
    const struct ioapic_unit *u = ioapic_find_for_gsi(gsi, &entry);
    if (!u) {
        spin_unlock_irqrestore(&g_ioapic_lock, flags);
        return -1;
    }
    ioapic_mask_entry(u, entry);
    spin_unlock_irqrestore(&g_ioapic_lock, flags);
    return 0;
}

/* MPS INTI flags from the MADT ISO entries (ACPI spec §5.2.12.5). */
static int inti_active_low(uint16_t flags)
{
    return (flags & 0x3u) == 0x3u;
}

static int inti_level_triggered(uint16_t flags)
{
    return ((flags >> 2) & 0x3u) == 0x3u;
}

static void ioapic_mask_all(void)
{
    for (int i = 0; i < g_ioapic_count; i++) {
        for (uint32_t n = 0; n < g_ioapics[i].entry_count; n++)
            ioapic_mask_entry(&g_ioapics[i], n);
    }
}

int ioapic_init(void)
{
    /* The ISA IRQs Tsukasa routes today (IRQ2 is the 8259 cascade). */
    static const uint8_t route_irqs[] = { 0, 1, 12 };

    g_active = 0;
    g_ioapic_count = 0;

    int count = acpi_get_ioapic_count();
    if (!acpi_available() || count <= 0) {
        kprintf("[ioapic] no MADT IOAPIC data, staying on legacy PIC\n");
        return -1;
    }
    if (count > ACPI_MAX_IOAPIC)
        count = ACPI_MAX_IOAPIC;

    for (int i = 0; i < count; i++) {
        uint8_t id = 0;
        uint32_t addr = 0;
        uint32_t gsi_base = 0;
        if (acpi_get_ioapic_info(i, &id, &addr, &gsi_base) != 0 || !addr) {
            kprintf("[ioapic] ERROR: bad MADT IOAPIC[%d] entry\n", i);
            if (i == 0)
                return -1;
            continue;
        }

        uintptr_t virt = 0;
        if (vmm_map_io_region((uint64_t)addr, 0x20u, &virt) != 0) {
            kprintf("[ioapic] ERROR: MMIO map failed for IOAPIC[%d] addr=0x%x\n", i, addr);
            if (i == 0)
                return -1;
            continue;
        }

        struct ioapic_unit *u = &g_ioapics[g_ioapic_count];
        u->base = (volatile uint32_t *)virt;
        u->id = id;
        u->gsi_base = gsi_base;

        uint32_t ver = ioapic_read_unit(u, IOAPIC_REG_VER);
        uint32_t hw_id = (ioapic_read_unit(u, IOAPIC_REG_ID) >> 24) & 0x0Fu;
        u->entry_count = ((ver >> 16) & 0xFFu) + 1u;
        if (u->entry_count > IOAPIC_MAX_ENTRIES)
            u->entry_count = IOAPIC_MAX_ENTRIES;

        for (uint32_t n = 0; n < u->entry_count; n++)
            ioapic_mask_entry(u, n);

        kprintf("[ioapic] id=%u (madt %u) addr=0x%x gsi_base=%u ver=0x%x entries=%u\n",
                hw_id, id, addr, gsi_base, ver & 0xFFu, u->entry_count);
        g_ioapic_count++;
    }

    if (g_ioapic_count == 0)
        return -1;

    /* ioapic_init runs on the BSP. */
    uint8_t bsp = (uint8_t)lapic_read_id();

    for (size_t i = 0; i < sizeof(route_irqs); i++) {
        uint8_t irq = route_irqs[i];
        uint32_t gsi = acpi_irq_to_gsi(irq);
        uint16_t flags = acpi_irq_flags(irq);
        int low = inti_active_low(flags);
        int level = inti_level_triggered(flags);
        uint8_t vector = (uint8_t)(32u + irq);
        uint32_t entry = 0;
        const struct ioapic_unit *u = ioapic_find_for_gsi(gsi, &entry);

        if (!u) {
            kprintf("[ioapic] ERROR: irq%u gsi%u outside IOAPICs, staying on PIC\n",
                    irq, gsi);
            ioapic_mask_all();
            g_ioapic_count = 0;
            return -1;
        }

        ioapic_set_redirection(u, entry, vector, bsp, low, level);
        kprintf("[ioapic] irq%u -> gsi%u ioapic%u entry%u vector%u dest=%u %s %s\n",
                irq, gsi, u->id, entry, vector, bsp,
                low ? "low" : "high", level ? "level" : "edge");
    }

    /* Redirections are live; silence the 8259 completely. */
    pic_disable();
    g_active = 1;

    kprintf("[guide08] ioapic live: irq0/1/12 -> lapic %u, PIC fully masked\n",
            bsp);
    return 0;
}
