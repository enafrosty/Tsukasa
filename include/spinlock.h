/*
 * spinlock.h - Simple x86 spinlock using atomic xchg.
 * Freestanding, no external dependencies.
 *
 * Usage:
 *   static spinlock_t my_lock = SPINLOCK_INIT;
 *   spin_lock(&my_lock);
 *   ... critical section ...
 *   spin_unlock(&my_lock);
 */

#ifndef SPINLOCK_H
#define SPINLOCK_H

#include <stdint.h>

typedef volatile uint32_t spinlock_t;

#define SPINLOCK_INIT  0u

/**
 * Atomically test-and-set the lock.
 * Returns the old value: 0 = lock acquired, 1 = already held.
 */
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

/** Acquire the spinlock (busy-waits until acquired). */
static inline void spin_lock(spinlock_t *lock)
{
    while (_spin_xchg(lock, 1u) != 0u)
        __asm__ volatile ("pause");
}

/** Release the spinlock. */
static inline void spin_unlock(spinlock_t *lock)
{
    __asm__ volatile ("" ::: "memory");   /* compiler barrier */
    *lock = 0u;
}

/**
 * Try to acquire the lock without blocking.
 * Returns 1 if acquired, 0 if already held.
 */
static inline int spin_trylock(spinlock_t *lock)
{
    return (_spin_xchg(lock, 1u) == 0u) ? 1 : 0;
}

#endif /* SPINLOCK_H */
