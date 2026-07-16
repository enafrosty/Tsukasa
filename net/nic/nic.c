/*
 * nic.c - Unified NIC abstraction layer.
 */

#include "nic.h"

#include "../../dev/pci.h"
#include "../../include/kprintf.h"
#include "../../include/spinlock.h"
#include "e1000.h"
#include "virtio_net.h"

#ifdef __x86_64__

static spinlock_t g_nic_lock = SPINLOCK_INIT;
static nic_device_t g_active_dev;
static int g_active_valid;
static nic_stats_t g_stats;
static nic_rx_callback_t g_rx_cb;
static void *g_rx_cb_ctx;
static int g_nic_inited;

int nic_register_active(const nic_device_t *dev)
{
    if (!dev || !dev->ops.tx || !dev->ops.rx_poll || !dev->ops.get_mac)
        return -1;

    spin_lock(&g_nic_lock);
    if (g_active_valid) {
        spin_unlock(&g_nic_lock);
        return -1;
    }
    g_active_dev = *dev;
    g_active_valid = 1;
    spin_unlock(&g_nic_lock);

    kprintf("[net] active nic driver=%s model=%s irq=%u mtu=%u\n",
            g_active_dev.driver_name ? g_active_dev.driver_name : "unknown",
            g_active_dev.model_name ? g_active_dev.model_name : "unknown",
            (unsigned)g_active_dev.irq_line,
            (unsigned)g_active_dev.mtu);
    return 0;
}

void nic_init(void)
{
    int attached;
    if (g_nic_inited)
        return;

    g_nic_inited = 1;
    g_active_valid = 0;
    g_rx_cb = NULL;
    g_rx_cb_ctx = NULL;
    for (size_t i = 0; i < sizeof(g_stats); i++)
        ((uint8_t *)&g_stats)[i] = 0;

    (void)virtio_net_register_pci_driver();
    (void)e1000_register_pci_driver();

    attached = pci_probe_and_attach();
    kprintf("[net] pci nic attachments=%d\n", attached);
}

int nic_ready(void)
{
    return g_active_valid ? 1 : 0;
}

const nic_device_t *nic_get_active(void)
{
    return g_active_valid ? &g_active_dev : NULL;
}

int nic_get_mac(uint8_t mac_out[6])
{
    if (!g_active_valid || !mac_out || !g_active_dev.ops.get_mac)
        return -1;
    return g_active_dev.ops.get_mac(mac_out);
}

int nic_link_up(void)
{
    if (!g_active_valid)
        return 0;
    if (!g_active_dev.ops.link_up)
        return 1;
    return g_active_dev.ops.link_up() ? 1 : 0;
}

int nic_send(const void *data, size_t len)
{
    int rc;
    if (!g_active_valid || !data || len == 0)
        return -1;

    rc = g_active_dev.ops.tx(data, len);
    if (rc == 0) {
        g_stats.tx_packets++;
        g_stats.tx_bytes += len;
    }
    return rc;
}

int nic_poll_rx(void)
{
    uint8_t frame[2048];
    int total = 0;
    int loops = 0;
    if (!g_active_valid || !g_active_dev.ops.rx_poll)
        return 0;

    g_stats.rx_poll_calls++;
    for (;;) {
        int got = g_active_dev.ops.rx_poll(frame, sizeof(frame));
        if (got <= 0)
            break;
        loops++;
        g_stats.rx_packets++;
        g_stats.rx_bytes += (uint64_t)got;
        if (g_rx_cb) {
            g_rx_cb(frame, (size_t)got, g_rx_cb_ctx);
        } else {
            g_stats.rx_dropped++;
        }
        total += got;
        if (loops >= 64)
            break;
    }
    return total;
}

void nic_set_rx_callback(nic_rx_callback_t cb, void *ctx)
{
    g_rx_cb = cb;
    g_rx_cb_ctx = ctx;
}

void nic_get_stats(nic_stats_t *out)
{
    if (!out)
        return;
    *out = g_stats;
}

void nic_note_irq(void)
{
    g_stats.irq_count++;
}

#else

void nic_init(void) {}
int nic_register_active(const nic_device_t *dev) { (void)dev; return -1; }
int nic_ready(void) { return 0; }
const nic_device_t *nic_get_active(void) { return NULL; }
int nic_get_mac(uint8_t mac_out[6]) { (void)mac_out; return -1; }
int nic_link_up(void) { return 0; }
int nic_send(const void *data, size_t len) { (void)data; (void)len; return -1; }
int nic_poll_rx(void) { return 0; }
void nic_set_rx_callback(nic_rx_callback_t cb, void *ctx) { (void)cb; (void)ctx; }
void nic_get_stats(nic_stats_t *out) { (void)out; }

#endif /* __x86_64__ */
