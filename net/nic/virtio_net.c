/*
 * virtio_net.c - Legacy PCI virtio-net (QEMU-first path).
 */

#include "virtio_net.h"

#include "nic.h"

#include "../../dev/pci.h"
#include "../../drv/irq.h"
#include "../../drv/pic.h"
#include "../../include/io.h"
#include "../../mm/vmm_x64.h"

#include <stddef.h>
#include <stdint.h>

#ifdef __x86_64__

#define VIRTIO_VENDOR_ID              0x1AF4
#define VIRTIO_DEVICE_NET_LEGACY      0x1000
#define VIRTIO_DEVICE_NET_MODERN_PCI  0x1041

#define VIRTIO_PCI_HOST_FEATURES  0x00
#define VIRTIO_PCI_GUEST_FEATURES 0x04
#define VIRTIO_PCI_QUEUE_PFN      0x08
#define VIRTIO_PCI_QUEUE_SIZE     0x0C
#define VIRTIO_PCI_QUEUE_SEL      0x0E
#define VIRTIO_PCI_QUEUE_NOTIFY   0x10
#define VIRTIO_PCI_STATUS         0x12
#define VIRTIO_PCI_ISR            0x13
#define VIRTIO_PCI_CONFIG         0x14

#define VRING_DESC_F_NEXT  1
#define VRING_DESC_F_WRITE 2

struct virtq_desc {
    uint64_t addr;
    uint32_t len;
    uint16_t flags;
    uint16_t next;
} __attribute__((packed));

struct virtq_avail {
    uint16_t flags;
    uint16_t idx;
    uint16_t ring[256];
} __attribute__((packed));

struct virtq_used_elem {
    uint32_t id;
    uint32_t len;
} __attribute__((packed));

struct virtq_used {
    uint16_t flags;
    uint16_t idx;
    struct virtq_used_elem ring[256];
} __attribute__((packed));

struct virtqueue {
    struct virtq_desc *desc;
    struct virtq_avail *avail;
    struct virtq_used *used;
    uint16_t q_size;
    uint16_t last_used_idx;
    uint16_t last_avail_idx;
};

struct virtio_net_hdr {
    uint8_t flags;
    uint8_t gso_type;
    uint16_t hdr_len;
    uint16_t gso_size;
    uint16_t csum_start;
    uint16_t csum_offset;
} __attribute__((packed));

typedef struct virtio_net_state {
    int initialized;
    uint16_t io_base;
    uint8_t mac[6];
    uint8_t irq_line;

    struct virtqueue rx_vq;
    struct virtqueue tx_vq;
} virtio_net_state_t;

static virtio_net_state_t g_virtio;

static uint8_t g_rx_ring_mem[16384] __attribute__((aligned(4096)));
static uint8_t g_tx_ring_mem[16384] __attribute__((aligned(4096)));
static uint8_t g_rx_buffers[256][2048] __attribute__((aligned(16)));
static uint8_t g_tx_buffers[256][2048] __attribute__((aligned(16)));
static struct virtio_net_hdr g_tx_hdr[256] __attribute__((aligned(16)));

static uint64_t virt_to_phys_ptr(const void *ptr)
{
    return vmm_virt_to_phys((uintptr_t)ptr);
}

static void mem_zero(void *ptr, size_t n)
{
    uint8_t *p = (uint8_t *)ptr;
    for (size_t i = 0; i < n; i++)
        p[i] = 0;
}

static void mem_copy(void *dst, const void *src, size_t n)
{
    uint8_t *d = (uint8_t *)dst;
    const uint8_t *s = (const uint8_t *)src;
    for (size_t i = 0; i < n; i++)
        d[i] = s[i];
}

static void virtqueue_init(struct virtqueue *vq, uint8_t *mem, uint16_t qsize, size_t mem_size)
{
    uintptr_t avail_end;
    uintptr_t used_start;
    if (!vq || !mem || qsize == 0)
        return;

    mem_zero(mem, mem_size);
    vq->q_size = qsize;
    vq->last_used_idx = 0;
    vq->last_avail_idx = 0;

    vq->desc = (struct virtq_desc *)mem;
    vq->avail = (struct virtq_avail *)(mem + qsize * sizeof(struct virtq_desc));

    avail_end = (uintptr_t)vq->avail + sizeof(struct virtq_avail) + qsize * sizeof(uint16_t);
    used_start = (avail_end + 4095u) & ~(uintptr_t)4095u;
    vq->used = (struct virtq_used *)used_start;
}

static int virtio_match(const pci_device_info_t *dev)
{
    if (!dev)
        return -1;
    if (dev->vendor_id != VIRTIO_VENDOR_ID)
        return -1;
    if (dev->device_id == VIRTIO_DEVICE_NET_LEGACY ||
        dev->device_id == VIRTIO_DEVICE_NET_MODERN_PCI)
        return 0;
    return -1;
}

