/*
 * pci.h - PCI discovery and driver attachment framework.
 */

#ifndef TSUKASA_PCI_H
#define TSUKASA_PCI_H

#include <stdint.h>

#define PCI_MAX_DEVICES 128
#define PCI_MAX_DRIVERS 16

#define PCI_CLASS_NETWORK 0x02
#define PCI_SUBCLASS_ETHERNET 0x00

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
void pci_write16(uint8_t bus, uint8_t device, uint8_t function, uint8_t offset, uint16_t value);
void pci_write32(uint8_t bus, uint8_t device, uint8_t function, uint8_t offset, uint32_t value);

void pci_enable_bus_mastering(const pci_device_info_t *dev);
void pci_enable_io(const pci_device_info_t *dev);
void pci_enable_mmio(const pci_device_info_t *dev);

#endif /* TSUKASA_PCI_H */
