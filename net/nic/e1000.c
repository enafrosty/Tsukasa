/*
 * e1000.c - Intel 82540/82545 class NIC driver (QEMU e1000 path).
 */

#include "e1000.h"

#include "nic.h"

#include "../../dev/pci.h"
#include "../../drv/irq.h"
#include "../../drv/pic.h"
#include "../../mm/vmm_x64.h"

#include <stddef.h>
#include <stdint.h>

#ifdef __x86_64__

#define E1000_VENDOR_ID 0x8086

#define E1000_REG_CTRL   0x0000
#define E1000_REG_STATUS 0x0008
#define E1000_REG_ICR    0x00C0
#define E1000_REG_IMS    0x00D0

#define E1000_REG_RCTL   0x0100
#define E1000_REG_RDBAL  0x2800
#define E1000_REG_RDBAH  0x2804
#define E1000_REG_RDLEN  0x2808
#define E1000_REG_RDH    0x2810
#define E1000_REG_RDT    0x2818

#define E1000_REG_TCTL   0x0400
#define E1000_REG_TIPG   0x0410
#define E1000_REG_TDBAL  0x3800
#define E1000_REG_TDBAH  0x3804
#define E1000_REG_TDLEN  0x3808
#define E1000_REG_TDH    0x3810
#define E1000_REG_TDT    0x3818

#define E1000_REG_RAL    0x5400
#define E1000_REG_RAH    0x5404

#define E1000_CTRL_RST   (1u << 26)
#define E1000_CTRL_SLU   (1u << 6)

#define E1000_STATUS_LU  (1u << 1)

#define E1000_TCTL_EN    (1u << 1)
#define E1000_TCTL_PSP   (1u << 3)

#define E1000_RCTL_EN        (1u << 1)
#define E1000_RCTL_BAM       (1u << 15)
#define E1000_RCTL_SECRC     (1u << 26)
#define E1000_RCTL_BSIZE_2048 0

#define E1000_TX_RING_SIZE 32
#define E1000_RX_RING_SIZE 32

typedef struct e1000_tx_desc {
    uint64_t buffer_addr;
    uint16_t length;
    uint8_t cso;
    uint8_t cmd;
    uint8_t status;
    uint8_t css;
    uint16_t special;
} __attribute__((packed)) e1000_tx_desc_t;

typedef struct e1000_rx_desc {
    uint64_t buffer_addr;
    uint16_t length;
    uint16_t checksum;
    uint8_t status;
    uint8_t errors;
    uint16_t special;
} __attribute__((packed)) e1000_rx_desc_t;

typedef struct e1000_state {
    int initialized;
    volatile uint32_t *mmio;
    uint8_t mac[6];
    uint8_t irq_line;
    uint16_t tx_tail;
    uint16_t rx_tail;
} e1000_state_t;

static e1000_state_t g_e1000;

static e1000_tx_desc_t g_tx_desc[E1000_TX_RING_SIZE] __attribute__((aligned(16)));
static e1000_rx_desc_t g_rx_desc[E1000_RX_RING_SIZE] __attribute__((aligned(16)));
static uint8_t g_tx_buf[E1000_TX_RING_SIZE][2048] __attribute__((aligned(16)));
static uint8_t g_rx_buf[E1000_RX_RING_SIZE][2048] __attribute__((aligned(16)));

static uint8_t g_supported_ids[] = {
    0x0E, /* 0x100E */
    0x0F, /* 0x100F */
    0xD3, /* 0x10D3 */
};

static uint64_t virt_to_phys_ptr(const void *ptr)
{
    return vmm_virt_to_phys((uintptr_t)ptr);
}

static void mem_copy(void *dst, const void *src, size_t n)
{
    uint8_t *d = (uint8_t *)dst;
    const uint8_t *s = (const uint8_t *)src;
    for (size_t i = 0; i < n; i++)
        d[i] = s[i];
}

static void mem_zero(void *dst, size_t n)
{
    uint8_t *d = (uint8_t *)dst;
    for (size_t i = 0; i < n; i++)
        d[i] = 0;
}

static inline uint32_t e1000_read(uint32_t reg)
{
    return g_e1000.mmio[reg / 4];
}

