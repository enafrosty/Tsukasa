/*
 * signal.h - Minimal signal interface for Process Phase 2.
 */

#ifndef TSUKASA_SIGNAL_H
#define TSUKASA_SIGNAL_H

#include <stdint.h>

#include "process.h"

#define SIG_BLOCK   0
#define SIG_UNBLOCK 1
#define SIG_SETMASK 2

int signal_register(int sig, process_signal_handler_t handler);
int signal_send(int pid, int sig);
int signal_mask(int how, uint64_t set, uint64_t *old_set);
int signal_pending(uint64_t *pending_out);

#endif /* TSUKASA_SIGNAL_H */

