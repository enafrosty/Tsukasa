/*
 * Project Tsukasa — PS/2 mouse driver
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

#ifndef PS2MOUSE_H
#define PS2MOUSE_H

/* Initialize the PS/2 mouse (enable second port, set sample rate, enable data reporting). Must be called... */
void ps2mouse_init(void);

/* IRQ 12 handler. Called from irq_handler when vector == 44. */
void ps2mouse_handler(void);

#endif /* PS2MOUSE_H */
