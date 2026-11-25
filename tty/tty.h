/*
 * Project Tsukasa — Multi-VT TTY subsystem with termios and job control
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

#ifndef TSUKASA_TTY_H
#define TSUKASA_TTY_H

#include <stddef.h>
#include <stdint.h>
#include "../sys/wait_queue.h"

#define TTY_MAX 8

void tty_init(void);

int tty_create(void);
int tty_destroy(int tty_id);

int tty_get_active(void);
int tty_set_active(int tty_id);

int tty_set_kd_mode(int tty_id, int mode);
int tty_get_kd_mode(int tty_id);

int tty_set_foreground_pgid(int tty_id, int pgid);
int tty_get_foreground_pgid(int tty_id);
int tty_kill_foreground(int tty_id, int sig);

long tty_read(int tty_id, void *buf, size_t count, int flags);
size_t tty_write(int tty_id, const void *buf, size_t count);
int tty_poll(int tty_id, int events);
int tty_ioctl(int tty_id, unsigned long request, void *arg);
wait_queue_head_t *tty_get_waitq(int tty_id, int events);

void tty_handle_scancode(uint8_t scancode, int pressed);

#endif /* TSUKASA_TTY_H */
