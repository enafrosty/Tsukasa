/*
 * Project Tsukasa — Simple x86 spinlock using atomic xchg
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

/* Atomically test-and-set the lock. Returns the old value: 0 = lock acquired, 1 = already held. */
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

/* Acquire the spinlock (busy-waits until acquired). */
static inline void spin_lock(spinlock_t *lock)
{
    while (_spin_xchg(lock, 1u) != 0u)
        __asm__ volatile ("pause");
}

/* Release the spinlock. */
static inline void spin_unlock(spinlock_t *lock)
{
    __asm__ volatile ("" ::: "memory");
    *lock = 0u;
}

/* Try to acquire the lock without blocking. Returns 1 if acquired, 0 if already held. */
static inline int spin_trylock(spinlock_t *lock)
{
    return (_spin_xchg(lock, 1u) == 0u) ? 1 : 0;
}

static inline unsigned long spin_lock_irqsave(spinlock_t *lock)
{
    unsigned long flags;
#ifdef __x86_64__
    __asm__ volatile ("pushfq; popq %0; cli" : "=r"(flags) : : "memory");
#else
    __asm__ volatile ("pushfl; popl %0; cli" : "=r"(flags) : : "memory");
#endif
    spin_lock(lock);
    return flags;
}

/* Release a lock taken with spin_lock_irqsave, restoring saved IF. */
static inline void spin_unlock_irqrestore(spinlock_t *lock, unsigned long flags)
{
    spin_unlock(lock);
    if (flags & (1ul << 9))
        __asm__ volatile ("sti" : : : "memory");
}

#endif /* SPINLOCK_H */
