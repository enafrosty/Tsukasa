/*
 * Project Tsukasa — HTTP File Retrieval Utility
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
#include <fcntl.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <sys/net.h>

static void print_usage(const char *prog)
{
    fprintf(stderr, "Usage: %s [-o output_file] <url>\n", prog);
}

static int parse_url(const char *url, char *host, size_t host_sz,
                     uint16_t *port, char *path, size_t path_sz)
{
    const char *p = url;

    if (strncmp(p, "https://", 8) == 0) {
        fprintf(stderr, "fetch: HTTPS is not supported\n");
        return -1;
    }
    if (strncmp(p, "http://", 7) == 0)
        p += 7;

    const char *slash = strchr(p, '/');
    size_t host_len = slash ? (size_t)(slash - p) : strlen(p);

    if (host_len == 0 || host_len >= host_sz) {
        fprintf(stderr, "fetch: invalid host in URL\n");
        return -1;
    }

    memcpy(host, p, host_len);
    host[host_len] = '\0';

    *port = 80;
    char *colon = strchr(host, ':');
    if (colon) {
        *colon = '\0';
        int pt = atoi(colon + 1);
        if (pt > 0 && pt <= 65535)
            *port = (uint16_t)pt;
    }

    if (slash && *slash) {
        strncpy(path, slash, path_sz - 1);
        path[path_sz - 1] = '\0';
    } else {
        strncpy(path, "/", path_sz - 1);
        path[path_sz - 1] = '\0';
    }

    return 0;
}

int main(int argc, char **argv)
{
    const char *out_filename = NULL;
    const char *url = NULL;

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-o") == 0 && i + 1 < argc) {
            out_filename = argv[++i];
        } else if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0) {
            print_usage(argv[0]);
            return 0;
        } else if (argv[i][0] != '-') {
            url = argv[i];
        } else {
            fprintf(stderr, "fetch: invalid option '%s'\n", argv[i]);
            print_usage(argv[0]);
            return 1;
        }
    }

    if (!url) {
        fprintf(stderr, "fetch: missing URL\n");
        print_usage(argv[0]);
        return 1;
    }

    char host[128];
    char path[256];
    uint16_t port = 80;
    if (parse_url(url, host, sizeof(host), &port, path, sizeof(path)) != 0)
        return 1;

    char default_filename[128];
    if (!out_filename) {
        const char *last_slash = strrchr(path, '/');
        if (last_slash && *(last_slash + 1) != '\0') {
            strncpy(default_filename, last_slash + 1, sizeof(default_filename) - 1);
            default_filename[sizeof(default_filename) - 1] = '\0';
        } else {
            strncpy(default_filename, "index.html", sizeof(default_filename) - 1);
            default_filename[sizeof(default_filename) - 1] = '\0';
        }
        out_filename = default_filename;
    }

    if (net_init() != 0) {
        fprintf(stderr, "fetch: failed to initialize network stack\n");
        return 1;
    }

    struct in_addr in;
    struct tsukasa_net_ipv4 target_ip;
    if (inet_aton(host, &in)) {
        memcpy(target_ip.bytes, &in.s_addr, 4);
    } else {
        if (net_dns_lookup(host, &target_ip) != 0) {
            fprintf(stderr, "fetch: cannot resolve %s: Unknown host\n", host);
            return 1;
        }
    }

    struct tsukasa_net_tcp_connect_req conn;
    conn.ip = target_ip;
    conn.port = port;

    if (net_tcp_connect(&conn) != 0) {
        fprintf(stderr, "fetch: connection to %s:%u failed\n", host, (unsigned)port);
        return 1;
    }

    char req[512];
    snprintf(req, sizeof(req),
             "GET %s HTTP/1.0\r\n"
             "Host: %s\r\n"
             "User-Agent: Tsukasa-Fetch/1.0\r\n"
             "Connection: close\r\n\r\n",
             path, host);

    if (net_tcp_send(req, strlen(req)) < 0) {
        fprintf(stderr, "fetch: failed to send HTTP request\n");
        net_tcp_close();
        return 1;
    }

    /* Buffer response headers and locate the \r\n\r\n delimiter. */
    char hbuf[4096];
    size_t hlen = 0;
    char *sep = NULL;
    int sep_len = 0;

    while (hlen < sizeof(hbuf) - 1) {
        int got = net_tcp_recv(hbuf + hlen, sizeof(hbuf) - 1 - hlen, 1);
        if (got <= 0)
            break;
        hlen += (size_t)got;
        hbuf[hlen] = '\0';

        sep = strstr(hbuf, "\r\n\r\n");
        if (sep) {
            sep_len = 4;
            break;
        }
        sep = strstr(hbuf, "\n\n");
        if (sep) {
            sep_len = 2;
            break;
        }
    }

    if (!sep) {
        fprintf(stderr, "fetch: malformed HTTP response or premature EOF\n");
        net_tcp_close();
        return 1;
    }

    /* Verify HTTP response status line (expecting 2xx). */
    char *status_code = strchr(hbuf, ' ');
    int code = status_code ? atoi(status_code + 1) : 0;
    if (code < 200 || code >= 300) {
        fprintf(stderr, "fetch: server returned HTTP %d\n", code);
        net_tcp_close();
        return 1;
    }

    int out_fd = STDOUT_FILENO;
    int is_stdout = (strcmp(out_filename, "-") == 0);
    if (!is_stdout) {
        out_fd = open(out_filename, O_WRONLY | O_CREAT | O_TRUNC, 0644);
        if (out_fd < 0) {
            fprintf(stderr, "fetch: failed to open output file '%s'\n", out_filename);
            net_tcp_close();
            return 1;
        }
    }

    size_t total_downloaded = 0;
    char *body_start = sep + sep_len;
    size_t body_in_hbuf = (size_t)((hbuf + hlen) - body_start);

    if (body_in_hbuf > 0) {
        write(out_fd, body_start, body_in_hbuf);
        total_downloaded += body_in_hbuf;
    }

    /* Stream the remainder of the HTTP response body. */
    char stream_buf[2048];
    int idle_count = 0;
    while (idle_count < 10) {
        int got = net_tcp_recv(stream_buf, sizeof(stream_buf), 1);
        if (got > 0) {
            write(out_fd, stream_buf, (size_t)got);
            total_downloaded += (size_t)got;
            idle_count = 0;
        } else if (got < 0) {
            break;
        } else {
            idle_count++;
            net_poll();
        }
    }

    if (!is_stdout) {
        close(out_fd);
        fprintf(stderr, "fetch: saved %zu bytes to '%s'\n",
                total_downloaded, out_filename);
    }

    net_tcp_close();
    return 0;
}
