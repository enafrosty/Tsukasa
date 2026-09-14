/*
 * Project Tsukasa - Spinlock Synchronization Primitives
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

#ifndef SPINLOCK_H
#define SPINLOCK_H

#include <stdint.h>

typedef volatile uint32_t spinlock_t;

#define SPINLOCK_INIT  0u

/* Atomically test and set lock. Returns 0 if acquired, non-zero if held. */
static inline uint32_t _spin_xchg(spinlock_t *lock, uint32_t val)
{
    __asm__ volatile (
        "xchgl %0, %1"
        : "+r"(val), "+m"(*lock)
        :
        : "memory"
    );
    return val;
}

/* Busy-wait until spinlock is acquired. */
static inline void spin_lock(spinlock_t *lock)
{
    while (_spin_xchg(lock, 1u) != 0u)
        __asm__ volatile ("pause");
}

/* Release spinlock with compiler memory barrier. */
static inline void spin_unlock(spinlock_t *lock)
{
    __asm__ volatile ("" ::: "memory");
    *lock = 0u;
}

/* Non-blocking lock attempt. Returns 1 if acquired, 0 if held. */
static inline int spin_trylock(spinlock_t *lock)
{
    return (_spin_xchg(lock, 1u) == 0u) ? 1 : 0;
}

/* Save RFLAGS, disable interrupts, and acquire spinlock. */
static inline unsigned long spin_lock_irqsave(spinlock_t *lock)
{
    unsigned long flags;
#ifdef __x86_64__
    __asm__ volatile (
        "pushfq\n\t"
        "popq %0\n\t"
        "cli"
        : "=r"(flags)
        :
        : "memory"
    );
#else
    __asm__ volatile (
        "pushfl\n\t"
        "popl %0\n\t"
        "cli"
        : "=r"(flags)
        :
        : "memory"
    );
#endif
    spin_lock(lock);
    return flags;
}

/* Release spinlock and restore previous RFLAGS interrupt state. */
static inline void spin_unlock_irqrestore(spinlock_t *lock, unsigned long flags)
{
    spin_unlock(lock);
#ifdef __x86_64__
    __asm__ volatile (
        "pushq %0\n\t"
        "popfq"
        :
        : "r"(flags)
        : "memory", "cc"
    );
#else
    __asm__ volatile (
        "pushl %0\n\t"
        "popfl"
        :
        : "r"(flags)
        : "memory", "cc"
    );
#endif
}

#endif /* SPINLOCK_H */
