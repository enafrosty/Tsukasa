/*
 * Project Tsukasa — ACPI table discovery (RSDP/XSDT/RSDT/FADT/MADT) + S5 poweroff
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

#ifndef TSUKASA_ACPI_H
#define TSUKASA_ACPI_H

#include <stddef.h>
#include <stdint.h>

/* Fixed-layout firmware structs; field order/packing is dictated by the acpi_structures.h and the OSDev wiki... */

/* Generic System Description Table header (every ACPI table starts with it). */
struct acpi_sdt {
    char     signature[4];
    uint32_t length;
    uint8_t  revision;
    uint8_t  checksum;
    char     oem_id[6];
    char     oem_table_id[8];
    uint32_t oem_revision;
    uint32_t creator_id;
    uint32_t creator_revision;
} __attribute__((packed));

/* Root System Description Pointer (revision 0 = ACPI 1.0 first 20 bytes; revision >= 2 adds the XSDT fields). */
struct acpi_rsdp {
    char     signature[8];
    uint8_t  checksum;
    char     oem_id[6];
    uint8_t  revision;
    uint32_t rsdt_address;

    uint32_t length;
    uint64_t xsdt_address;
    uint8_t  extended_checksum;
    uint8_t  reserved[3];
} __attribute__((packed));

struct acpi_xsdt {
    struct acpi_sdt header;
    uint64_t tables[];
} __attribute__((packed));

/* ACPI Generic Address Structure (spec §5.2.3.2). */
typedef struct {
    uint8_t  address_space_id;
    uint8_t  register_bit_width;
    uint8_t  register_bit_offset;
    uint8_t  access_size;
    uint64_t address;
} __attribute__((packed)) acpi_gas_t;

typedef struct acpi_fadt {
    struct acpi_sdt header;
    uint32_t firmware_ctrl;
    uint32_t dsdt;
    uint8_t  reserved0;
    uint8_t  preferred_pm_profile;
    uint16_t sci_int;
    uint32_t smi_cmd;
    uint8_t  acpi_enable;
    uint8_t  acpi_disable;
    uint8_t  s4bios_req;
    uint8_t  pstate_cnt;
    uint32_t pm1a_evt_blk;
    uint32_t pm1b_evt_blk;
    uint32_t pm1a_cnt_blk;
    uint32_t pm1b_cnt_blk;
    uint32_t pm2_cnt_blk;
    uint32_t pm_tmr_blk;
    uint32_t gpe0_blk;
    uint32_t gpe1_blk;
    uint8_t  pm1_evt_len;
    uint8_t  pm1_cnt_len;
    uint8_t  pm2_cnt_len;
    uint8_t  pm_tmr_len;
    uint8_t  gpe0_blk_len;
    uint8_t  gpe1_blk_len;
    uint8_t  gpe1_base;
    uint8_t  cst_cnt;
    uint16_t p_lvl2_lat;
    uint16_t p_lvl3_lat;
    uint16_t flush_size;
    uint16_t flush_stride;
    uint8_t  duty_offset;
    uint8_t  duty_width;
    uint8_t  day_alrm;
    uint8_t  mon_alrm;
    uint8_t  century;
    uint16_t iapc_boot_arch;
    uint8_t  reserved1;
    uint32_t flags;
    acpi_gas_t reset_reg;
    uint8_t  reset_value;
    uint16_t arm_boot_arch;
    uint8_t  fadt_minor_version;
    uint64_t x_firmware_ctrl;
    uint64_t x_dsdt;
    uint8_t  x_pm1a_evt_blk[12];
    uint8_t  x_pm1b_evt_blk[12];
    uint8_t  x_pm1a_cnt_blk[12];
    uint8_t  x_pm1b_cnt_blk[12];
    uint8_t  x_pm2_cnt_blk[12];
    uint8_t  x_pm_tmr_blk[12];
    uint8_t  x_gpe0_blk[12];
    uint8_t  x_gpe1_blk[12];
    uint8_t  sleep_control_reg[12];
    uint8_t  sleep_status_reg[12];
    uint64_t hypervisor_vendor_identity;
} __attribute__((packed)) acpi_fadt_t;

/* Multiple APIC Description Table, signature "APIC" (spec §5.2.12). */
struct acpi_madt {
    struct acpi_sdt header;
    uint32_t lapic_addr;
    uint32_t flags;
    uint8_t  entries[];
} __attribute__((packed));

struct madt_entry_header {
    uint8_t type;
    uint8_t length;
} __attribute__((packed));

/* MADT type 0 — Processor Local APIC (spec §5.2.12.2). */
struct madt_lapic {
    struct madt_entry_header hdr;
    uint8_t  acpi_processor_id;
    uint8_t  apic_id;
    uint32_t flags;
} __attribute__((packed));

/* MADT type 1 — I/O APIC (spec §5.2.12.3). */
struct madt_ioapic {
    struct madt_entry_header hdr;
    uint8_t  ioapic_id;
    uint8_t  reserved;
    uint32_t ioapic_addr;
    uint32_t gsi_base;
} __attribute__((packed));

/* MADT type 2 — Interrupt Source Override (spec §5.2.12.5). */
struct madt_iso {
    struct madt_entry_header hdr;
    uint8_t  bus;
    uint8_t  source;
    uint32_t gsi;
    uint16_t flags;
} __attribute__((packed));

/* PM1 control register bits (spec §4.8.3.2.1). */
#define ACPI_PM1_SCI_EN (1u << 0)
#define ACPI_PM1_SLP_EN (1u << 13)

#define ACPI_MAX_IOAPIC 8
#define ACPI_MAX_ISO    16

struct tsukasa_boot_info;

/* Parse RSDP -> XSDT/RSDT -> FADT/MADT, ACPI-enable the machine, capture the _S5_ sleep type. */
int acpi_init(const struct tsukasa_boot_info *boot_info);

/* 1 when acpi_init() completed and tables are usable. */
int acpi_available(void);

/* MADT's LAPIC physical base, or 0 when ACPI is unavailable/not yet run — drv/lapic.c falls back to the... */
uint64_t acpi_get_lapic_base(void);

int acpi_get_ioapic_count(void);
int acpi_get_ioapic_info(int idx, uint8_t *id_out, uint32_t *addr_out,
                         uint32_t *gsi_base_out);

/* ISA IRQ -> GSI (identity when no override exists) and its MPS INTI flags. */
uint32_t acpi_irq_to_gsi(uint32_t irq);
uint16_t acpi_irq_flags(uint32_t irq);

/* S5 poweroff: PM1a/PM1b sleep write, then the emulator magic ports (Bochs/VBox/QEMU/Cloud Hypervisor), then... */
void acpi_power_off(void) __attribute__((noreturn));

#endif /* TSUKASA_ACPI_H */
