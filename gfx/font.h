/*
 * font.h - Font rendering interface.
 */

#ifndef FONT_H
#define FONT_H

#include "blit.h"

/**
 * Draw a character at (x, y).
 *
 * @param x X position.
 * @param y Y position.
 * @param c Character (ASCII).
 * @param fg Foreground color.
 * @param bg Background color.
 */
void fb_draw_char(int x, int y, char c, color_t fg, color_t bg);

/**
 * Draw a string at (x, y).
 *
 * @param x X position.
 * @param y Y position.
 * @param str Null-terminated string.
 * @param fg Foreground color.
 * @param bg Background color.
 */
void fb_draw_string(int x, int y, const char *str, color_t fg, color_t bg);

#endif /* FONT_H */
