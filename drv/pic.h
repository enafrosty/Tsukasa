/*
 * Project Tsukasa — Programmable Interrupt Controller
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

#ifndef PIC_H
#define PIC_H

#include <stdint.h>

#include "../drv/ps2.h"

void pic_init(void);

/* Send End-Of-Interrupt to PIC. @param irq IRQ number (0-15). */
void pic_eoi(unsigned int irq);

void pic_mask_irq(uint8_t irq);
void pic_unmask_irq(uint8_t irq);

/* The chips stay remapped to vectors 32-47 so any spurious assertion still lands on a sane vector; they... */
void pic_disable(void);

#endif /* PIC_H */
