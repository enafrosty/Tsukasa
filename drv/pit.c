/*
 * Project Tsukasa — Programmable Interval Timer
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

#include "pit.h"
#include "ps2.h"

static volatile uint64_t g_pit_ticks;
static volatile uint32_t g_pit_hz;

void pit_init(uint32_t hz)
{
    uint32_t divisor;

    if (hz < 20)
        hz = 20;
    if (hz > 1000)
        hz = 1000;

    divisor = 1193182u / hz;
    if (divisor == 0)
        divisor = 1;

    outb(0x43, 0x36);
    outb(0x40, (uint8_t)(divisor & 0xFFu));
    outb(0x40, (uint8_t)((divisor >> 8) & 0xFFu));

    g_pit_ticks = 0;
    g_pit_hz = hz;
}

void pit_irq_tick(void)
{
    g_pit_ticks++;
}

uint64_t pit_ticks(void)
{
    return g_pit_ticks;
}

uint32_t pit_frequency(void)
{
    return g_pit_hz;
}
