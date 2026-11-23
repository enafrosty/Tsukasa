/*
 * Project Tsukasa — Freestanding Mathematical Implementation (<math.h>)
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

#include "math.h"
#include <stdint.h>
#include <string.h>

#define LN2 0.693147180559945309417

double fabs(double x)
{
    return __builtin_fabs(x);
}

float fabsf(float x)
{
    return __builtin_fabsf(x);
}

double sqrt(double x)
{
    if (x < 0.0)
        return NAN;
    double res;
    __asm__ ("sqrtsd %1, %0" : "=x"(res) : "x"(x));
    return res;
}

float sqrtf(float x)
{
    if (x < 0.0f)
        return NAN;
    float res;
    __asm__ ("sqrtss %1, %0" : "=x"(res) : "x"(x));
    return res;
}

double floor(double x)
{
    if (x != x || x == INFINITY || x == -INFINITY)
        return x;
    if (fabs(x) >= 4503599627370496.0)
        return x;
    long long i = (long long)x;
    double d = (double)i;
    if (x < 0.0 && d > x)
        return d - 1.0;
    return d;
}

double ceil(double x)
{
    if (x != x || x == INFINITY || x == -INFINITY)
        return x;
    if (fabs(x) >= 4503599627370496.0)
        return x;
    long long i = (long long)x;
    double d = (double)i;
    if (x > 0.0 && d < x)
        return d + 1.0;
    return d;
}

double round(double x)
{
    if (x != x || x == INFINITY || x == -INFINITY)
        return x;
    if (x >= 0.0)
        return floor(x + 0.5);
    return ceil(x - 0.5);
}

double fmod(double x, double y)
{
    if (y == 0.0 || x != x || y != y)
        return NAN;
    if (x == INFINITY || x == -INFINITY)
        return NAN;
    if (y == INFINITY || y == -INFINITY)
        return x;
    long long q = (long long)(x / y);
    return x - (double)q * y;
}

static double reduce_angle(double x)
{
    double q = round(x / (2.0 * M_PI));
    return x - q * (2.0 * M_PI);
}

static double sin_kernel(double x)
{
    double x2 = x * x;
    double term = x;
    double sum = x;

    term *= -x2 / (2.0 * 3.0);
    sum += term;

    term *= -x2 / (4.0 * 5.0);
    sum += term;

    term *= -x2 / (6.0 * 7.0);
    sum += term;

    term *= -x2 / (8.0 * 9.0);
    sum += term;

    term *= -x2 / (10.0 * 11.0);
    sum += term;

    term *= -x2 / (12.0 * 13.0);
    sum += term;

    return sum;
}

double sin(double x)
{
    if (x != x || x == INFINITY || x == -INFINITY)
        return NAN;
    x = reduce_angle(x);
    if (x > M_PI_2)
        x = M_PI - x;
    else if (x < -M_PI_2)
        x = -M_PI - x;
    return sin_kernel(x);
}

double cos(double x)
{
    return sin(x + M_PI_2);
}

double tan(double x)
{
    double c = cos(x);
    if (c == 0.0)
        return (sin(x) >= 0.0) ? INFINITY : -INFINITY;
    return sin(x) / c;
}

static inline int dbl_signbit(double x)
{
    union {
        double d;
        uint64_t u;
    } v = { .d = x };
    return (int)(v.u >> 63);
}

static inline double sqrt_1m_x2(double x)
{
    double a = fabs(x);
    double d = (a > 0.5) ? (1.0 - a) * (1.0 + a) : (1.0 - x * x);
    return sqrt(d);
}

static double atan_kernel(double t)
{
    const double TAN_PI_12 = 0.267949192431122707; /* 2 - sqrt(3) */
    const double SQRT3 = 1.7320508075688772935;
    const double PI_OVER_6 = M_PI / 6.0;
    double offset = 0.0;

    /* Reduce t > (2 - sqrt(3)) using atan(t) = pi/6 + atan((sqrt(3)*t - 1) / (sqrt(3) + t)) */
    if (t > TAN_PI_12) {
        t = (SQRT3 * t - 1.0) / (SQRT3 + t);
        offset = PI_OVER_6;
    }

    double t2 = t * t;
    double num = t;
    double sum = 0.0;
    double denom = 1.0;
    int sign = 1;

    for (int i = 0; i < 14; i++) {
        sum += sign * (num / denom);
        num *= t2;
        denom += 2.0;
        sign = -sign;
    }
    return offset + sum;
}

