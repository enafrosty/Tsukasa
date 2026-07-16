#ifndef TSUKASA_STDIO_H
#define TSUKASA_STDIO_H

#include <stdarg.h>
#include <stddef.h>

#define EOF (-1)

int putchar(int c);
int puts(const char *s);

int dprintf(int fd, const char *fmt, ...);
int vdprintf(int fd, const char *fmt, va_list ap);
int printf(const char *fmt, ...);
int vprintf(const char *fmt, va_list ap);

int snprintf(char *out, size_t size, const char *fmt, ...);
int vsnprintf(char *out, size_t size, const char *fmt, va_list ap);

#endif /* TSUKASA_STDIO_H */
