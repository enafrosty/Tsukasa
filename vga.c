/*
 * vga.c - Bounds-checked VGA output. All writes are capped to current row size.
 */

#include "vga.h"

unsigned int vga_puts_row(unsigned int row, const char *str)
{
    unsigned int i = 0;
    unsigned int offset;

    if (row >= VGA_HEIGHT)
        return 0;

    offset = row * VGA_WIDTH;

    while (str[i] != '\0' && i < VGA_WIDTH) {
        VGA_BUFFER[offset + i] = (VGA_ATTR << 8) | (unsigned char)str[i];
        i++;
    }
    return i;
}
