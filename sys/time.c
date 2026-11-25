/*
 * Project Tsukasa — High-resolution HPET + 128-bit calibrated TSC timekeeping
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

#include "time.h"

#include <stddef.h>
#include <stdint.h>

#include "../drv/hpet.h"
#include "../drv/pit.h"
#include "../drv/rtc.h"
#include "../include/io.h"
#include "../include/kprintf.h"
#include "../include/spinlock.h"
#include "../include/vfs_abi.h"

static uint64_t g_tsc_hz;
static uint64_t g_tsc_base;
static uint64_t g_mono_base_ns;
static volatile uint64_t g_last_mono_ns;
static volatile int64_t g_realtime_offset_ns;
static spinlock_t g_time_lock = SPINLOCK_INIT;
static int g_time_ready;
static int g_tsc_calibrated;

static inline uint64_t read_tsc_ordered(void)
{
    uint32_t lo = 0, hi = 0;
    __asm__ volatile ("lfence\n\trdtsc" : "=a"(lo), "=d"(hi) : : "memory");
    return ((uint64_t)hi << 32) | (uint64_t)lo;
}

static inline uint64_t u128_mul_div_u64(uint64_t a, uint64_t b, uint64_t d)
{
    if (d == 0)
        return 0;
    __uint128_t prod = (__uint128_t)a * (__uint128_t)b;
    uint64_t hi = (uint64_t)(prod >> 64);
    uint64_t lo = (uint64_t)prod;
    if (hi >= d)
        hi %= d;
    uint64_t q = 0;
    uint64_t rem = 0;
    __asm__ volatile ("divq %3"
                      : "=a"(q), "=d"(rem)
                      : "d"(hi), "r"(d), "a"(lo));
    return q;
}

static int cpu_has_tsc(void)
{
    uint32_t eax = 1u, ebx = 0u, ecx = 0u, edx = 0u;
    __asm__ volatile ("cpuid" : "+a"(eax), "=b"(ebx), "=c"(ecx), "=d"(edx) : : "memory");
    return (edx & (1u << 4)) != 0;
}

static int cpu_has_invariant_tsc(void)
{
    uint32_t eax = 0x80000000u, ebx = 0u, ecx = 0u, edx = 0u;
    __asm__ volatile ("cpuid" : "+a"(eax), "=b"(ebx), "=c"(ecx), "=d"(edx) : : "memory");
    if (eax < 0x80000007u)
        return 0;
    eax = 0x80000007u;
    __asm__ volatile ("cpuid" : "+a"(eax), "=b"(ebx), "=c"(ecx), "=d"(edx) : : "memory");
    return (edx & (1u << 8)) != 0;
}

static int is_leap_year(int year)
{
    if ((year % 4) != 0)
        return 0;
    if ((year % 100) != 0)
        return 1;
    return (year % 400) == 0;
}

static int month_days(int year, int month)
{
    static const int days[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    if (month == 2)
        return is_leap_year(year) ? 29 : 28;
    if (month < 1 || month > 12)
        return 30;
    return days[month - 1];
}

static int64_t rtc_to_unix_sec(const rtc_time_t *rt)
{
    if (!rt)
        return 0;
    int64_t days = 0;
    for (int y = 1970; y < (int)rt->year; y++)
        days += is_leap_year(y) ? 366 : 365;
    for (int m = 1; m < (int)rt->month; m++)
        days += month_days((int)rt->year, m);
    days += (int64_t)rt->day - 1;

    int64_t sec = days * 86400LL;
    sec += (int64_t)rt->hour * 3600LL;
    sec += (int64_t)rt->min * 60LL;
    sec += (int64_t)rt->sec;
    return sec;
}

/* Measure TSC frequency with PIT channel 2 one-shot gate when HPET is absent. */
static uint64_t calibrate_tsc_via_pit_ch2(void)
{
    uint32_t reload = 11932u; /* ~10 ms at 1,193,182 Hz */
    uint8_t gate = io_inb(0x61);
    gate = (uint8_t)((gate & 0xFDu) | 0x01u); /* gate high, speaker data off */
    io_outb(0x61, gate);

    io_outb(0x43, 0xB0u); /* Ch2, lobyte/hibyte, mode 0, binary */
    io_outb(0x42, (uint8_t)(reload & 0xFFu));
    io_outb(0x42, (uint8_t)((reload >> 8) & 0xFFu));

    uint64_t t0 = read_tsc_ordered();
    uint32_t spins = 0;
    while (!(io_inb(0x61) & 0x20u) && spins < 50000000u)
        spins++;
    uint64_t t1 = read_tsc_ordered();
    if (spins >= 50000000u || t1 <= t0)
        return 0;
    /* 11932 / 1193182 s -> dt_ns = 11932 * 1e9 / 1193182 = 10000150 ns */
    uint64_t hz = u128_mul_div_u64(t1 - t0, 1000000000ULL, 10000150ULL);
    return hz;
}

