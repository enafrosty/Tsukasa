/*
 * Project Tsukasa — Formatted String Input Scanning (<stdio.h>)
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

#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>

int vsscanf(const char *str, const char *fmt, va_list ap)
{
    if (!str || !fmt)
        return 0;

    int matched = 0;
    const char *s = str;
    const char *f = fmt;

    while (*f && *s) {
        if (*f == ' ' || *f == '\t' || *f == '\n' || *f == '\r') {
            while (*f == ' ' || *f == '\t' || *f == '\n' || *f == '\r')
                f++;
            while (*s == ' ' || *s == '\t' || *s == '\n' || *s == '\r')
                s++;
            continue;
        }

        if (*f != '%') {
            if (*f != *s)
                break;
            f++;
            s++;
            continue;
        }

        f++;
        if (*f == '%') {
            if (*s != '%')
                break;
            f++;
            s++;
            continue;
        }

        int is_long = 0;
        int is_longlong = 0;
        if (*f == 'l') {
            f++;
            if (*f == 'l') {
                is_longlong = 1;
                f++;
            } else {
                is_long = 1;
            }
        }

        if (*f == 'd' || *f == 'i') {
            while (*s == ' ' || *s == '\t' || *s == '\n' || *s == '\r')
                s++;
            char *endp = NULL;
            long long val = strtoll(s, &endp, (*f == 'i') ? 0 : 10);
            if (endp == s)
                break;
            s = endp;
            if (is_longlong)
                *va_arg(ap, long long *) = val;
            else if (is_long)
                *va_arg(ap, long *) = (long)val;
            else
                *va_arg(ap, int *) = (int)val;
            matched++;
            f++;
        } else if (*f == 'u') {
            while (*s == ' ' || *s == '\t' || *s == '\n' || *s == '\r')
                s++;
            char *endp = NULL;
            unsigned long long val = strtoull(s, &endp, 10);
            if (endp == s)
                break;
            s = endp;
            if (is_longlong)
                *va_arg(ap, unsigned long long *) = val;
            else if (is_long)
                *va_arg(ap, unsigned long *) = (unsigned long)val;
            else
                *va_arg(ap, unsigned int *) = (unsigned int)val;
            matched++;
            f++;
        } else if (*f == 'x' || *f == 'X') {
            while (*s == ' ' || *s == '\t' || *s == '\n' || *s == '\r')
                s++;
            char *endp = NULL;
            unsigned long long val = strtoull(s, &endp, 16);
            if (endp == s)
                break;
            s = endp;
            if (is_longlong)
                *va_arg(ap, unsigned long long *) = val;
            else if (is_long)
                *va_arg(ap, unsigned long *) = (unsigned long)val;
            else
                *va_arg(ap, unsigned int *) = (unsigned int)val;
            matched++;
            f++;
        } else if (*f == 'f') {
            while (*s == ' ' || *s == '\t' || *s == '\n' || *s == '\r')
                s++;
            char *endp = NULL;
            double val = strtod(s, &endp);
            if (endp == s)
                break;
            s = endp;
            if (is_long)
                *va_arg(ap, double *) = val;
            else
                *va_arg(ap, float *) = (float)val;
            matched++;
            f++;
        } else if (*f == 's') {
            while (*s == ' ' || *s == '\t' || *s == '\n' || *s == '\r')
                s++;
            char *dest = va_arg(ap, char *);
            if (!*s)
                break;
            while (*s && *s != ' ' && *s != '\t' && *s != '\n' && *s != '\r')
                *dest++ = *s++;
            *dest = '\0';
            matched++;
            f++;
        } else if (*f == 'c') {
            char *dest = va_arg(ap, char *);
            if (!*s)
                break;
            *dest = *s++;
            matched++;
            f++;
        } else {
            break;
        }
    }

    return matched;
}

int sscanf(const char *str, const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    int rc = vsscanf(str, fmt, ap);
    va_end(ap);
    return rc;
}
