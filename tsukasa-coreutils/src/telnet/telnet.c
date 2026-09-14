/*
 * Project Tsukasa — Interactive TCP Client Utility
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
#include <sys/poll.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <sys/net.h>

static void print_usage(const char *prog)
{
    fprintf(stderr, "Usage: %s <host> [port]\n", prog);
}

int main(int argc, char **argv)
{
    const char *host = NULL;
    uint16_t port = 23;

    if (argc < 2) {
        print_usage(argv[0]);
        return 1;
    }

    if (strcmp(argv[1], "-h") == 0 || strcmp(argv[1], "--help") == 0) {
        print_usage(argv[0]);
        return 0;
    }

    host = argv[1];
    if (argc >= 3) {
        int p = atoi(argv[2]);
        if (p <= 0 || p > 65535) {
            fprintf(stderr, "telnet: invalid port '%s'\n", argv[2]);
            return 1;
        }
        port = (uint16_t)p;
    }

    if (net_init() != 0) {
        fprintf(stderr, "telnet: failed to initialize network stack\n");
        return 1;
    }

    struct in_addr in;
    struct tsukasa_net_ipv4 target_ip;
    if (inet_aton(host, &in)) {
        memcpy(target_ip.bytes, &in.s_addr, 4);
    } else {
        if (net_dns_lookup(host, &target_ip) != 0) {
            fprintf(stderr, "telnet: cannot resolve %s: Unknown host\n", host);
            return 1;
        }
    }

    char ip_str[20];
    snprintf(ip_str, sizeof(ip_str), "%u.%u.%u.%u",
             target_ip.bytes[0], target_ip.bytes[1],
             target_ip.bytes[2], target_ip.bytes[3]);

    printf("Trying %s...\n", ip_str);

    struct tsukasa_net_tcp_connect_req conn;
    conn.ip = target_ip;
    conn.port = port;

    if (net_tcp_connect(&conn) != 0) {
        fprintf(stderr, "telnet: unable to connect to remote host: Connection refused\n");
        return 1;
    }

    printf("Connected to %s.\n", host);
    printf("Escape character is '^]'.\n");

    char tx_buf[256];
    char rx_buf[512];
    struct pollfd pfd;
    pfd.fd = STDIN_FILENO;
    pfd.events = POLLIN;
    pfd.revents = 0;

    for (;;) {
        /* Check for keyboard input with short timeout. */
        int pr = poll(&pfd, 1, 20);
        if (pr > 0 && (pfd.revents & POLLIN)) {
            ssize_t n = read(STDIN_FILENO, tx_buf, sizeof(tx_buf));
            if (n <= 0) {
                printf("\nConnection closed.\n");
                break;
            }

            /* Check for escape character Ctrl+] (ASCII 0x1D). */
            int escaped = 0;
            for (ssize_t i = 0; i < n; i++) {
                if (tx_buf[i] == 0x1D) {
                    escaped = 1;
                    break;
                }
            }
            if (escaped) {
                printf("\nConnection closed.\n");
                break;
            }

            if (net_tcp_send(tx_buf, (size_t)n) < 0) {
                fprintf(stderr, "\ntelnet: send failed: Broken pipe\n");
                break;
            }
        }

        /* Check for incoming data from the remote host (non-blocking). */
        int got = net_tcp_recv(rx_buf, sizeof(rx_buf), 0);
        if (got > 0) {
            write(STDOUT_FILENO, rx_buf, (size_t)got);
        } else if (got < 0) {
            printf("\nConnection closed by foreign host.\n");
            break;
        }

        net_poll();
    }

    net_tcp_close();
    return 0;
}
