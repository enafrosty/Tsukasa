/*
 * Project Tsukasa — String to Floating-Point Number Conversion (<stdlib.h>)
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

#include <stdlib.h>
#include <stddef.h>

double strtod(const char *nptr, char **endptr)
{
    double val = 0.0;
    double frac = 0.0;
    double div = 1.0;
    int neg = 0;
    const char *p = nptr;
    if (!p) {
        if (endptr)
            *endptr = (char *)nptr;
        return 0.0;
    }

    while (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r')
        p++;
    if (*p == '-') {
        neg = 1;
        p++;
    } else if (*p == '+') {
        p++;
    }

    while (*p >= '0' && *p <= '9') {
        val = val * 10.0 + (*p - '0');
        p++;
    }

    if (*p == '.') {
        p++;
        while (*p >= '0' && *p <= '9') {
            frac = frac * 10.0 + (*p - '0');
            div *= 10.0;
            p++;
        }
        val += frac / div;
    }

    if (*p == 'e' || *p == 'E') {
        p++;
        int exp_neg = 0;
        if (*p == '-') {
            exp_neg = 1;
            p++;
        } else if (*p == '+') {
            p++;
        }
        int exp = 0;
        while (*p >= '0' && *p <= '9') {
            exp = exp * 10 + (*p - '0');
            p++;
        }
        double factor = 1.0;
        for (int i = 0; i < exp; i++)
            factor *= 10.0;
        if (exp_neg)
            val /= factor;
        else
            val *= factor;
    }

    if (endptr)
        *endptr = (char *)p;
    return neg ? -val : val;
}

float strtof(const char *nptr, char **endptr)
{
    return (float)strtod(nptr, endptr);
}

long double strtold(const char *nptr, char **endptr)
{
    return (long double)strtod(nptr, endptr);
}

double atof(const char *nptr)
{
    return strtod(nptr, NULL);
}
