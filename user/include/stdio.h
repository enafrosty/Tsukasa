/*
 * Project Tsukasa — Standard I/O header
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
