/*
 * Project Tsukasa — Mouse cursor rendering
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

#ifndef CURSOR_H
#define CURSOR_H

#include "blit.h"

#define CURSOR_W  12
#define CURSOR_HEIGHT  19

void cursor_init(void);

/* Update cursor position (called from mouse handler). */
void cursor_move(int dx, int dy);

void cursor_set(int x, int y);

/* Get cursor X position. */
int cursor_x(void);

/* Get cursor Y position. */
int cursor_y(void);

/* Draw the cursor at its current position. Call AFTER drawing all windows. */
void cursor_draw(void);

#endif /* CURSOR_H */
