#include "../include/app_runtime.h"
#include "../include/stdio.h"
#include "../include/stdlib.h"

#include "../lib/syscall.h"

static int cmd_ping_main(int argc, char **argv)
{
    struct tsukasa_net_ipv4 target;
    int rtt;
    (void)argc;
    (void)argv;

    target.bytes[0] = 10;
    target.bytes[1] = 0;
    target.bytes[2] = 2;
    target.bytes[3] = 2;

    if (net_init() != 0 || !net_has_ip()) {
        dprintf(2, "ping: network not ready\n");
        return 1;
    }

    rtt = net_ping(&target, (uint32_t)1000);
    if (rtt < 0) {
        dprintf(2, "ping: request failed\n");
        return 1;
    }
    dprintf(1, "ping 10.0.2.2: rtt=%d ms\n", rtt);
    return 0;
}

void app_cmd_ping_entry(void)
{
    _exit(app_run_main(cmd_ping_main));
}
