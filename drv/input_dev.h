/*
 * Project Tsukasa — /dev/keyboard and /dev/mouse device ring buffers
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

#ifndef TSUKASA_INPUT_DEV_H
#define TSUKASA_INPUT_DEV_H

#include <stddef.h>
#include <stdint.h>
#include "../include/vfs_abi.h"

/* Standard input devices */
#define INPUT_DEV_KEYBOARD 0
#define INPUT_DEV_MOUSE    1
#define INPUT_DEV_UNIFIED  2

/* Subsystem initialization */
void input_dev_init(void);

/* Mark a device opened/closed by a reader. */
void input_dev_open(int dev);
void input_dev_close(int dev);

/* Push normalized input event packets into unified queue. */
void input_dev_push(uint32_t type, uint16_t code, int32_t value);
void input_dev_push_event(const struct input_event *ev);

/* Read from unified /dev/input/events stream. */
size_t input_dev_read_events(void *buf, size_t count, int flags);

/* Poll readiness for /dev/input/events. */
int input_dev_poll_events(int events);

/* Backward compatibility helpers for /dev/keyboard and /dev/mouse. */
size_t input_dev_read(int dev, void *buf, size_t count);
int input_dev_poll_readable(int dev);

#endif /* TSUKASA_INPUT_DEV_H */
