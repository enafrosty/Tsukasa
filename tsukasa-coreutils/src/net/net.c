/*
 * Project Tsukasa — Network Interface Configuration & Telemetry Utility
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

#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <sys/net.h>

static void print_usage(const char *prog)
{
    fprintf(stderr, "Usage: %s [status | dhcp | stats]\n", prog);
    fprintf(stderr, "Commands:\n");
    fprintf(stderr, "  status  Display network interface details (default)\n");
    fprintf(stderr, "  dhcp    Trigger DHCP configuration request\n");
    fprintf(stderr, "  stats   Display network packet and driver telemetry\n");
}

static int do_status(void)
{
    struct tsukasa_net_link_info link;

    if (net_init() != 0) {
        fprintf(stderr, "net: failed to initialize network stack\n");
        return 1;
    }

    if (net_get_link(&link) != 0) {
        fprintf(stderr, "net: failed to retrieve link status\n");
        return 1;
    }

    printf("Interface: %s\n", link.nic_name[0] ? link.nic_name : "unknown");
    printf("Link:      %s\n", link.link_up ? "UP" : "DOWN");
    printf("MAC:       %02x:%02x:%02x:%02x:%02x:%02x\n",
           link.mac.bytes[0], link.mac.bytes[1], link.mac.bytes[2],
           link.mac.bytes[3], link.mac.bytes[4], link.mac.bytes[5]);
    printf("IPv4:      %u.%u.%u.%u\n",
           link.ip.bytes[0], link.ip.bytes[1],
           link.ip.bytes[2], link.ip.bytes[3]);
    printf("Gateway:   %u.%u.%u.%u\n",
           link.gateway.bytes[0], link.gateway.bytes[1],
           link.gateway.bytes[2], link.gateway.bytes[3]);
    printf("DNS:       %u.%u.%u.%u\n",
           link.dns.bytes[0], link.dns.bytes[1],
           link.dns.bytes[2], link.dns.bytes[3]);

    return 0;
}

static int do_dhcp(void)
{
    struct tsukasa_net_link_info link;

    if (net_init() != 0) {
        fprintf(stderr, "net: failed to initialize network stack\n");
        return 1;
    }

    if (net_get_link(&link) == 0 && link.nic_name[0])
        printf("Sending DHCP request on %s...\n", link.nic_name);
    else
        printf("Sending DHCP request...\n");

    if (net_dhcp() != 0) {
        fprintf(stderr, "net: failed to send DHCP request\n");
        return 1;
    }

    /* Wait up to 3 seconds for DHCP lease negotiation. */
    for (int i = 0; i < 30; i++) {
        net_poll();
        if (net_has_ip())
            break;
        usleep(100000);
    }

    if (!net_has_ip()) {
        fprintf(stderr, "net: DHCP lease timed out\n");
        return 1;
    }

    if (net_get_link(&link) != 0) {
        fprintf(stderr, "net: DHCP lease acquired but failed to read link\n");
        return 1;
    }

    printf("DHCP lease acquired:\n");
    printf("  IPv4:    %u.%u.%u.%u\n",
           link.ip.bytes[0], link.ip.bytes[1],
           link.ip.bytes[2], link.ip.bytes[3]);
    printf("  Gateway: %u.%u.%u.%u\n",
           link.gateway.bytes[0], link.gateway.bytes[1],
           link.gateway.bytes[2], link.gateway.bytes[3]);
    printf("  DNS:     %u.%u.%u.%u\n",
           link.dns.bytes[0], link.dns.bytes[1],
           link.dns.bytes[2], link.dns.bytes[3]);

    return 0;
}

static int do_stats(void)
{
    struct tsukasa_net_stats stats;

    if (net_init() != 0) {
        fprintf(stderr, "net: failed to initialize network stack\n");
        return 1;
    }

    if (net_get_stats(&stats) != 0) {
        fprintf(stderr, "net: failed to retrieve network statistics\n");
        return 1;
    }

    printf("Network Statistics:\n");
    printf("  TX Packets: %llu\n", (unsigned long long)stats.tx_packets);
    printf("  TX Bytes:   %llu\n", (unsigned long long)stats.tx_bytes);
    printf("  RX Packets: %llu\n", (unsigned long long)stats.rx_packets);
    printf("  RX Bytes:   %llu\n", (unsigned long long)stats.rx_bytes);
    printf("  RX Dropped: %llu\n", (unsigned long long)stats.rx_dropped);
    printf("  Interrupts: %llu\n", (unsigned long long)stats.irq_count);
    printf("  Poll Calls: %llu\n", (unsigned long long)stats.rx_poll_calls);
    printf("  Stack Init: %s\n", stats.stack_initialized ? "yes" : "no");
    printf("  Has IPv4:   %s\n", stats.has_ip ? "yes" : "no");

    return 0;
}

int main(int argc, char **argv)
{
    if (argc > 1) {
        if (strcmp(argv[1], "-h") == 0 || strcmp(argv[1], "--help") == 0) {
            print_usage(argv[0]);
            return 0;
        }
        if (strcmp(argv[1], "status") == 0)
            return do_status();
        if (strcmp(argv[1], "dhcp") == 0)
            return do_dhcp();
        if (strcmp(argv[1], "stats") == 0)
            return do_stats();

        fprintf(stderr, "net: unknown command '%s'\n", argv[1]);
        print_usage(argv[0]);
        return 1;
    }

    return do_status();
}