double atan2(double y, double x)
{
    if (x != x || y != y)
        return NAN;

    int y_neg = dbl_signbit(y);
    int x_neg = dbl_signbit(x);

    if (x == 0.0) {
        if (y > 0.0)
            return M_PI_2;
        if (y < 0.0)
            return -M_PI_2;
        if (x_neg)
            return y_neg ? -M_PI : M_PI;
        return y_neg ? -0.0 : 0.0;
    }

    double r;
    double abs_y = fabs(y);
    double abs_x = fabs(x);

    if (abs_x == INFINITY && abs_y == INFINITY) {
        r = M_PI / 4.0;
    } else if (abs_y <= abs_x) {
        double t = abs_y / abs_x;
        r = atan_kernel(t);
    } else {
        double t = abs_x / abs_y;
        r = M_PI_2 - atan_kernel(t);
    }

    if (x_neg)
        r = M_PI - r;
    if (y_neg)
        r = -r;

    return r;
}

double atan(double x)
{
    return atan2(x, 1.0);
}

double asin(double x)
{
    if (x < -1.0 || x > 1.0 || x != x)
        return NAN;
    return atan2(x, sqrt_1m_x2(x));
}

double acos(double x)
{
    if (x < -1.0 || x > 1.0 || x != x)
        return NAN;
    return atan2(sqrt_1m_x2(x), x);
}

double log(double x)
{
    if (x != x || x < 0.0)
        return NAN;
    if (x == 0.0)
        return -INFINITY;
    if (x == INFINITY)
        return INFINITY;

    int exp = 0;
    while (x >= 2.0) {
        x *= 0.5;
        exp++;
    }
    while (x < 1.0) {
        x *= 2.0;
        exp--;
    }

    double u = (x - 1.0) / (x + 1.0);
    double u2 = u * u;
    double term = u;
    double sum = 0.0;
    for (int i = 1; i <= 25; i += 2) {
        sum += term / (double)i;
        term *= u2;
    }
    return (double)exp * LN2 + 2.0 * sum;
}

double log2(double x)
{
    return log(x) / LN2;
}

double log10(double x)
{
    return log(x) / 2.30258509299404568402;
}

double exp(double x)
{
    if (x != x)
        return NAN;
    if (x > 709.782712893384)
        return INFINITY;
    if (x < -708.3964185322641)
        return 0.0;

    long long k = (long long)round(x / LN2);
    double r = x - (double)k * LN2;

    double term = 1.0;
    double sum = 1.0;
    for (int i = 1; i <= 20; i++) {
        term *= r / (double)i;
        sum += term;
    }

    double p2 = 1.0;
    if (k > 0) {
        for (long long j = 0; j < k; j++)
            p2 *= 2.0;
    } else if (k < 0) {
        for (long long j = 0; j < -k; j++)
            p2 *= 0.5;
    }

    return sum * p2;
}

double pow(double base, double exp_val)
{
    if (exp_val == 0.0)
        return 1.0;
    if (base == 1.0)
        return 1.0;
    if (base == 0.0) {
        if (exp_val > 0.0)
            return 0.0;
        return INFINITY;
    }

    long long iexp = (long long)exp_val;
    if ((double)iexp == exp_val) {
        double result = 1.0;
        double b = (exp_val < 0) ? (1.0 / base) : base;
        long long p = (iexp < 0) ? -iexp : iexp;
        while (p > 0) {
            if (p & 1)
                result *= b;
            b *= b;
            p >>= 1;
        }
        return result;
    }

    if (base < 0.0)
        return NAN;

    return exp(exp_val * log(base));
}

float sinf(float x)
{
    return (float)sin((double)x);
}

float cosf(float x)
{
    return (float)cos((double)x);
}

static uint64_t dbl_to_u64(double x)
{
    uint64_t u;
    memcpy(&u, &x, sizeof(u));
    return u;
}

static double u64_to_dbl(uint64_t u)
{
    double x;
    memcpy(&x, &u, sizeof(x));
    return x;
}

static uint32_t flt_to_u32(float x)
{
    uint32_t u;
    memcpy(&u, &x, sizeof(u));
    return u;
}

static float u32_to_flt(uint32_t u)
{
    float x;
    memcpy(&x, &u, sizeof(x));
    return x;
}

