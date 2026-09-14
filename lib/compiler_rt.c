/*
 * Project Tsukasa — include <stdint.h>
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

#include <stdint.h>

typedef uint64_t u64;
typedef int64_t s64;

static u64 udivmod64(u64 num, u64 den, u64 *rem)
{
    u64 q = 0;
    int shift = 0;

    if (den == 0) {
        if (rem)
            *rem = 0;
        return 0;
    }

    if (num < den) {
        if (rem)
            *rem = num;
        return 0;
    }

    while (den <= (num >> 1)) {
        den <<= 1;
        shift++;
    }

    while (shift >= 0) {
        q <<= 1;
        if (num >= den) {
            num -= den;
            q |= 1;
        }
        den >>= 1;
        shift--;
    }

    if (rem)
        *rem = num;
    return q;
}

u64 __udivmoddi4(u64 num, u64 den, u64 *rem)
{
    return udivmod64(num, den, rem);
}

u64 __udivdi3(u64 num, u64 den)
{
    return udivmod64(num, den, 0);
}

u64 __umoddi3(u64 num, u64 den)
{
    u64 rem = 0;
    (void)udivmod64(num, den, &rem);
    return rem;
}

static u64 abs_s64_to_u64(s64 v)
{
    u64 uv = (u64)v;
    if (v >= 0)
        return uv;
    return (~uv) + 1;
}

s64 __divdi3(s64 num, s64 den)
{
    int neg = ((num < 0) != (den < 0));
    u64 uq = udivmod64(abs_s64_to_u64(num), abs_s64_to_u64(den), 0);
    s64 q = (s64)uq;
    return neg ? -q : q;
}

s64 __moddi3(s64 num, s64 den)
{
    u64 rem = 0;
    (void)udivmod64(abs_s64_to_u64(num), abs_s64_to_u64(den), &rem);
    if (num < 0)
        return -(s64)rem;
    return (s64)rem;
}
