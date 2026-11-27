/*
 * Project Tsukasa — High Precision Event Timer (HPET) driver implementation
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

#include "hpet.h"

#include <stddef.h>
#include <stdint.h>

#include "acpi.h"
#include "../include/kprintf.h"
#include "../include/spinlock.h"
#include "../mm/vmm_x64.h"

/* IA-PC HPET 1.0a MMIO register offsets (§2.3.1). */
#define HPET_REG_GEN_CAP_ID   0x000u
#define HPET_REG_GEN_CONF     0x010u
#define HPET_REG_MAIN_CNT_VAL 0x0F0u

#define HPET_CAP_REV_MASK     0xFFULL
#define HPET_CAP_64BIT        (1ULL << 13)
#define HPET_CONF_ENABLE      (1ULL << 0)
#define HPET_CONF_LEG_RT      (1ULL << 1)
#define HPET_MAX_PERIOD_FS    100000000ULL /* 10 MHz minimum per spec §2.3.4 */

static volatile uint8_t *g_hpet_mmio;
static uint64_t g_hpet_period_fs;
static int g_hpet_is_64bit;
static int g_hpet_ready;
static spinlock_t g_hpet_lock = SPINLOCK_INIT;
static uint32_t g_hpet_last_lo32;
static uint32_t g_hpet_hi32;

static inline uint64_t hpet_mmio_read64(uint32_t offset)
{
    return *(volatile uint64_t *)(g_hpet_mmio + offset);
}

static inline void hpet_mmio_write64(uint32_t offset, uint64_t val)
{
    *(volatile uint64_t *)(g_hpet_mmio + offset) = val;
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

int hpet_init(void)
{
    uint64_t phys = 0;
    uint8_t num = 0;
    uint16_t min_tick = 0;
    uintptr_t virt = 0;

    g_hpet_ready = 0;
    g_hpet_mmio = NULL;
    g_hpet_period_fs = 0;
    g_hpet_is_64bit = 0;
    g_hpet_last_lo32 = 0;
    g_hpet_hi32 = 0;

    if (acpi_get_hpet_info(&phys, &num, &min_tick) != 0 || !phys)
        return -1;

    if (vmm_map_io_region(phys, 0x1000u, &virt) != 0 || !virt) {
        kprintf("[hpet] ERROR: MMIO map failed phys=0x%08x%08x\n",
                (uint32_t)(phys >> 32), (uint32_t)phys);
        return -1;
    }

    g_hpet_mmio = (volatile uint8_t *)(uintptr_t)virt;

    /* General Capabilities and ID Register (§2.3.4): bits 63:32 = COUNTER_CLK_PERIOD. */
    uint64_t cap = hpet_mmio_read64(HPET_REG_GEN_CAP_ID);
    uint64_t period_fs = (cap >> 32) & 0xFFFFFFFFULL;
    if ((cap & HPET_CAP_REV_MASK) == 0 || period_fs == 0 || period_fs > HPET_MAX_PERIOD_FS) {
        kprintf("[hpet] ERROR: invalid GEN_CAP_ID=0x%08x%08x\n",
                (uint32_t)(cap >> 32), (uint32_t)cap);
        g_hpet_mmio = NULL;
        return -1;
    }

    g_hpet_period_fs = period_fs;
    g_hpet_is_64bit = (cap & HPET_CAP_64BIT) ? 1 : 0;

    uint64_t conf = hpet_mmio_read64(HPET_REG_GEN_CONF);
    conf &= ~(HPET_CONF_ENABLE | HPET_CONF_LEG_RT);
    hpet_mmio_write64(HPET_REG_GEN_CONF, conf);

    hpet_mmio_write64(HPET_REG_MAIN_CNT_VAL, 0ULL);

    conf |= HPET_CONF_ENABLE;
    hpet_mmio_write64(HPET_REG_GEN_CONF, conf);

    g_hpet_ready = 1;
    kprintf("[hpet] #%u enabled: period_fs=%llu (%ubit) min_tick=%u\n",
            (unsigned)num, (unsigned long long)g_hpet_period_fs,
            g_hpet_is_64bit ? 64u : 32u, (unsigned)min_tick);
    return 0;
}

int hpet_available(void)
{
    return g_hpet_ready;
}

uint64_t hpet_period_fs(void)
{
    return g_hpet_period_fs;
}

uint64_t hpet_read_counter(void)
{
    if (!g_hpet_ready || !g_hpet_mmio)
        return 0;
    if (g_hpet_is_64bit)
        return hpet_mmio_read64(HPET_REG_MAIN_CNT_VAL);

    unsigned long flags = spin_lock_irqsave(&g_hpet_lock);
    uint32_t lo = (uint32_t)hpet_mmio_read64(HPET_REG_MAIN_CNT_VAL);
    if (lo < g_hpet_last_lo32)
        g_hpet_hi32++;
    g_hpet_last_lo32 = lo;
    uint64_t full = ((uint64_t)g_hpet_hi32 << 32) | (uint64_t)lo;
    spin_unlock_irqrestore(&g_hpet_lock, flags);
    return full;
}

uint64_t hpet_read_ns(void)
{
    if (!g_hpet_ready || !g_hpet_period_fs)
        return 0;
    uint64_t cnt = hpet_read_counter();
    return u128_mul_div_u64(cnt, g_hpet_period_fs, 1000000ULL);
}

void hpet_delay_ns(uint64_t ns)
{
    if (!g_hpet_ready || ns == 0)
        return;
    uint64_t start = hpet_read_ns();
    while ((hpet_read_ns() - start) < ns)
        __asm__ volatile ("pause");
}
