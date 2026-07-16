/*
 * nic.h - Unified NIC abstraction and stats.
 */

#ifndef TSUKASA_NET_NIC_H
#define TSUKASA_NET_NIC_H

#include <stddef.h>
#include <stdint.h>

typedef struct nic_stats {
    uint64_t tx_packets;
    uint64_t tx_bytes;
    uint64_t rx_packets;
    uint64_t rx_bytes;
    uint64_t rx_dropped;
    uint64_t irq_count;
    uint64_t rx_poll_calls;
} nic_stats_t;

typedef struct nic_device_ops {
    int (*tx)(const void *data, size_t len);
    int (*rx_poll)(void *buffer, size_t max_len);
    int (*get_mac)(uint8_t mac_out[6]);
    int (*link_up)(void);
    void (*irq_ack)(void);
} nic_device_ops_t;

typedef struct nic_device {
    const char *driver_name;
    const char *model_name;
    uint8_t irq_line;
    uint16_t mtu;
    nic_device_ops_t ops;
    void *priv;
} nic_device_t;

typedef void (*nic_rx_callback_t)(const uint8_t *frame, size_t len, void *ctx);

void nic_init(void);
int nic_register_active(const nic_device_t *dev);
int nic_ready(void);
const nic_device_t *nic_get_active(void);

int nic_get_mac(uint8_t mac_out[6]);
int nic_link_up(void);
int nic_send(const void *data, size_t len);
int nic_poll_rx(void);

void nic_set_rx_callback(nic_rx_callback_t cb, void *ctx);
void nic_get_stats(nic_stats_t *out);
void nic_note_irq(void);

#endif /* TSUKASA_NET_NIC_H */
