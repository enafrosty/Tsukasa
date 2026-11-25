/*
 * Project Tsukasa — Local APIC driver implementation
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

#include <stddef.h>
#include <stdint.h>
#include "include/lapic.h"
#include "include/kprintf.h"
#include "include/spinlock.h"
#include "arch/x86_64/cpu/idt.h"
#include "drv/acpi.h"
#include "mm/vmm_x64.h"
#include "sys/time.h"

static volatile uint32_t *lapic_base = NULL;
static spinlock_t lapic_lock = SPINLOCK_INIT;
static uint32_t g_lapic_timer_init_count = 0;
static volatile int g_lapic_timer_active = 0;
static volatile uint64_t g_lapic_timer_ticks[32];

#define LAPIC_ID         (0x020 / 4)
#define LAPIC_EOI        (0x0B0 / 4)
#define LAPIC_SVR        (0x0F0 / 4)
#define LAPIC_ICR_LOW    (0x300 / 4)
#define LAPIC_ICR_HIGH   (0x310 / 4)
#define LAPIC_LVT_TIMER  (0x320 / 4)
#define LAPIC_TIMER_INIT (0x380 / 4)
#define LAPIC_TIMER_CURR (0x390 / 4)
#define LAPIC_TIMER_DIV  (0x3E0 / 4)

#define LAPIC_LVT_MASKED   (1u << 16)
#define LAPIC_LVT_PERIODIC (1u << 17)

static inline volatile uint32_t *lapic_ptr(void)
{
    if (!lapic_base) {
        uintptr_t mapped = 0;
        uint64_t phys = acpi_get_lapic_base();
        if (!phys)
            phys = 0xFEE00000ULL;
        if (vmm_map_io_region(phys, 0x1000u, &mapped) != 0)
            return NULL;
        lapic_base = (volatile uint32_t *)(uintptr_t)mapped;
    }
    return lapic_base;
}

static inline void lapic_wait(void)
{
    volatile uint32_t *lapic = lapic_ptr();
    if (!lapic)
        return;

    while (lapic[LAPIC_ICR_LOW] & (1u << 12))
        __asm__ volatile ("pause");
}

void lapic_enable(void)
{
    volatile uint32_t *lapic = lapic_ptr();
    if (!lapic)
        return;

    lapic[LAPIC_SVR] = 0x1FF;
}

void lapic_init(void)
{
    if (!lapic_ptr())
        return;

    lapic_enable();
}

uint32_t lapic_read_id(void)
{
    volatile uint32_t *lapic = lapic_ptr();
    if (!lapic)
        return 0;

    return (lapic[LAPIC_ID] >> 24) & 0xFFu;
}

void lapic_eoi(void)
{
    volatile uint32_t *lapic = lapic_ptr();
    if (!lapic)
        return;

    lapic[LAPIC_EOI] = 0;
}

void lapic_send_ipi_all(void)
{
    volatile uint32_t *lapic = lapic_ptr();
    unsigned long flags;
    if (!lapic)
        return;

    flags = spin_lock_irqsave(&lapic_lock);
    lapic_wait();
    lapic[LAPIC_ICR_HIGH] = 0;
    lapic[LAPIC_ICR_LOW] = (0x41u) | (0b11u << 18) | (1u << 14);
    while (lapic[LAPIC_ICR_LOW] & (1u << 12))
        __asm__ volatile ("pause");
    spin_unlock_irqrestore(&lapic_lock, flags);
}

void lapic_send_ipi(uint32_t lapic_id, uint8_t vector)
{
    volatile uint32_t *lapic = lapic_ptr();
    unsigned long flags;
    if (!lapic)
        return;

    flags = spin_lock_irqsave(&lapic_lock);
    lapic_wait();
    lapic[LAPIC_ICR_HIGH] = (lapic_id << 24);
    lapic[LAPIC_ICR_LOW] = (uint32_t)vector | (1u << 14);
    while (lapic[LAPIC_ICR_LOW] & (1u << 12))
        __asm__ volatile ("pause");
    spin_unlock_irqrestore(&lapic_lock, flags);
}

int lapic_timer_calibrate(uint32_t hz)
{
    volatile uint32_t *lapic = lapic_ptr();
    if (!lapic || hz == 0 || !time_is_ready())
        return -1;

    lapic[LAPIC_LVT_TIMER] = LAPIC_LVT_MASKED | IDT_VEC_LAPIC_TIMER;
    lapic[LAPIC_TIMER_DIV] = 0x3u; /* Divide by 16 (SDM Vol 3A Table 10-1) */
    lapic[LAPIC_TIMER_INIT] = 0xFFFFFFFFu;

    uint64_t t0 = time_monotonic_ns();
    uint32_t spins = 0;
    while ((time_monotonic_ns() - t0) < 10000000ULL && spins < 100000000u) {
        __asm__ volatile ("pause");
        spins++;
    }
    uint64_t dt_ns = time_monotonic_ns() - t0;
    uint32_t curr = lapic[LAPIC_TIMER_CURR];
    lapic[LAPIC_TIMER_INIT] = 0;

    uint32_t elapsed = 0xFFFFFFFFu - curr;
    if (dt_ns == 0 || elapsed == 0)
        return -1;

    /* Scale elapsed 10ms window ticks to exact target frequency hz. */
    uint64_t ticks_per_sec = ((uint64_t)elapsed * 1000000000ULL) / dt_ns;
    uint64_t init_cnt = ticks_per_sec / (uint64_t)hz;
    if (init_cnt < 1000ULL)
        init_cnt = 1000ULL;
    if (init_cnt > 0xFFFFFFFFULL)
        init_cnt = 0xFFFFFFFFULL;

    g_lapic_timer_init_count = (uint32_t)init_cnt;
    kprintf("[lapic] timer calibrated %u Hz (init_count=%u, div=16)\n",
            hz, g_lapic_timer_init_count);
    return 0;
}

void lapic_timer_start_cpu(void)
{
    volatile uint32_t *lapic = lapic_ptr();
    if (!lapic || g_lapic_timer_init_count == 0)
        return;

    lapic[LAPIC_TIMER_DIV] = 0x3u;
    lapic[LAPIC_LVT_TIMER] = (uint32_t)IDT_VEC_LAPIC_TIMER | LAPIC_LVT_PERIODIC;
    lapic[LAPIC_TIMER_INIT] = g_lapic_timer_init_count;
    __atomic_store_n(&g_lapic_timer_active, 1, __ATOMIC_RELEASE);
}

void lapic_timer_stop_cpu(void)
{
    volatile uint32_t *lapic = lapic_ptr();
    if (!lapic)
        return;
    lapic[LAPIC_LVT_TIMER] = LAPIC_LVT_MASKED | IDT_VEC_LAPIC_TIMER;
    lapic[LAPIC_TIMER_INIT] = 0;
}

int lapic_timer_active(void)
{
    return __atomic_load_n(&g_lapic_timer_active, __ATOMIC_ACQUIRE);
}

void lapic_timer_note_tick(uint32_t cpu_id)
{
    if (cpu_id < 32u)
        __atomic_fetch_add(&g_lapic_timer_ticks[cpu_id], 1ULL, __ATOMIC_RELAXED);
}

uint64_t lapic_timer_ticks_cpu(uint32_t cpu_id)
{
    if (cpu_id >= 32u)
        return 0;
    return __atomic_load_n(&g_lapic_timer_ticks[cpu_id], __ATOMIC_RELAXED);
}
