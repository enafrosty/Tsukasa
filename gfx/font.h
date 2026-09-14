/*
 * Project Tsukasa — Font rendering interface
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

#ifndef FONT_H
#define FONT_H

#include "blit.h"

/* Draw a character at (x, y). */
void fb_draw_char(int x, int y, char c, color_t fg, color_t bg);

/* Draw a string at (x, y). */
void fb_draw_string(int x, int y, const char *str, color_t fg, color_t bg);

#endif /* FONT_H */
