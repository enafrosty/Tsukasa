/*
 * Project Tsukasa — Test Freestanding Math Library
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
#include <unistd.h>
#include <math.h>

static void assert_test(int cond, const char *msg)
{
    if (!cond) {
        printf("[FAIL] Math assertion: %s\n", msg);
        exit(1);
    }
}

static int approx_eq(double a, double b, double eps)
{
    double diff = fabs(a - b);
    return diff <= eps;
}

int main(int argc, char **argv)
{
    (void)argc;
    (void)argv;

    printf("[TEST] Starting tsukasa-libc libm test suite...\n");

    /* fabs */
    assert_test(fabs(-42.5) == 42.5, "fabs(-42.5) == 42.5");
    assert_test(fabs(13.37) == 13.37, "fabs(13.37) == 13.37");
    assert_test(fabsf(-5.5f) == 5.5f, "fabsf(-5.5f) == 5.5f");

    /* sqrt */
    assert_test(sqrt(0.0) == 0.0, "sqrt(0.0) == 0.0");
    assert_test(sqrt(144.0) == 12.0, "sqrt(144.0) == 12.0");
    assert_test(approx_eq(sqrt(2.0), M_SQRT2, 0.00001), "sqrt(2.0) == M_SQRT2");
    double nan_val = sqrt(-1.0);
    assert_test(nan_val != nan_val, "sqrt(-1.0) is NaN");

    /* sqrtf */
    assert_test(sqrtf(25.0f) == 5.0f, "sqrtf(25.0f) == 5.0f");

    /* floor, ceil, round */
    assert_test(floor(3.7) == 3.0, "floor(3.7) == 3.0");
    assert_test(floor(-3.7) == -4.0, "floor(-3.7) == -4.0");
    assert_test(ceil(3.2) == 4.0, "ceil(3.2) == 4.0");
    assert_test(ceil(-3.2) == -3.0, "ceil(-3.2) == -3.0");
    assert_test(round(3.5) == 4.0, "round(3.5) == 4.0");
    assert_test(round(3.4) == 3.0, "round(3.4) == 3.0");
    assert_test(round(-3.5) == -4.0, "round(-3.5) == -4.0");

    /* fmod */
    assert_test(approx_eq(fmod(5.5, 2.0), 1.5, 0.00001), "fmod(5.5, 2.0) == 1.5");
    assert_test(approx_eq(fmod(-5.5, 2.0), -1.5, 0.00001), "fmod(-5.5, 2.0) == -1.5");

    /* Trigonometry: sin, cos, tan */
    assert_test(approx_eq(sin(0.0), 0.0, 0.00001), "sin(0.0) == 0.0");
    assert_test(approx_eq(sin(M_PI_2), 1.0, 0.00001), "sin(PI/2) == 1.0");
    assert_test(approx_eq(sin(M_PI), 0.0, 0.00001), "sin(PI) == 0.0");
    assert_test(approx_eq(sin(-M_PI_2), -1.0, 0.00001), "sin(-PI/2) == -1.0");

    assert_test(approx_eq(cos(0.0), 1.0, 0.00001), "cos(0.0) == 1.0");
    assert_test(approx_eq(cos(M_PI_2), 0.0, 0.00001), "cos(PI/2) == 0.0");
    assert_test(approx_eq(cos(M_PI), -1.0, 0.00001), "cos(PI) == -1.0");

    assert_test(approx_eq(tan(0.0), 0.0, 0.00001), "tan(0.0) == 0.0");
    assert_test(approx_eq(tan(M_PI / 4.0), 1.0, 0.0001), "tan(PI/4) == 1.0");

    /* atan, atan2, acos */
    assert_test(approx_eq(atan(1.0), M_PI / 4.0, 0.0001), "atan(1.0) == PI/4");
    assert_test(approx_eq(atan2(1.0, 1.0), M_PI / 4.0, 0.0001), "atan2(1.0, 1.0) == PI/4");
    assert_test(approx_eq(atan2(1.0, -1.0), 3.0 * M_PI / 4.0, 0.0001), "atan2(1.0, -1.0) == 3PI/4");
    assert_test(approx_eq(acos(1.0), 0.0, 0.0001), "acos(1.0) == 0.0");
    assert_test(approx_eq(acos(0.0), M_PI_2, 0.0001), "acos(0.0) == PI/2");
    assert_test(approx_eq(acos(-1.0), M_PI, 0.0001), "acos(-1.0) == PI");
    double nan_acos = acos(2.0);
    assert_test(nan_acos != nan_acos, "acos(2.0) is NaN");
    nan_acos = acos(-2.0);
    assert_test(nan_acos != nan_acos, "acos(-2.0) is NaN");

    /* exp, log */
    assert_test(approx_eq(exp(0.0), 1.0, 0.00001), "exp(0.0) == 1.0");
    assert_test(approx_eq(exp(1.0), M_E, 0.0001), "exp(1.0) == M_E");
    assert_test(approx_eq(log(1.0), 0.0, 0.00001), "log(1.0) == 0.0");
    assert_test(approx_eq(log(M_E), 1.0, 0.0001), "log(M_E) == 1.0");

    /* pow */
    assert_test(pow(2.0, 0.0) == 1.0, "pow(2.0, 0.0) == 1.0");
    assert_test(pow(2.0, 10.0) == 1024.0, "pow(2.0, 10.0) == 1024.0");
    assert_test(pow(3.0, 3.0) == 27.0, "pow(3.0, 3.0) == 27.0");
    assert_test(approx_eq(pow(2.0, 0.5), M_SQRT2, 0.00001), "pow(2.0, 0.5) == M_SQRT2");

    /* Single precision trig */
    assert_test(approx_eq((double)sinf((float)M_PI_2), 1.0, 0.0001), "sinf(PI/2) == 1.0");
    assert_test(approx_eq((double)cosf(0.0f), 1.0, 0.0001), "cosf(0.0) == 1.0");

    printf("[PASS] All libm mathematical assertions passed successfully.\n");
    return 0;
}
