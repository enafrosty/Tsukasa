/*
 * Project Tsukasa — futex: physical-address-keyed block/wake primitive
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

#define FUTEX_WAIT             0
#define FUTEX_WAKE             1
#define FUTEX_WAIT_BITSET      9
#define FUTEX_WAKE_BITSET      10
#define FUTEX_PRIVATE_FLAG     128
#define FUTEX_CLOCK_REALTIME   256
#define FUTEX_CMD_MASK         (~(FUTEX_PRIVATE_FLAG | FUTEX_CLOCK_REALTIME))
#define FUTEX_BITSET_MATCH_ANY 0xFFFFFFFFU

long kernel_futex_wait(volatile uint32_t *uaddr, uint32_t expected);
long kernel_futex_wait_ex(volatile uint32_t *uaddr,
                          uint32_t expected,
                          int has_timeout,
                          uint64_t mono_deadline_ns,
                          uint32_t bitset);

long kernel_futex_wake(volatile uint32_t *uaddr, int count);
long kernel_futex_wake_ex(volatile uint32_t *uaddr, int count, uint32_t bitset);

int futex_pending_waiters(const volatile uint32_t *uaddr);

#endif /* SYS_FUTEX_H */
