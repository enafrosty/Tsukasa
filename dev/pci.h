/*
 * Project Tsukasa - PCI discovery, ECAM, BAR decode, capabilities, and MSI/MSI-X
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

#ifndef TSUKASA_PCI_H
#define TSUKASA_PCI_H

#include <stdint.h>

#define PCI_MAX_DEVICES 128
#define PCI_MAX_DRIVERS 16

#define PCI_CLASS_NETWORK 0x02
#define PCI_SUBCLASS_ETHERNET 0x00

#define PCI_CAP_ID_MSI  0x05u
#define PCI_CAP_ID_MSIX 0x11u

typedef struct pci_device_info {
    uint8_t bus;
    uint8_t device;
    uint8_t function;

    uint16_t vendor_id;
    uint16_t device_id;

    uint8_t class_code;
    uint8_t subclass;
    uint8_t prog_if;
    uint8_t revision;
    uint8_t header_type;

    uint8_t irq_line;
    uint8_t irq_pin;

    uint32_t bars[6];
    char bound_driver[24];
} pci_device_info_t;

typedef int (*pci_driver_match_fn)(const pci_device_info_t *dev);
typedef int (*pci_driver_attach_fn)(const pci_device_info_t *dev);

typedef struct pci_driver {
    const char *name;
    pci_driver_match_fn match;
    pci_driver_attach_fn attach;
} pci_driver_t;

void pci_init(void);
int pci_rescan(void);

int pci_device_count(void);
const pci_device_info_t *pci_device_at(int index);
const pci_device_info_t *pci_find_device(uint16_t vendor_id, uint16_t device_id);
const pci_device_info_t *pci_find_class(uint8_t class_code, uint8_t subclass);

int pci_register_driver(const pci_driver_t *driver);
int pci_probe_and_attach(void);

uint8_t pci_read8(uint8_t bus, uint8_t device, uint8_t function, uint8_t offset);
uint16_t pci_read16(uint8_t bus, uint8_t device, uint8_t function, uint8_t offset);
uint32_t pci_read32(uint8_t bus, uint8_t device, uint8_t function, uint8_t offset);
uint32_t pci_read_ext32(uint8_t bus, uint8_t device, uint8_t function, uint16_t offset);
void pci_write16(uint8_t bus, uint8_t device, uint8_t function, uint8_t offset, uint16_t value);
void pci_write32(uint8_t bus, uint8_t device, uint8_t function, uint8_t offset, uint32_t value);
void pci_write_ext32(uint8_t bus, uint8_t device, uint8_t function, uint16_t offset, uint32_t value);

int pci_ecam_active(void);
int pci_get_bar(const pci_device_info_t *dev, int bar_idx,
                uint64_t *base_out, uint64_t *size_out, int *is_mmio_out);
uint8_t pci_find_capability(const pci_device_info_t *dev, uint8_t cap_id);
int pci_enable_msi(const pci_device_info_t *dev, uint8_t vector, uint8_t dest_lapic_id);
int pci_enable_msix(const pci_device_info_t *dev, uint16_t entry_idx,
                    uint8_t vector, uint8_t dest_lapic_id);

void pci_enable_bus_mastering(const pci_device_info_t *dev);
void pci_enable_io(const pci_device_info_t *dev);
void pci_enable_mmio(const pci_device_info_t *dev);

#endif /* TSUKASA_PCI_H */
