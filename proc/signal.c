/*
 * Project Tsukasa — Signal APIs backed by the process runtime
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

#include "signal.h"

int signal_register(int sig, process_signal_handler_t handler)
{
    return process_signal_register(process_current_pid(), sig, handler);
}

int signal_send(int pid, int sig)
{
    return process_signal_send(pid, sig);
}

int signal_mask(int how, uint64_t set, uint64_t *old_set)
{
    return process_signal_mask(process_current_pid(), how, set, old_set);
}

int signal_pending(uint64_t *pending_out)
{
    return process_signal_pending(process_current_pid(), pending_out);
}