static void virtio_irq_cb(uint8_t irq, void *ctx)
{
    (void)irq;
    (void)ctx;
    if (!g_virtio.initialized)
        return;
    (void)io_inb((uint16_t)(g_virtio.io_base + VIRTIO_PCI_ISR));
    nic_note_irq();
    (void)nic_poll_rx();
}

static int virtio_link_up(void)
{
    return g_virtio.initialized ? 1 : 0;
}

static int virtio_get_mac(uint8_t mac_out[6])
{
    if (!g_virtio.initialized || !mac_out)
        return -1;
    for (int i = 0; i < 6; i++)
        mac_out[i] = g_virtio.mac[i];
    return 0;
}

static int virtio_send_packet(const void *data, size_t length)
{
    struct virtqueue *txq = &g_virtio.tx_vq;
    uint16_t head;
    uint16_t d0;
    uint16_t d1;
    size_t wire_len = length < 60 ? 60 : length;

    if (!g_virtio.initialized || !data || length == 0 || length > 1514)
        return -1;

    if ((uint16_t)(txq->avail->idx - txq->used->idx) >= (uint16_t)(txq->q_size - 2))
        return -1;

    head = (uint16_t)(txq->last_avail_idx % (txq->q_size / 2));
    d0 = (uint16_t)(head * 2);
    d1 = (uint16_t)(d0 + 1);

    mem_zero(&g_tx_hdr[head], sizeof(struct virtio_net_hdr));
    txq->desc[d0].addr = virt_to_phys_ptr(&g_tx_hdr[head]);
    txq->desc[d0].len = sizeof(struct virtio_net_hdr);
    txq->desc[d0].flags = VRING_DESC_F_NEXT;
    txq->desc[d0].next = d1;

    mem_copy(g_tx_buffers[head], data, length);
    for (size_t i = length; i < wire_len; i++)
        g_tx_buffers[head][i] = 0;

    txq->desc[d1].addr = virt_to_phys_ptr(g_tx_buffers[head]);
    txq->desc[d1].len = (uint32_t)wire_len;
    txq->desc[d1].flags = 0;
    txq->desc[d1].next = 0;

    txq->avail->ring[txq->avail->idx % txq->q_size] = d0;
    __asm__ volatile ("mfence" ::: "memory");
    txq->avail->idx++;
    txq->last_avail_idx++;
    __asm__ volatile ("mfence" ::: "memory");
    io_outw((uint16_t)(g_virtio.io_base + VIRTIO_PCI_QUEUE_NOTIFY), 1);
    return 0;
}

static int virtio_receive_packet(void *buffer, size_t buffer_size)
{
    struct virtqueue *rxq = &g_virtio.rx_vq;
    uint16_t used_idx;
    uint32_t d_idx;
    uint32_t len;
    uint32_t payload_len;
    uint8_t *src;
    uint8_t *dst = (uint8_t *)buffer;

    if (!g_virtio.initialized || !buffer || buffer_size == 0)
        return 0;
    if (rxq->last_used_idx == rxq->used->idx)
        return 0;

    used_idx = (uint16_t)(rxq->last_used_idx % rxq->q_size);
    d_idx = rxq->used->ring[used_idx].id;
    len = rxq->used->ring[used_idx].len;
    rxq->last_used_idx++;

    if (len <= sizeof(struct virtio_net_hdr)) {
        rxq->avail->ring[rxq->avail->idx % rxq->q_size] = (uint16_t)d_idx;
        rxq->avail->idx++;
        io_outw((uint16_t)(g_virtio.io_base + VIRTIO_PCI_QUEUE_NOTIFY), 0);
        return 0;
    }

    payload_len = len - (uint32_t)sizeof(struct virtio_net_hdr);
    if (payload_len > buffer_size)
        payload_len = (uint32_t)buffer_size;

    src = g_rx_buffers[d_idx] + sizeof(struct virtio_net_hdr);
    for (uint32_t i = 0; i < payload_len; i++)
        dst[i] = src[i];

    rxq->avail->ring[rxq->avail->idx % rxq->q_size] = (uint16_t)d_idx;
    __asm__ volatile ("mfence" ::: "memory");
    rxq->avail->idx++;
    __asm__ volatile ("mfence" ::: "memory");
    io_outw((uint16_t)(g_virtio.io_base + VIRTIO_PCI_QUEUE_NOTIFY), 0);
    return (int)payload_len;
}

