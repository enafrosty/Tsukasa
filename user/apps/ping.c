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
