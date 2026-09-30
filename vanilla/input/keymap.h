/*
 * Project Tsukasa — Unified Evdev to ASCII Keymap Header
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

#ifndef _VANILLA_KEYMAP_H
#define _VANILLA_KEYMAP_H

#include <stdint.h>

/*
 * Convert an evdev keycode and shift state to an ASCII character.
 * Returns 0 if the keycode has no printable ASCII representation,
 * or if code is out of the handled range.
 *
 * shift: 1 if Shift is held, 0 otherwise.
 *
 * This is a US-QWERTY layout; no dead keys, no compose, no AltGr.
 */
char vanilla_evdev_to_ascii(uint16_t code, int shift);

#endif /* _VANILLA_KEYMAP_H */
