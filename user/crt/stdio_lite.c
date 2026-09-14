/*
 * Project Tsukasa — SDK stdio-lite: unbuffered, write()-backed output for
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

#include "tsukasa_sdk.h"

#include <stdarg.h>
#include <stdint.h>

static size_t sl_strlen(const char *s)
{
    size_t n = 0;
    while (s && s[n])
        n++;
    return n;
}

static void sl_out(int fd, const char *buf, size_t n)
{
    if (n > 0)
        write(fd, buf, n);
}

static void sl_udec(int fd, unsigned long v)
{
    char tmp[24];
    int i = (int)sizeof(tmp);
    if (v == 0) {
        sl_out(fd, "0", 1);
        return;
    }
    while (v > 0 && i > 0) {
        tmp[--i] = (char)('0' + (v % 10));
        v /= 10;
    }
    sl_out(fd, &tmp[i], sizeof(tmp) - (size_t)i);
}

static void sl_uhex(int fd, unsigned long v)
{
    static const char digits[] = "0123456789abcdef";
    char tmp[20];
    int i = (int)sizeof(tmp);
    if (v == 0) {
        sl_out(fd, "0", 1);
        return;
    }
    while (v > 0 && i > 0) {
        tmp[--i] = digits[v & 0xF];
        v >>= 4;
    }
    sl_out(fd, &tmp[i], sizeof(tmp) - (size_t)i);
}

int putchar(int c)
{
    char ch = (char)c;
    return (write(1, &ch, 1) == 1) ? c : -1;
}

int dprintf(int fd, const char *fmt, ...)
{
    va_list ap;
    size_t start;
    int count = 0;

    if (!fmt)
        return -1;

    va_start(ap, fmt);
    start = 0;
    for (size_t i = 0; fmt[i]; i++) {
        if (fmt[i] != '%')
            continue;
        sl_out(fd, &fmt[start], i - start);
        count += (int)(i - start);
        i++;
        switch (fmt[i]) {
        case 's': {
            const char *s = va_arg(ap, const char *);
            if (!s)
                s = "(null)";
            sl_out(fd, s, sl_strlen(s));
            count += (int)sl_strlen(s);
            break;
        }
        case 'c': {
            char ch = (char)va_arg(ap, int);
            sl_out(fd, &ch, 1);
            count++;
            break;
        }
        case 'd': {
            long v = (long)va_arg(ap, int);
            if (v < 0) {
                sl_out(fd, "-", 1);
                v = -v;
            }
            sl_udec(fd, (unsigned long)v);
            count++;
            break;
        }
        case 'u':
            sl_udec(fd, (unsigned long)va_arg(ap, unsigned int));
            count++;
            break;
        case 'x':
            sl_uhex(fd, (unsigned long)va_arg(ap, unsigned int));
            count++;
            break;
        case '%':
            sl_out(fd, "%", 1);
            count++;
            break;
        case '\0':
            va_end(ap);
            return count;
        default:
            sl_out(fd, &fmt[i - 1], 2);
            count += 2;
            break;
        }
        start = i + 1;
    }
    sl_out(fd, &fmt[start], sl_strlen(&fmt[start]));
    count += (int)sl_strlen(&fmt[start]);
    va_end(ap);
    return count;
}
