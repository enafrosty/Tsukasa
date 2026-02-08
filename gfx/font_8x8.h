/*
 * font_8x8.h - Embedded 8x8 bitmap font (VGA-style).
 * 256 glyphs, 8 bytes each (1 byte per row).
 */

#ifndef FONT_8X8_H
#define FONT_8X8_H

#include <stdint.h>

#define FONT_WIDTH  8
#define FONT_HEIGHT 8

extern const uint8_t font_8x8[128][8];

#endif /* FONT_8X8_H */
