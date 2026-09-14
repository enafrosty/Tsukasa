/*
 * Project Tsukasa — PIT timer driver for IRQ0 preemption ticks
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

#ifndef TSUKASA_PIT_H
#define TSUKASA_PIT_H

#include <stdint.h>

void pit_init(uint32_t hz);
void pit_irq_tick(void);
uint64_t pit_ticks(void);
uint32_t pit_frequency(void);

#endif /* TSUKASA_PIT_H */
