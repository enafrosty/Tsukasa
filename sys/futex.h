/*
 * Project Tsukasa — futex: address-keyed block/wake primitive (guide 10)
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

#ifndef SYS_FUTEX_H
#define SYS_FUTEX_H

#include <stdint.h>

#define FUTEX_WAIT 0
#define FUTEX_WAKE 1

/* Block the calling process while *uaddr == expected. */
long kernel_futex_wait(volatile uint32_t *uaddr, uint32_t expected);

/* Wake up to `count` processes blocked on uaddr. */
long kernel_futex_wake(volatile uint32_t *uaddr, int count);

int futex_pending_waiters(const volatile uint32_t *uaddr);

#endif /* SYS_FUTEX_H */