void time_init(void)
{
    g_time_ready = 0;
    g_tsc_calibrated = 0;
    g_tsc_hz = 0;
    g_tsc_base = 0;
    g_mono_base_ns = 0;
    g_last_mono_ns = 0;

    (void)hpet_init();

    if (cpu_has_tsc()) {
        if (hpet_available()) {
            uint64_t h0 = hpet_read_ns();
            uint64_t t0 = read_tsc_ordered();
            while ((hpet_read_ns() - h0) < 10000000ULL)
                __asm__ volatile ("pause");
            uint64_t h1 = hpet_read_ns();
            uint64_t t1 = read_tsc_ordered();
            uint64_t dh = h1 - h0;
            if (dh > 0 && t1 > t0) {
                g_tsc_hz = u128_mul_div_u64(t1 - t0, 1000000000ULL, dh);
                g_tsc_base = t1;
                g_mono_base_ns = h1;
                g_last_mono_ns = h1;
                g_tsc_calibrated = 1;
            }
        } else {
            uint64_t hz = calibrate_tsc_via_pit_ch2();
            if (hz > 0) {
                g_tsc_hz = hz;
                g_tsc_base = read_tsc_ordered();
                g_mono_base_ns = 0;
                g_last_mono_ns = 0;
                g_tsc_calibrated = 1;
            }
        }
    }

    g_time_ready = 1;

    rtc_time_t rt;
    rtc_read(&rt);
    int64_t epoch_sec = rtc_to_unix_sec(&rt);
    uint64_t mono_now = time_monotonic_ns();
    g_realtime_offset_ns = (epoch_sec * 1000000000LL) - (int64_t)mono_now;

    kprintf("[time] clocksource=%s tsc_hz=%llu invariant_tsc=%d\n",
            time_clocksource_name(),
            (unsigned long long)g_tsc_hz,
            cpu_has_invariant_tsc());
}

int time_is_ready(void)
{
    return g_time_ready;
}

uint64_t time_tsc_hz(void)
{
    return g_tsc_hz;
}

const char *time_clocksource_name(void)
{
    if (g_tsc_calibrated && hpet_available())
        return "tsc+hpet";
    if (g_tsc_calibrated)
        return "tsc";
    if (hpet_available())
        return "hpet";
    return "pit";
}

uint64_t time_monotonic_ns(void)
{
    uint64_t ns;
    if (g_tsc_calibrated && g_tsc_hz > 0) {
        uint64_t tsc = read_tsc_ordered();
        uint64_t delta = (tsc > g_tsc_base) ? (tsc - g_tsc_base) : 0ULL;
        ns = g_mono_base_ns + u128_mul_div_u64(delta, 1000000000ULL, g_tsc_hz);
    } else if (hpet_available()) {
        ns = hpet_read_ns();
    } else {
        uint32_t hz = pit_frequency();
        if (hz == 0)
            hz = 100;
        ns = (pit_ticks() * 1000000000ULL) / hz;
    }

    for (;;) {
        uint64_t prev = __atomic_load_n(&g_last_mono_ns, __ATOMIC_ACQUIRE);
        if (ns <= prev)
            return prev;
        if (__atomic_compare_exchange_n(&g_last_mono_ns, &prev, ns, 1,
                                        __ATOMIC_SEQ_CST, __ATOMIC_ACQUIRE))
            return ns;
    }
}

uint64_t time_monotonic_ms(void)
{
    return time_monotonic_ns() / 1000000ULL;
}

int64_t time_realtime_ns(void)
{
    unsigned long flags = spin_lock_irqsave(&g_time_lock);
    int64_t off = g_realtime_offset_ns;
    spin_unlock_irqrestore(&g_time_lock, flags);
    return off + (int64_t)time_monotonic_ns();
}

int time_set_realtime_ns(int64_t ns)
{
    if (ns < 0)
        return -1;
    uint64_t mono = time_monotonic_ns();
    unsigned long flags = spin_lock_irqsave(&g_time_lock);
    g_realtime_offset_ns = ns - (int64_t)mono;
    spin_unlock_irqrestore(&g_time_lock, flags);
    return 0;
}

uint64_t time_clock_res_ns(int clk_id)
{
    if (clk_id != TSK_CLOCK_REALTIME && clk_id != TSK_CLOCK_MONOTONIC &&
        clk_id != TSK_CLOCK_MONOTONIC_RAW && clk_id != TSK_CLOCK_BOOTTIME)
        return 0;
    if (g_tsc_calibrated && g_tsc_hz >= 1000000000ULL)
        return 1ULL;
    if (hpet_available() && hpet_period_fs() > 0) {
        uint64_t ns = (hpet_period_fs() + 999999ULL) / 1000000ULL;
        return ns ? ns : 1ULL;
    }
    return 10000000ULL;
}
