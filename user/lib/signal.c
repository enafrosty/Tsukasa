/*
 * Project Tsukasa — include "../include/signal.h"
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

#include <stddef.h>
#include <stdint.h>
#include "user/include/signal.h"
#include "user/include/unistd.h"
#include "user/lib/syscall.h"

sighandler_t signal(int sig, sighandler_t handler)
{
    struct sigaction sa;
    struct sigaction old;
    sa.sa_handler = handler;
    sa.sa_mask = 0;
    sa.sa_flags = 0;
    if (sigaction(sig, &sa, &old) != 0)
        return SIG_ERR;
    return old.sa_handler;
}

int raise(int sig)
{
    return kill(getpid(), sig);
}
