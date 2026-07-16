/*
 * signal.c - Signal APIs backed by the process runtime.
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

