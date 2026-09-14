/*
 * Project Tsukasa — include "../include/app_runtime.h"
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

#include "user/include/app_runtime.h"

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
