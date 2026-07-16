#include "../include/app_runtime.h"
#include "../include/stdio.h"
#include "../include/stdlib.h"

#include "../lib/syscall.h"

static int cmd_telnet_main(int argc, char **argv)
{
    struct tsukasa_net_ipv4 ip;
    struct tsukasa_net_tcp_connect_req conn;
    char rx[128];
    static const char probe[] = "HEAD / HTTP/1.0\r\nHost: example.com\r\n\r\n";
    int got;
    (void)argc;
    (void)argv;

    if (net_init() != 0) {
        dprintf(2, "telnet: net init failed\n");
        return 1;
    }
    if (net_dns_lookup("example.com", &ip) != 0) {
        dprintf(2, "telnet: dns lookup failed\n");
        return 1;
    }

    conn.ip = ip;
    conn.port = 80;
    if (net_tcp_connect(&conn) != 0) {
        dprintf(2, "telnet: connect failed\n");
        return 1;
    }
    if (net_tcp_send(probe, sizeof(probe) - 1) < 0) {
        net_tcp_close();
        dprintf(2, "telnet: send failed\n");
        return 1;
    }

    got = net_tcp_recv(rx, sizeof(rx) - 1, 1);
    net_tcp_close();
    if (got <= 0) {
        dprintf(2, "telnet: recv failed\n");
        return 1;
    }
    rx[got] = '\0';
    dprintf(1, "%s\n", rx);
    return 0;
}

void app_cmd_telnet_entry(void)
{
    _exit(app_run_main(cmd_telnet_main));
}
