/*
 * Project Tsukasa — I/O APIC driver: MADT-routed device interrupt delivery
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

#ifndef TSUKASA_IOAPIC_H
#define TSUKASA_IOAPIC_H

/* All-or-nothing cutover: on success the PIC is fully masked and IOAPIC-delivered interrupts are... */

/* Route timer/keyboard/mouse through the IOAPIC and mask the PIC. */
int ioapic_init(void);

/* 1 once ioapic_init() completed the cutover (EOIs must go to the LAPIC). */
int ioapic_active(void);

#endif /* TSUKASA_IOAPIC_H */