static int virtio_attach(const pci_device_info_t *dev)
{
    uint32_t bar0;
    uint32_t host_features;
    uint16_t rx_qsize;
    uint16_t tx_qsize;
    nic_device_t nic_dev;

    if (!dev || g_virtio.initialized)
        return -1;

    bar0 = dev->bars[0];
    if ((bar0 & 1u) == 0)
        return -1;

    pci_enable_io(dev);
    pci_enable_bus_mastering(dev);

    g_virtio.io_base = (uint16_t)(bar0 & ~3u);
    g_virtio.irq_line = dev->irq_line;

    io_outb((uint16_t)(g_virtio.io_base + VIRTIO_PCI_STATUS), 0);
    io_outb((uint16_t)(g_virtio.io_base + VIRTIO_PCI_STATUS), 0x01 | 0x02);

    host_features = io_inl((uint16_t)(g_virtio.io_base + VIRTIO_PCI_HOST_FEATURES));
    io_outl((uint16_t)(g_virtio.io_base + VIRTIO_PCI_GUEST_FEATURES), host_features & 0x01000020u);

    io_outw((uint16_t)(g_virtio.io_base + VIRTIO_PCI_QUEUE_SEL), 0);
    rx_qsize = io_inw((uint16_t)(g_virtio.io_base + VIRTIO_PCI_QUEUE_SIZE));
    if (rx_qsize == 0)
        return -1;
    if (rx_qsize > 256)
        rx_qsize = 256;
    virtqueue_init(&g_virtio.rx_vq, g_rx_ring_mem, rx_qsize, sizeof(g_rx_ring_mem));
    io_outl((uint16_t)(g_virtio.io_base + VIRTIO_PCI_QUEUE_PFN),
            (uint32_t)(virt_to_phys_ptr(g_rx_ring_mem) >> 12));

    for (uint16_t i = 0; i < rx_qsize; i++) {
        g_virtio.rx_vq.desc[i].addr = virt_to_phys_ptr(g_rx_buffers[i]);
        g_virtio.rx_vq.desc[i].len = 2048;
        g_virtio.rx_vq.desc[i].flags = VRING_DESC_F_WRITE;
        g_virtio.rx_vq.desc[i].next = 0;
        g_virtio.rx_vq.avail->ring[i] = i;
    }
    g_virtio.rx_vq.avail->idx = rx_qsize;
    g_virtio.rx_vq.last_avail_idx = rx_qsize;

    io_outw((uint16_t)(g_virtio.io_base + VIRTIO_PCI_QUEUE_SEL), 1);
    tx_qsize = io_inw((uint16_t)(g_virtio.io_base + VIRTIO_PCI_QUEUE_SIZE));
    if (tx_qsize == 0)
        return -1;
    if (tx_qsize > 256)
        tx_qsize = 256;
    virtqueue_init(&g_virtio.tx_vq, g_tx_ring_mem, tx_qsize, sizeof(g_tx_ring_mem));
    io_outl((uint16_t)(g_virtio.io_base + VIRTIO_PCI_QUEUE_PFN),
            (uint32_t)(virt_to_phys_ptr(g_tx_ring_mem) >> 12));

    for (int i = 0; i < 6; i++)
        g_virtio.mac[i] = io_inb((uint16_t)(g_virtio.io_base + VIRTIO_PCI_CONFIG + i));

    io_outb((uint16_t)(g_virtio.io_base + VIRTIO_PCI_STATUS), 0x01 | 0x02 | 0x04);
    io_outw((uint16_t)(g_virtio.io_base + VIRTIO_PCI_QUEUE_NOTIFY), 0);

    g_virtio.initialized = 1;

    nic_dev.driver_name = "virtio-net";
    nic_dev.model_name = "virtio legacy pci";
    nic_dev.irq_line = dev->irq_line;
    nic_dev.mtu = 1500;
    nic_dev.ops.tx = virtio_send_packet;
    nic_dev.ops.rx_poll = virtio_receive_packet;
    nic_dev.ops.get_mac = virtio_get_mac;
    nic_dev.ops.link_up = virtio_link_up;
    nic_dev.ops.irq_ack = NULL;
    nic_dev.priv = NULL;

    if (nic_register_active(&nic_dev) != 0)
        return -1;

    if (dev->irq_line < 16) {
        irq_register_handler(dev->irq_line, virtio_irq_cb, NULL);
        pic_unmask_irq(dev->irq_line);
    }
    return 0;
}

int virtio_net_register_pci_driver(void)
{
    static const pci_driver_t drv = {
        .name = "virtio-net",
        .match = virtio_match,
        .attach = virtio_attach,
    };
    return pci_register_driver(&drv);
}

#else

int virtio_net_register_pci_driver(void)
{
    return -1;
}

#endif /* __x86_64__ */
