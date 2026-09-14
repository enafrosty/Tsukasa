/*
 * Project Tsukasa — IRQ callback registration for PIC IRQ lines
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

#ifndef TSUKASA_IRQ_H
#define TSUKASA_IRQ_H

#include <stdint.h>

typedef void (*irq_callback_t)(uint8_t irq, void *ctx);

int irq_register_handler(uint8_t irq, irq_callback_t callback, void *ctx);
void irq_unregister_handler(uint8_t irq);

/* On x86_64 with the IOAPIC live, device interrupts are acknowledged at the LAPIC (the 8259 never saw them);... */
void irq_ack(unsigned int irq);

#endif /* TSUKASA_IRQ_H */
