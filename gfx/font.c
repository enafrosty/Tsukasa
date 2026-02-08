/*
 * font.c - Font renderer using embedded 8x8 bitmap.
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
