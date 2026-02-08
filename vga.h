/*
 * vga.h - VGA text mode constants and safe output helpers.
 * Use these for all VGA writes to avoid buffer overflows.
 */

#ifndef VGA_H
#define VGA_H

#include <stdint.h>

#define VGA_WIDTH  80
#define VGA_HEIGHT 25

/* VGA text buffer base (physical address, identity-mapped by bootloader). */
#define VGA_BUFFER ((volatile unsigned short *)0xB8000)

/* Default attribute: light grey on black. */
#define VGA_ATTR 0x07

/*
 * Write string to a given row, bounded to VGA_WIDTH. Does not wrap or scroll.
 * Returns the number of characters written (capped at VGA_WIDTH).
 */
unsigned int vga_puts_row(unsigned int row, const char *str);

#endif /* VGA_H */
