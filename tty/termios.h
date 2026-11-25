/*
 * Project Tsukasa — Kernel termios line discipline
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

#ifndef TSUKASA_TERMIOS_H
#define TSUKASA_TERMIOS_H

#include <stddef.h>
#include <stdint.h>
#include "../include/spinlock.h"
#include "../include/vfs_abi.h"
#include "../sys/wait_queue.h"

#define TTY_LDISC_BUF_SIZE 4096U

typedef void (*tty_echo_fn_t)(void *ctx, const char *buf, size_t len);
typedef void (*tty_sig_fn_t)(void *ctx, int sig);

typedef struct tty_ldisc {
    spinlock_t lock;
    struct tsk_termios termios;
    char line_buf[TTY_LDISC_BUF_SIZE];
    size_t line_len;
    char rx_buf[TTY_LDISC_BUF_SIZE];
    size_t rx_head;
    size_t rx_tail;
    size_t rx_count;
    uint32_t eof_pending;
} tty_ldisc_t;

void tty_ldisc_init(tty_ldisc_t *ld);
void tty_ldisc_flush_input(tty_ldisc_t *ld);

void tty_ldisc_input_bytes(tty_ldisc_t *ld, const char *data, size_t len,
                           tty_echo_fn_t echo_fn, tty_sig_fn_t sig_fn,
                           void *ctx, wait_queue_head_t *waitq);

size_t tty_ldisc_process_output(tty_ldisc_t *ld, const char *in, size_t len,
                                char *out, size_t out_cap);

long tty_ldisc_read(tty_ldisc_t *ld, void *buf, size_t count, int flags,
                    int (*is_hup)(void *), void *ctx, wait_queue_head_t *waitq);

int tty_ldisc_can_read(tty_ldisc_t *ld);
int tty_ldisc_fionread(tty_ldisc_t *ld);
int tty_ldisc_ioctl(tty_ldisc_t *ld, unsigned long req, void *arg);

#endif /* TSUKASA_TERMIOS_H */
