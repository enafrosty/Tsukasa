/*
 * serial.h - COM1 serial port driver (x86 port I/O).
 * QEMU captures output with: -serial stdio
 */

#ifndef SERIAL_H
#define SERIAL_H

#include <stdint.h>

/** Initialise COM1 at 115200 baud, 8N1.  Must be called before serial_putc. */
void serial_init(void);

/** Transmit one character (blocks until THR is empty). */
void serial_putc(char c);

/** Transmit a NUL-terminated string. */
void serial_puts(const char *s);

#endif /* SERIAL_H */
