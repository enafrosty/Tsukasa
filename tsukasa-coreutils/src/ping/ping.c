/*
 * Project Tsukasa — ICMP Network Echo Request Utility
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
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <sys/net.h>
#include <sys/syscall.h>

static void print_usage(const char *prog)
{
    fprintf(stderr, "Usage: %s [-c count] [-t timeout_ms] <host>\n", prog);
}

int main(int argc, char **argv)
{
    int count = 4;
    uint32_t timeout_ms = 1000;
    const char *host = NULL;

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-c") == 0 && i + 1 < argc) {
            count = atoi(argv[++i]);
            if (count <= 0)
                count = 1;
        } else if (strcmp(argv[i], "-t") == 0 && i + 1 < argc) {
            int t = atoi(argv[++i]);
            if (t > 0)
                timeout_ms = (uint32_t)t;
        } else if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0) {
            print_usage(argv[0]);
            return 0;
        } else if (argv[i][0] != '-') {
            host = argv[i];
        } else {
            fprintf(stderr, "ping: invalid option '%s'\n", argv[i]);
            print_usage(argv[0]);
            return 1;
        }
    }

    if (!host) {
        fprintf(stderr, "ping: missing target host\n");
        print_usage(argv[0]);
        return 1;
    }

    if (net_init() != 0) {
        fprintf(stderr, "ping: failed to initialize network stack\n");
        return 1;
    }

    struct in_addr in;
    struct tsukasa_net_ipv4 target_ip;
    if (inet_aton(host, &in)) {
        memcpy(target_ip.bytes, &in.s_addr, 4);
    } else {
        if (net_dns_lookup(host, &target_ip) != 0) {
            fprintf(stderr, "ping: cannot resolve %s: Unknown host\n", host);
            return 1;
        }
    }

    char ip_str[20];
    snprintf(ip_str, sizeof(ip_str), "%u.%u.%u.%u",
             target_ip.bytes[0], target_ip.bytes[1],
             target_ip.bytes[2], target_ip.bytes[3]);

    printf("PING %s (%s): 56 data bytes\n", host, ip_str);

    int transmitted = 0;
    int received = 0;
    int min_rtt = -1;
    int max_rtt = -1;
    long total_rtt = 0;

    for (int seq = 1; seq <= count; seq++) {
        transmitted++;
        int64_t t_start = __syscall0(SYS_ticks);
        int res = net_ping(&target_ip, timeout_ms);
        int64_t t_end = __syscall0(SYS_ticks);

        if (res < 0) {
            printf("Request timeout for icmp_seq=%d\n", seq);
        } else {
            received++;
            int rtt = res;
            if (rtt <= 0) {
                int64_t elapsed_ticks = (t_end >= t_start) ? (t_end - t_start) : 0;
                rtt = (int)(elapsed_ticks * 10);
                if (rtt <= 0)
                    rtt = 1;
            }
            if (min_rtt < 0 || rtt < min_rtt)
                min_rtt = rtt;
            if (rtt > max_rtt)
                max_rtt = rtt;
            total_rtt += rtt;

            printf("64 bytes from %s: icmp_seq=%d time=%d ms\n", ip_str, seq, rtt);
        }

        if (seq < count)
            sleep(1);
    }

    int loss = 0;
    if (transmitted > 0)
        loss = ((transmitted - received) * 100) / transmitted;

    printf("--- %s ping statistics ---\n", host);
    printf("%d packets transmitted, %d received, %d%% packet loss\n",
           transmitted, received, loss);

    if (received > 0) {
        int avg_rtt = (int)(total_rtt / received);
        printf("round-trip min/avg/max = %d/%d/%d ms\n", min_rtt, avg_rtt, max_rtt);
        return 0;
    }

    return 1;
}