double ldexp(double x, int exp)
{
    uint64_t bits = dbl_to_u64(x);
    uint64_t sign = bits & (1ULL << 63);
    uint64_t abs_bits = bits & 0x7FFFFFFFFFFFFFFFULL;
    int e_bits = (int)(abs_bits >> 52);
    uint64_t mant = abs_bits & 0x000FFFFFFFFFFFFFULL;

    if (abs_bits == 0 || e_bits == 0x7FF || exp == 0)
        return x;

    int64_t e = (int64_t)e_bits;
    if (e == 0) {
        e = 1;
        while ((mant & (1ULL << 52)) == 0) {
            mant <<= 1;
            e--;
        }
    } else {
        mant |= (1ULL << 52);
    }

    e += (int64_t)exp;
    if (e >= 2047)
        return u64_to_dbl(sign | 0x7FF0000000000000ULL);
    if (e >= 1)
        return u64_to_dbl(sign | ((uint64_t)e << 52) | (mant & 0x000FFFFFFFFFFFFFULL));

    int64_t shift = 1 - e;
    if (shift > 53)
        return u64_to_dbl(sign);

    uint64_t kept = mant >> (unsigned)shift;
    uint64_t half = 1ULL << (unsigned)(shift - 1);
    uint64_t rem = mant & ((1ULL << (unsigned)shift) - 1ULL);
    if (rem > half || (rem == half && (kept & 1ULL) != 0))
        kept++;

    return u64_to_dbl(sign | kept);
}

float ldexpf(float x, int exp)
{
    uint32_t bits = flt_to_u32(x);
    uint32_t sign = bits & 0x80000000U;
    uint32_t abs_bits = bits & 0x7FFFFFFFU;
    int e_bits = (int)(abs_bits >> 23);
    uint32_t mant = abs_bits & 0x007FFFFFU;

    if (abs_bits == 0 || e_bits == 0xFF || exp == 0)
        return x;

    int64_t e = (int64_t)e_bits;
    if (e == 0) {
        e = 1;
        while ((mant & (1U << 23)) == 0) {
            mant <<= 1;
            e--;
        }
    } else {
        mant |= (1U << 23);
    }

    e += (int64_t)exp;
    if (e >= 255)
        return u32_to_flt(sign | 0x7F800000U);
    if (e >= 1)
        return u32_to_flt(sign | ((uint32_t)e << 23) | (mant & 0x007FFFFFU));

    int64_t shift = 1 - e;
    if (shift > 24)
        return u32_to_flt(sign);

    uint32_t kept = mant >> (unsigned)shift;
    uint32_t half = 1U << (unsigned)(shift - 1);
    uint32_t rem = mant & ((1U << (unsigned)shift) - 1U);
    if (rem > half || (rem == half && (kept & 1U) != 0))
        kept++;

    return u32_to_flt(sign | kept);
}

double frexp(double x, int *exp)
{
    uint64_t bits = dbl_to_u64(x);
    uint64_t sign = bits & (1ULL << 63);
    uint64_t abs_bits = bits & 0x7FFFFFFFFFFFFFFFULL;
    int e_bits = (int)(abs_bits >> 52);
    uint64_t mant = abs_bits & 0x000FFFFFFFFFFFFFULL;

    if (abs_bits == 0 || e_bits == 0x7FF) {
        if (exp)
            *exp = 0;
        return x;
    }

    int out_exp;
    if (e_bits == 0) {
        int shift = 0;
        while ((mant & (1ULL << 52)) == 0) {
            mant <<= 1;
            shift++;
        }
        out_exp = 1 - 1022 - shift;
    } else {
        out_exp = e_bits - 1022;
    }

    if (exp)
        *exp = out_exp;
    return u64_to_dbl(sign | (1022ULL << 52) | (mant & 0x000FFFFFFFFFFFFFULL));
}

float frexpf(float x, int *exp)
{
    uint32_t bits = flt_to_u32(x);
    uint32_t sign = bits & 0x80000000U;
    uint32_t abs_bits = bits & 0x7FFFFFFFU;
    int e_bits = (int)(abs_bits >> 23);
    uint32_t mant = abs_bits & 0x007FFFFFU;

    if (abs_bits == 0 || e_bits == 0xFF) {
        if (exp)
            *exp = 0;
        return x;
    }

    int out_exp;
    if (e_bits == 0) {
        int shift = 0;
        while ((mant & (1U << 23)) == 0) {
            mant <<= 1;
            shift++;
        }
        out_exp = 1 - 126 - shift;
    } else {
        out_exp = e_bits - 126;
    }

    if (exp)
        *exp = out_exp;
    return u32_to_flt(sign | (126U << 23) | (mant & 0x007FFFFFU));
}
