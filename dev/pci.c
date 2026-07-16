/*
 * pci.c - PCI config-space scan and driver probing.
 */

#include "pci.h"

#include "../include/io.h"
#include "../include/kprintf.h"
#include "../include/spinlock.h"

#include <stddef.h>

#ifdef __x86_64__

#define PCI_CONFIG_ADDRESS 0xCF8u
#define PCI_CONFIG_DATA    0xCFCu

typedef struct pci_driver_slot {
    int used;
    pci_driver_t driver;
} pci_driver_slot_t;

static pci_device_info_t g_pci_devices[PCI_MAX_DEVICES];
static int g_pci_device_count;
static pci_driver_slot_t g_pci_drivers[PCI_MAX_DRIVERS];
static int g_pci_driver_count;
static spinlock_t g_pci_lock = SPINLOCK_INIT;

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

static uint32_t pci_cfg_addr(uint8_t bus, uint8_t device, uint8_t function, uint8_t offset)
{
    return (uint32_t)(0x80000000u |
                      ((uint32_t)bus << 16) |
                      ((uint32_t)device << 11) |
                      ((uint32_t)function << 8) |
                      ((uint32_t)offset & 0xFCu));
}

uint32_t pci_read32(uint8_t bus, uint8_t device, uint8_t function, uint8_t offset)
{
    io_outl(PCI_CONFIG_ADDRESS, pci_cfg_addr(bus, device, function, offset));
    return io_inl(PCI_CONFIG_DATA);
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
    io_outl(PCI_CONFIG_ADDRESS, pci_cfg_addr(bus, device, function, offset));
    io_outl(PCI_CONFIG_DATA, value);
}

void pci_write16(uint8_t bus, uint8_t device, uint8_t function, uint8_t offset, uint16_t value)
{
    uint32_t aligned = pci_read32(bus, device, function, (uint8_t)(offset & 0xFCu));
    uint32_t shift = (uint32_t)((offset & 2u) * 8u);
    uint32_t mask = (uint32_t)(0xFFFFu << shift);
    uint32_t merged = (aligned & ~mask) | ((uint32_t)value << shift);
    pci_write32(bus, device, function, (uint8_t)(offset & 0xFCu), merged);
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

static uint16_t pci_cmd_get(const pci_device_info_t *dev)
{
    if (!dev)
        return 0;
    return pci_read16(dev->bus, dev->device, dev->function, 0x04);
}

static void pci_cmd_set(const pci_device_info_t *dev, uint16_t cmd)
{
    if (!dev)
        return;
    pci_write16(dev->bus, dev->device, dev->function, 0x04, cmd);
}

void pci_enable_bus_mastering(const pci_device_info_t *dev)
{
    uint16_t cmd = pci_cmd_get(dev);
    cmd |= (1u << 2);
    pci_cmd_set(dev, cmd);
}

void pci_enable_io(const pci_device_info_t *dev)
{
    uint16_t cmd = pci_cmd_get(dev);
    cmd |= (1u << 0);
    pci_cmd_set(dev, cmd);
}

void pci_enable_mmio(const pci_device_info_t *dev)
{
    uint16_t cmd = pci_cmd_get(dev);
    cmd |= (1u << 1);
    pci_cmd_set(dev, cmd);
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
void pci_write16(uint8_t bus, uint8_t device, uint8_t function, uint8_t offset, uint16_t value)
{
    (void)bus; (void)device; (void)function; (void)offset; (void)value;
}
void pci_write32(uint8_t bus, uint8_t device, uint8_t function, uint8_t offset, uint32_t value)
{
    (void)bus; (void)device; (void)function; (void)offset; (void)value;
}
void pci_enable_bus_mastering(const pci_device_info_t *dev) { (void)dev; }
void pci_enable_io(const pci_device_info_t *dev) { (void)dev; }
void pci_enable_mmio(const pci_device_info_t *dev) { (void)dev; }

#endif /* __x86_64__ */
