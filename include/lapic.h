/*
 * Project Tsukasa — Local APIC driver interface
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

#ifndef LAPIC_H
#define LAPIC_H

#include <stdint.h>

void lapic_init(void);
void lapic_enable(void);
void lapic_eoi(void);
uint32_t lapic_read_id(void);
void lapic_send_ipi_all(void);
void lapic_send_ipi(uint32_t lapic_id, uint8_t vector);

int lapic_timer_calibrate(uint32_t hz);
void lapic_timer_start_cpu(void);
void lapic_timer_stop_cpu(void);
int lapic_timer_active(void);
void lapic_timer_note_tick(uint32_t cpu_id);
uint64_t lapic_timer_ticks_cpu(uint32_t cpu_id);

#endif /* LAPIC_H */