static inline void e1000_write(uint32_t reg, uint32_t value)
{
    g_e1000.mmio[reg / 4] = value;
}

static int e1000_match(const pci_device_info_t *dev)
{
    uint8_t low;
    if (!dev || dev->vendor_id != E1000_VENDOR_ID)
        return -1;
    low = (uint8_t)(dev->device_id & 0xFFu);
    for (size_t i = 0; i < sizeof(g_supported_ids); i++) {
        if (g_supported_ids[i] == low)
            return 0;
    }
    if (dev->device_id == 0x100E)
        return 0;
    return -1;
}

static void e1000_irq_cb(uint8_t irq, void *ctx)
{
    (void)irq;
    (void)ctx;
    if (!g_e1000.initialized)
        return;
    (void)e1000_read(E1000_REG_ICR);
    nic_note_irq();
    (void)nic_poll_rx();
}

static int e1000_get_mac(uint8_t mac_out[6])
{
    if (!g_e1000.initialized || !mac_out)
        return -1;
    for (int i = 0; i < 6; i++)
        mac_out[i] = g_e1000.mac[i];
    return 0;
}

static int e1000_link_up(void)
{
    if (!g_e1000.initialized)
        return 0;
    return (e1000_read(E1000_REG_STATUS) & E1000_STATUS_LU) ? 1 : 0;
}

static int e1000_send_packet(const void *data, size_t len)
{
    e1000_tx_desc_t *desc;
    uint16_t next;
    if (!g_e1000.initialized || !data || len == 0 || len > 1518)
        return -1;

    desc = &g_tx_desc[g_e1000.tx_tail];
    if ((desc->status & 0x1u) == 0)
        return -1;

    mem_copy(g_tx_buf[g_e1000.tx_tail], data, len);
    desc->length = (uint16_t)len;
    desc->cmd = 0x0B; /* EOP | IFCS | RS */
    desc->status = 0;

    next = (uint16_t)((g_e1000.tx_tail + 1) % E1000_TX_RING_SIZE);
    g_e1000.tx_tail = next;
    e1000_write(E1000_REG_TDT, next);
    return 0;
}

static int e1000_receive_packet(void *buffer, size_t max_len)
{
    uint16_t idx;
    e1000_rx_desc_t *desc;
    uint16_t len;
    if (!g_e1000.initialized || !buffer || max_len == 0)
        return 0;

    idx = (uint16_t)((g_e1000.rx_tail + 1) % E1000_RX_RING_SIZE);
    desc = &g_rx_desc[idx];
    if ((desc->status & 0x1u) == 0)
        return 0;

    len = desc->length;
    if (len > max_len)
        len = (uint16_t)max_len;
    mem_copy(buffer, g_rx_buf[idx], len);

    desc->status = 0;
    desc->length = 0;
    g_e1000.rx_tail = idx;
    e1000_write(E1000_REG_RDT, idx);
    return (int)len;
}

