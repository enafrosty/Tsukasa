/*
 * Project Tsukasa — CMOS Real-Time Clock driver (x86 ports 0x70 / 0x71)
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

#ifndef RTC_H
#define RTC_H

#include <stdint.h>

typedef struct {
    uint8_t  sec;
    uint8_t  min;
    uint8_t  hour;
    uint8_t  day;
    uint8_t  month;
    uint16_t year;
} rtc_time_t;

/* Read and cache the initial RTC time.  Must be called once during boot. */
void rtc_init(void);

/* Fill *t with the current RTC date/time. Values are all decoded from BCD and represent real calendar numbers. */
void rtc_read(rtc_time_t *t);

#endif /* RTC_H */
