/*
 * Project Tsukasa — High-resolution kernel timekeeping interface
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

#ifndef TSUKASA_SYS_TIME_H
#define TSUKASA_SYS_TIME_H

#include <stdint.h>

void time_init(void);
int time_is_ready(void);

uint64_t time_monotonic_ns(void);
uint64_t time_monotonic_ms(void);
int64_t time_realtime_ns(void);
int time_set_realtime_ns(int64_t ns);
uint64_t time_clock_res_ns(int clk_id);

uint64_t time_tsc_hz(void);
const char *time_clocksource_name(void);

#endif /* TSUKASA_SYS_TIME_H */