static int e1000_attach(const pci_device_info_t *dev)
{
    uintptr_t mmio_virt = 0;
    uint64_t mmio_phys;
    uint32_t bar0;
    uint32_t ral;
    uint32_t rah;
    nic_device_t nic_dev;

    if (!dev || g_e1000.initialized)
        return -1;

    bar0 = dev->bars[0];
    if (bar0 == 0 || bar0 == 0xFFFFFFFFu || (bar0 & 1u))
        return -1;

    mmio_phys = (uint64_t)(bar0 & ~0x0Fu);
    if (vmm_map_io_region(mmio_phys, 0x20000, &mmio_virt) != 0 || !mmio_virt)
        return -1;

    pci_enable_mmio(dev);
    pci_enable_bus_mastering(dev);

    g_e1000.mmio = (volatile uint32_t *)mmio_virt;
    g_e1000.irq_line = dev->irq_line;
    g_e1000.initialized = 0;

    e1000_write(E1000_REG_CTRL, e1000_read(E1000_REG_CTRL) | E1000_CTRL_RST);
    for (int i = 0; i < 100000; i++) {
        if ((e1000_read(E1000_REG_CTRL) & E1000_CTRL_RST) == 0)
            break;
    }

    ral = e1000_read(E1000_REG_RAL);
    rah = e1000_read(E1000_REG_RAH);
    g_e1000.mac[0] = (uint8_t)(ral & 0xFFu);
    g_e1000.mac[1] = (uint8_t)((ral >> 8) & 0xFFu);
    g_e1000.mac[2] = (uint8_t)((ral >> 16) & 0xFFu);
    g_e1000.mac[3] = (uint8_t)((ral >> 24) & 0xFFu);
    g_e1000.mac[4] = (uint8_t)(rah & 0xFFu);
    g_e1000.mac[5] = (uint8_t)((rah >> 8) & 0xFFu);

    mem_zero(g_tx_desc, sizeof(g_tx_desc));
    mem_zero(g_rx_desc, sizeof(g_rx_desc));
    for (int i = 0; i < E1000_TX_RING_SIZE; i++) {
        g_tx_desc[i].buffer_addr = virt_to_phys_ptr(g_tx_buf[i]);
        g_tx_desc[i].status = 0x1;
    }
    for (int i = 0; i < E1000_RX_RING_SIZE; i++)
        g_rx_desc[i].buffer_addr = virt_to_phys_ptr(g_rx_buf[i]);

    e1000_write(E1000_REG_TDBAL, (uint32_t)(virt_to_phys_ptr(g_tx_desc) & 0xFFFFFFFFu));
    e1000_write(E1000_REG_TDBAH, (uint32_t)(virt_to_phys_ptr(g_tx_desc) >> 32));
    e1000_write(E1000_REG_TDLEN, (uint32_t)sizeof(g_tx_desc));
    e1000_write(E1000_REG_TDH, 0);
    e1000_write(E1000_REG_TDT, 0);
    g_e1000.tx_tail = 0;

    e1000_write(E1000_REG_RDBAL, (uint32_t)(virt_to_phys_ptr(g_rx_desc) & 0xFFFFFFFFu));
    e1000_write(E1000_REG_RDBAH, (uint32_t)(virt_to_phys_ptr(g_rx_desc) >> 32));
    e1000_write(E1000_REG_RDLEN, (uint32_t)sizeof(g_rx_desc));
    e1000_write(E1000_REG_RDH, 0);
    e1000_write(E1000_REG_RDT, E1000_RX_RING_SIZE - 1);
    g_e1000.rx_tail = E1000_RX_RING_SIZE - 1;

    e1000_write(E1000_REG_TCTL, E1000_TCTL_EN | E1000_TCTL_PSP | (0x10u << 4) | (0x40u << 12));
    e1000_write(E1000_REG_TIPG, 0x0060200A);
    e1000_write(E1000_REG_RCTL, E1000_RCTL_EN | E1000_RCTL_BAM | E1000_RCTL_SECRC | E1000_RCTL_BSIZE_2048);
    e1000_write(E1000_REG_CTRL, e1000_read(E1000_REG_CTRL) | E1000_CTRL_SLU);
    e1000_write(E1000_REG_IMS, 0x1F6DCu);
    (void)e1000_read(E1000_REG_ICR);

    g_e1000.initialized = 1;

    nic_dev.driver_name = "e1000";
    nic_dev.model_name = "intel 8254x";
    nic_dev.irq_line = dev->irq_line;
    nic_dev.mtu = 1500;
    nic_dev.ops.tx = e1000_send_packet;
    nic_dev.ops.rx_poll = e1000_receive_packet;
    nic_dev.ops.get_mac = e1000_get_mac;
    nic_dev.ops.link_up = e1000_link_up;
    nic_dev.ops.irq_ack = NULL;
    nic_dev.priv = NULL;

    if (nic_register_active(&nic_dev) != 0)
        return -1;

    if (dev->irq_line < 16) {
        irq_register_handler(dev->irq_line, e1000_irq_cb, NULL);
        pic_unmask_irq(dev->irq_line);
    }
    return 0;
}

int e1000_register_pci_driver(void)
{
    static const pci_driver_t drv = {
        .name = "e1000",
        .match = e1000_match,
        .attach = e1000_attach,
    };
    return pci_register_driver(&drv);
}

#else

int e1000_register_pci_driver(void)
{
    return -1;
}

#endif /* __x86_64__ */
