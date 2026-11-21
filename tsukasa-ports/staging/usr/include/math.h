/*
 * Project Tsukasa — Freestanding Mathematical Header (<math.h>)
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

#ifndef _TSUKASA_MATH_H
#define _TSUKASA_MATH_H

#define M_PI       3.14159265358979323846
#define M_PI_2     1.57079632679489661923
#define M_E        2.71828182845904523536
#define M_SQRT2    1.41421356237309504880
#define INFINITY   (__builtin_inff())
#define NAN        (__builtin_nanf(""))
#define HUGE_VAL   (__builtin_huge_val())

double fabs(double x);
double sqrt(double x);
double sin(double x);
double cos(double x);
double asin(double x);
double acos(double x);
double tan(double x);
double atan(double x);
double atan2(double y, double x);
double pow(double base, double exp);
double exp(double x);
double log(double x);
double log2(double x);
double log10(double x);
double floor(double x);
double ceil(double x);
double round(double x);
double fmod(double x, double y);
double ldexp(double x, int exp);
double frexp(double x, int *exp);

float fabsf(float x);
float sqrtf(float x);
float sinf(float x);
float cosf(float x);
float ldexpf(float x, int exp);
float frexpf(float x, int *exp);

#endif /* _TSUKASA_MATH_H */
