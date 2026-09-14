/*
 * Project Tsukasa — Font renderer using embedded 8x8 bitmap
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

#include "font.h"
#include "font_8x8.h"
#include "blit.h"
#include "../drv/fb.h"
#include <stddef.h>

void fb_draw_char(int x, int y, char c, color_t fg, color_t bg)
{
    unsigned int idx = (unsigned char)c;
    if (idx >= 128)
        idx = 0;

    for (int row = 0; row < FONT_HEIGHT; row++) {
        uint8_t line = font_8x8[idx][row];
        for (int col = 0; col < FONT_WIDTH; col++) {
            color_t color = (line & (1u << (7 - col))) ? fg : bg;
            fb_putpixel(x + col, y + row, color);
        }
    }
}

void fb_draw_string(int x, int y, const char *str, color_t fg, color_t bg)
{
    int ox = x;
    while (str && *str) {
        if (*str == '\n') {
            y += FONT_HEIGHT;
            x = ox;
        } else {
            fb_draw_char(x, y, *str, fg, bg);
            x += FONT_WIDTH;
        }
        str++;
    }
}
