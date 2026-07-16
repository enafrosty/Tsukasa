#include "../include/app_runtime.h"
#include "../include/stdio.h"
#include "../include/stdlib.h"

#include "../lib/syscall.h"

static int cmd_net_main(int argc, char **argv)
{
    struct tsukasa_net_link_info info;
    (void)argc;
    (void)argv;

    if (net_init() != 0) {
        dprintf(2, "net: stack init failed\n");
        return 1;
    }
    if (net_get_link(&info) != 0) {
        dprintf(2, "net: link info unavailable\n");
        return 1;
    }

    dprintf(1, "nic=%s link=%u ip=%u.%u.%u.%u gateway=%u.%u.%u.%u dns=%u.%u.%u.%u\n",
            info.nic_name,
            (unsigned)info.link_up,
            (unsigned)info.ip.bytes[0], (unsigned)info.ip.bytes[1],
            (unsigned)info.ip.bytes[2], (unsigned)info.ip.bytes[3],
            (unsigned)info.gateway.bytes[0], (unsigned)info.gateway.bytes[1],
            (unsigned)info.gateway.bytes[2], (unsigned)info.gateway.bytes[3],
            (unsigned)info.dns.bytes[0], (unsigned)info.dns.bytes[1],
            (unsigned)info.dns.bytes[2], (unsigned)info.dns.bytes[3]);
    return net_has_ip() ? 0 : 1;
}

void app_cmd_net_entry(void)
{
    _exit(app_run_main(cmd_net_main));
}
