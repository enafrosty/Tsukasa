/*
 * kprintf.h - Freestanding kernel printf.
 * Outputs to COM1 serial (captured by QEMU -serial stdio).
 * Supports: %d %i %u %x %X %s %c %% and width/zero-pad modifiers.
 */

#ifndef KPRINTF_H
#define KPRINTF_H

#include <stddef.h>
#include <stdarg.h>

/**
 * Print a formatted string to the serial port.
 * Returns the number of characters written.
 */
int kprintf(const char *fmt, ...);

/**
 * Format a string into buf (at most n bytes including NUL terminator).
 * Returns the number of characters that would have been written (excluding NUL).
 */
int ksprintf(char *buf, size_t n, const char *fmt, ...);

/** Bare string output to serial (no formatting). */
void kputs(const char *s);

#endif /* KPRINTF_H */
