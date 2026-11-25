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

#include "futex.h"

#include <stddef.h>

#include "../include/errno.h"
#include "../include/spinlock.h"
#include "../mm/heap.h"
#include "../mm/vmm_x64.h"
#include "../proc/process.h"
#include "../sys/time.h"

#define FUTEX_BUCKETS 128

typedef struct futex_waiter {
    uint64_t phys_key;
    uint32_t bitset;
    process_t *proc;
    struct futex_waiter *next;
} futex_waiter_t;

typedef struct futex_bucket {
    futex_waiter_t *head;
    spinlock_t lock;
} futex_bucket_t;

static futex_bucket_t g_futex_buckets[FUTEX_BUCKETS];

static int futex_phys_key(const volatile uint32_t *uaddr, uint64_t *key_out)
{
    uintptr_t va = (uintptr_t)uaddr;
    if (!uaddr || !key_out)
        return -EINVAL;
    if (va >= 0xFFFF800000000000ULL) {
        uint64_t pa = vmm_virt_to_phys(va);
        *key_out = pa ? pa : (uint64_t)va;
        return 0;
    }

    process_t *cur = process_current();
    vm_space_t *vs = process_vm_space(cur);
    if (vs && vs->pml4_phys) {
        uint64_t pg_phys = 0;
        uint64_t pte_flags = 0;
        uintptr_t pg_va = va & ~(uintptr_t)(VMM_X64_PAGE_SIZE - 1ULL);
        if (vmm_query_page(vs->pml4_phys, pg_va, &pg_phys, &pte_flags) != 0 ||
            !(pte_flags & VMM_X64_PTE_PRESENT)) {
            (void)*uaddr;
            if (vmm_query_page(vs->pml4_phys, pg_va, &pg_phys, &pte_flags) != 0 ||
                !(pte_flags & VMM_X64_PTE_PRESENT)) {
                return -EFAULT;
            }
        }
        *key_out = pg_phys + (uint64_t)(va & (VMM_X64_PAGE_SIZE - 1ULL));
        return 0;
    }

    *key_out = (uint64_t)va;
    return 0;
}

static futex_bucket_t *futex_bucket_for_key(uint64_t key)
{
    uint64_t h = (key >> 2) ^ (key >> 12);
    return &g_futex_buckets[h & (FUTEX_BUCKETS - 1)];
}

long kernel_futex_wait_ex(volatile uint32_t *uaddr,
                          uint32_t expected,
                          int has_timeout,
                          uint64_t mono_deadline_ns,
                          uint32_t bitset)
{
    futex_bucket_t *b;
    futex_waiter_t *node;
    futex_waiter_t **pp;
    unsigned long irqf;
    uint64_t phys_key = 0;
    process_t *self = process_current();

    if (!uaddr || !self || bitset == 0u)
        return -EINVAL;
    if (futex_phys_key(uaddr, &phys_key) != 0)
        return -EFAULT;

    node = (futex_waiter_t *)kmalloc(sizeof(*node));
    if (!node)
        return -ENOMEM;
    node->phys_key = phys_key;
    node->bitset = bitset;
    node->proc = self;
    node->next = NULL;

    b = futex_bucket_for_key(phys_key);
    irqf = spin_lock_irqsave(&b->lock);

    if (*uaddr != expected) {
        spin_unlock_irqrestore(&b->lock, irqf);
        kfree(node);
        return -EAGAIN;
    }

    if (has_timeout && time_monotonic_ns() >= mono_deadline_ns) {
        spin_unlock_irqrestore(&b->lock, irqf);
        kfree(node);
        return -ETIMEDOUT;
    }

    node->next = b->head;
    b->head = node;
    process_block_current_ns(has_timeout ? mono_deadline_ns : 0ULL);
    spin_unlock_irqrestore(&b->lock, irqf);

    for (;;) {
        process_yield();

        irqf = spin_lock_irqsave(&b->lock);
        int still_queued = 0;
        for (pp = &b->head; *pp; pp = &(*pp)->next) {
            if (*pp == node && (*pp)->proc == self) {
                still_queued = 1;
                break;
            }
        }
        if (!still_queued) {
            spin_unlock_irqrestore(&b->lock, irqf);
            return 0;
        }

        if (process_has_deliverable_signal(self)) {
            *pp = node->next;
            spin_unlock_irqrestore(&b->lock, irqf);
            kfree(node);
            return -EINTR;
        }
        if (has_timeout && time_monotonic_ns() >= mono_deadline_ns) {
            *pp = node->next;
            spin_unlock_irqrestore(&b->lock, irqf);
            kfree(node);
            return -ETIMEDOUT;
        }

        process_block_current_ns(has_timeout ? mono_deadline_ns : 0ULL);
        spin_unlock_irqrestore(&b->lock, irqf);
    }
}

long kernel_futex_wait(volatile uint32_t *uaddr, uint32_t expected)
{
    return kernel_futex_wait_ex(uaddr, expected, 0, 0ULL, FUTEX_BITSET_MATCH_ANY);
}

long kernel_futex_wake_ex(volatile uint32_t *uaddr, int count, uint32_t bitset)
{
    futex_bucket_t *b;
    futex_waiter_t *collected = NULL;
    futex_waiter_t **pp;
    futex_waiter_t *n;
    unsigned long irqf;
    uint64_t phys_key = 0;
    long woken = 0;

    if (!uaddr || bitset == 0u)
        return -EINVAL;
    if (count <= 0)
        return 0;
    if (futex_phys_key(uaddr, &phys_key) != 0)
        return -EFAULT;

    b = futex_bucket_for_key(phys_key);
    irqf = spin_lock_irqsave(&b->lock);

    pp = &b->head;
    while (*pp && woken < count) {
        n = *pp;
        if (n->phys_key != phys_key || (n->bitset & bitset) == 0u) {
            pp = &n->next;
            continue;
        }
        *pp = n->next;
        if (process_wake_blocked(n->proc))
            woken++;
        n->next = collected;
        collected = n;
    }

    spin_unlock_irqrestore(&b->lock, irqf);

    while (collected) {
        n = collected;
        collected = collected->next;
        kfree(n);
    }
    return woken;
}

long kernel_futex_wake(volatile uint32_t *uaddr, int count)
{
    return kernel_futex_wake_ex(uaddr, count, FUTEX_BITSET_MATCH_ANY);
}

int futex_pending_waiters(const volatile uint32_t *uaddr)
{
    futex_bucket_t *b;
    futex_waiter_t *n;
    unsigned long irqf;
    uint64_t phys_key = 0;
    int cnt = 0;

    if (!uaddr || futex_phys_key(uaddr, &phys_key) != 0)
        return 0;

    b = futex_bucket_for_key(phys_key);
    irqf = spin_lock_irqsave(&b->lock);
    for (n = b->head; n; n = n->next) {
        if (n->phys_key == phys_key)
            cnt++;
    }
    spin_unlock_irqrestore(&b->lock, irqf);
    return cnt;
}
