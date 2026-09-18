/*
 * Project Tsukasa — x86_64 Interrupt Descriptor Table Definitions
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

#ifndef TSUKASA_X64_IDT_H
#define TSUKASA_X64_IDT_H

#include <stdint.h>
#include "sys/panic.h"

void idt_init_x64(void);
void idt_load(void);
void idt_exception_handler_x64(interrupt_frame_t *frame);

#endif