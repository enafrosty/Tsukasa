/*
 * Project Tsukasa — lwIP platform glue: time base and IRQ-safe SMP protection
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

#include <stdint.h>

#include "lwip/opt.h"
#include "lwip/arch.h"

#include "../drv/pit.h"
#include "../include/smp.h"
#include "../include/spinlock.h"
#include "../sys/time.h"

static spinlock_t g_lwip_lock = SPINLOCK_INIT;
static volatile uint32_t g_lwip_owner = 0xFFFFFFFFu;
static volatile uint32_t g_lwip_depth = 0;

uint32_t sys_now(void)
{
    if (time_is_ready())
        return (uint32_t)time_monotonic_ms();
    uint32_t hz = pit_frequency();
    if (hz == 0)
        hz = 100;
    return (uint32_t)((pit_ticks() * 1000u) / hz);
}

sys_prot_t sys_arch_protect(void)
{
    uint64_t flags;
    __asm__ volatile ("pushfq\n\tpopq %0\n\tcli" : "=r"(flags) : : "memory");
    uint32_t cpu = smp_this_cpu_id();
    if (__atomic_load_n(&g_lwip_depth, __ATOMIC_RELAXED) > 0u &&
        __atomic_load_n(&g_lwip_owner, __ATOMIC_RELAXED) == cpu) {
        g_lwip_depth++;
        return (sys_prot_t)flags;
    }
    spin_lock(&g_lwip_lock);
    __atomic_store_n(&g_lwip_owner, cpu, __ATOMIC_RELAXED);
    __atomic_store_n(&g_lwip_depth, 1u, __ATOMIC_RELAXED);
    return (sys_prot_t)flags;
}

void sys_arch_unprotect(sys_prot_t pval)
{
    uint32_t d = __atomic_load_n(&g_lwip_depth, __ATOMIC_RELAXED);
    if (d > 1u) {
        __atomic_store_n(&g_lwip_depth, d - 1u, __ATOMIC_RELAXED);
    } else if (d == 1u) {
        __atomic_store_n(&g_lwip_owner, 0xFFFFFFFFu, __ATOMIC_RELAXED);
        __atomic_store_n(&g_lwip_depth, 0u, __ATOMIC_RELAXED);
        spin_unlock(&g_lwip_lock);
    }
    if (pval & 0x200u)
        __asm__ volatile ("sti" : : : "memory");
}
