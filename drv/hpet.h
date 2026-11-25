/*
 * Project Tsukasa — High Precision Event Timer (HPET) driver interface
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

#ifndef TSUKASA_HPET_H
#define TSUKASA_HPET_H

#include <stdint.h>

int hpet_init(void);
int hpet_available(void);
uint64_t hpet_period_fs(void);
uint64_t hpet_read_counter(void);
uint64_t hpet_read_ns(void);
void hpet_delay_ns(uint64_t ns);

#endif /* TSUKASA_HPET_H */
