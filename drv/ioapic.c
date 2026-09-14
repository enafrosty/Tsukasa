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
#include "../mm/vmm_x64.h"

/* Register access is indirect: write the register index to IOREGSEL (byte offset 0x00), then read/write the... */
#define IOAPIC_REG_ID      0x00u
#define IOAPIC_REG_VER     0x01u
#define IOAPIC_REG_REDTBL  0x10u  /* entry n: low dword 0x10+2n, high 0x10+2n+1 */

/* Redirection entry, low dword (82093AA §3.2.4): bits 0-7 vector, 8-10 delivery mode (000 = fixed), 11 dest... */
#define IOAPIC_LOW_POLARITY_LOW  (1u << 13)
#define IOAPIC_LOW_TRIGGER_LEVEL (1u << 15)
#define IOAPIC_LOW_MASKED        (1u << 16)

static volatile uint32_t *g_base;
static uint32_t g_gsi_base;
static uint32_t g_entry_count;
static int g_active;

static uint32_t ioapic_read(uint8_t reg)
{
    g_base[0] = reg;
    return g_base[4];
}

static void ioapic_write(uint8_t reg, uint32_t val)
{
    g_base[0] = reg;
    g_base[4] = val;
}

/* Mask one redirection entry (safe contents: masked, no destination). */
static void ioapic_mask_entry(uint32_t entry)
{
    ioapic_write((uint8_t)(IOAPIC_REG_REDTBL + entry * 2 + 1), 0);
    ioapic_write((uint8_t)(IOAPIC_REG_REDTBL + entry * 2),
                 IOAPIC_LOW_MASKED);
}

/* Program one redirection entry. */
static void ioapic_set_redirection(uint32_t entry, uint8_t vector,
                                   uint8_t dest_lapic_id, int active_low,
                                   int level_triggered)
{
    uint32_t low = vector;
    if (active_low)
        low |= IOAPIC_LOW_POLARITY_LOW;
    if (level_triggered)
        low |= IOAPIC_LOW_TRIGGER_LEVEL;

    ioapic_write((uint8_t)(IOAPIC_REG_REDTBL + entry * 2 + 1),
                 (uint32_t)dest_lapic_id << 24);
    ioapic_write((uint8_t)(IOAPIC_REG_REDTBL + entry * 2), low);
}

int ioapic_active(void)
{
    return g_active;
}

/* MPS INTI flags from the MADT ISO entries (ACPI spec §5.2.12.5): bits 1:0 polarity — 00 = conforms to bus... */
static int inti_active_low(uint16_t flags)
{
    return (flags & 0x3u) == 0x3u;
}

static int inti_level_triggered(uint16_t flags)
{
    return ((flags >> 2) & 0x3u) == 0x3u;
}

int ioapic_init(void)
{
    /* The ISA IRQs Tsukasa routes today (drv/pic.c unmasks exactly these; IRQ2 is the 8259 cascade — it has no... */
    static const uint8_t route_irqs[] = { 0, 1, 12 };

    if (!acpi_available() || acpi_get_ioapic_count() <= 0) {
        kprintf("[ioapic] no MADT IOAPIC data, staying on legacy PIC\n");
        return -1;
    }

    /* Every routed GSI is bounds-checked against this chip's GSI window; a second IOAPIC would only matter for... */
    uint8_t id = 0;
    uint32_t addr = 0;
    if (acpi_get_ioapic_info(0, &id, &addr, &g_gsi_base) != 0 || !addr) {
        kprintf("[ioapic] ERROR: bad MADT IOAPIC entry, staying on PIC\n");
        return -1;
    }

    uintptr_t virt = 0;
    if (vmm_map_io_region((uint64_t)addr, 0x20u, &virt) != 0) {
        kprintf("[ioapic] ERROR: MMIO map failed, staying on PIC\n");
        return -1;
    }
    g_base = (volatile uint32_t *)virt;

    uint32_t ver = ioapic_read(IOAPIC_REG_VER);
    uint32_t hw_id = (ioapic_read(IOAPIC_REG_ID) >> 24) & 0x0Fu;
    g_entry_count = ((ver >> 16) & 0xFFu) + 1u;
    kprintf("[ioapic] id=%u (madt %u) addr=0x%x gsi_base=%u ver=0x%x entries=%u\n",
            hw_id, id, addr, g_gsi_base, ver & 0xFFu, g_entry_count);

    for (uint32_t n = 0; n < g_entry_count; n++)
        ioapic_mask_entry(n);

    /* ioapic_init runs on the BSP. */
    uint8_t bsp = (uint8_t)lapic_read_id();

    for (size_t i = 0; i < sizeof(route_irqs); i++) {
        uint8_t irq = route_irqs[i];
        uint32_t gsi = acpi_irq_to_gsi(irq);
        uint16_t flags = acpi_irq_flags(irq);
        int low = inti_active_low(flags);
        int level = inti_level_triggered(flags);
        uint8_t vector = (uint8_t)(32u + irq);

        if (gsi < g_gsi_base || gsi - g_gsi_base >= g_entry_count) {
            kprintf("[ioapic] ERROR: irq%u gsi%u outside entries, staying on PIC\n",
                    irq, gsi);
            for (uint32_t n = 0; n < g_entry_count; n++)
                ioapic_mask_entry(n);
            return -1;
        }

        ioapic_set_redirection(gsi - g_gsi_base, vector, bsp, low, level);
        kprintf("[ioapic] irq%u -> gsi%u entry%u vector%u dest=%u %s %s\n",
                irq, gsi, gsi - g_gsi_base, vector, bsp,
                low ? "low" : "high", level ? "level" : "edge");
    }

    /* Redirections are live; silence the 8259 completely. */
    pic_disable();
    g_active = 1;

    kprintf("[guide08] ioapic live: irq0/1/12 -> lapic %u, PIC fully masked\n",
            bsp);
    return 0;
}
