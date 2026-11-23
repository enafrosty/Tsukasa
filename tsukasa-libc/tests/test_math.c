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
#include <stdint.h>
#include <string.h>
#include <limits.h>
#include <float.h>
#include <math.h>
#include "../../scripts/test/tsk_test.h"

static uint64_t dbl_bits(double x)
{
    uint64_t u;
    memcpy(&u, &x, sizeof(u));
    return u;
}

static uint32_t flt_bits(float x)
{
    uint32_t u;
    memcpy(&u, &x, sizeof(u));
    return u;
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
    int passed = 0;
    int total = 10;

    /* 1. fabs */
    if (fabs(-42.5) == 42.5 && fabs(13.37) == 13.37 && fabsf(-5.5f) == 5.5f) {
        TSK_TEST_PASS("math", "fabs");
        passed++;
    } else {
        TSK_TEST_FAIL("math", "fabs", "floating point absolute value mismatch");
        return 1;
    }

    /* 2. sqrt */
    double nan_val = sqrt(-1.0);
    if (sqrt(0.0) == 0.0 && sqrt(144.0) == 12.0 &&
        approx_eq(sqrt(2.0), M_SQRT2, 0.00001) &&
        (nan_val != nan_val) && sqrtf(25.0f) == 5.0f) {
        TSK_TEST_PASS("math", "sqrt");
        passed++;
    } else {
        TSK_TEST_FAIL("math", "sqrt", "square root computation failed");
        return 2;
    }

    /* 3. floor, ceil, round */
    if (floor(3.7) == 3.0 && floor(-3.7) == -4.0 &&
        ceil(3.2) == 4.0 && ceil(-3.2) == -3.0 &&
        round(3.5) == 4.0 && round(3.4) == 3.0 && round(-3.5) == -4.0) {
        TSK_TEST_PASS("math", "rounding");
        passed++;
    } else {
        TSK_TEST_FAIL("math", "rounding", "floor/ceil/round mismatch");
        return 3;
    }

    /* 4. fmod */
    if (approx_eq(fmod(5.5, 2.0), 1.5, 0.00001) &&
        approx_eq(fmod(-5.5, 2.0), -1.5, 0.00001)) {
        TSK_TEST_PASS("math", "fmod");
        passed++;
    } else {
        TSK_TEST_FAIL("math", "fmod", "floating point remainder mismatch");
        return 4;
    }

    /* 5. Trigonometry: sin, cos, tan */
    if (approx_eq(sin(0.0), 0.0, 0.00001) &&
        approx_eq(sin(M_PI_2), 1.0, 0.00001) &&
        approx_eq(sin(M_PI), 0.0, 0.00001) &&
        approx_eq(sin(-M_PI_2), -1.0, 0.00001) &&
        approx_eq(cos(0.0), 1.0, 0.00001) &&
        approx_eq(cos(M_PI_2), 0.0, 0.00001) &&
        approx_eq(cos(M_PI), -1.0, 0.00001) &&
        approx_eq(tan(0.0), 0.0, 0.00001) &&
        approx_eq(tan(M_PI / 4.0), 1.0, 0.0001) &&
        approx_eq((double)sinf((float)M_PI_2), 1.0, 0.0001) &&
        approx_eq((double)cosf(0.0f), 1.0, 0.0001)) {
        TSK_TEST_PASS("math", "trig");
        passed++;
    } else {
        TSK_TEST_FAIL("math", "trig", "trigonometric calculation mismatch");
        return 5;
    }

    /* 6. atan, atan2, acos */
    double nan_acos1 = acos(2.0);
    double nan_acos2 = acos(-2.0);
    if (approx_eq(atan(1.0), M_PI / 4.0, 0.0001) &&
        approx_eq(atan2(1.0, 1.0), M_PI / 4.0, 0.0001) &&
        approx_eq(atan2(1.0, -1.0), 3.0 * M_PI / 4.0, 0.0001) &&
        approx_eq(acos(1.0), 0.0, 0.0001) &&
        approx_eq(acos(0.0), M_PI_2, 0.0001) &&
        approx_eq(acos(-1.0), M_PI, 0.0001) &&
        (nan_acos1 != nan_acos1) && (nan_acos2 != nan_acos2)) {
        TSK_TEST_PASS("math", "inverse_trig");
        passed++;
    } else {
        TSK_TEST_FAIL("math", "inverse_trig", "inverse trigonometric calculation mismatch");
        return 6;
    }

    /* 7. exp, log */
    if (approx_eq(exp(0.0), 1.0, 0.00001) &&
        approx_eq(exp(1.0), M_E, 0.0001) &&
        approx_eq(log(1.0), 0.0, 0.00001) &&
        approx_eq(log(M_E), 1.0, 0.0001)) {
        TSK_TEST_PASS("math", "exp_log");
        passed++;
    } else {
        TSK_TEST_FAIL("math", "exp_log", "exponential or logarithm mismatch");
        return 7;
    }

    /* 8. pow */
    if (pow(2.0, 0.0) == 1.0 &&
        pow(2.0, 10.0) == 1024.0 &&
        pow(3.0, 3.0) == 27.0 &&
        approx_eq(pow(2.0, 0.5), M_SQRT2, 0.00001)) {
        TSK_TEST_PASS("math", "pow");
        passed++;
    } else {
        TSK_TEST_FAIL("math", "pow", "power function mismatch");
        return 8;
    }

    /* 9. ldexp / ldexpf */
    {
        double sub_d = ldexp(1.0, -1074);
        float sub_f = ldexpf(1.0f, -149);
        double ld_nan = ldexp((double)NAN, 5);
        float lf_nan = ldexpf(NAN, 3);
        if (ldexp(1.5, 3) == 12.0 &&
            ldexp(-3.0, -2) == -0.75 &&
            ldexp(1.0, 0) == 1.0 &&
            dbl_bits(ldexp(0.0, 100)) == 0ULL &&
            dbl_bits(ldexp(-0.0, 100)) == (1ULL << 63) &&
            ldexp((double)INFINITY, -50) == (double)INFINITY &&
            ldexp(-(double)INFINITY, 50) == -(double)INFINITY &&
            (ld_nan != ld_nan) &&
            sub_d > 0.0 &&
            ldexp(sub_d, 1) == ldexp(1.0, -1073) &&
            ldexp(DBL_MIN, -1) == DBL_MIN * 0.5 &&
            ldexp(DBL_MIN * 0.5, 1) == DBL_MIN &&
            ldexp(1.0, 1024) == (double)INFINITY &&
            ldexp(-1.0, INT_MAX) == -(double)INFINITY &&
            dbl_bits(ldexp(1.0, -1075)) == 0ULL &&
            dbl_bits(ldexp(-1.0, INT_MIN)) == (1ULL << 63) &&
            ldexpf(1.5f, 3) == 12.0f &&
            ldexpf(-3.0f, -2) == -0.75f &&
            flt_bits(ldexpf(0.0f, 50)) == 0U &&
            flt_bits(ldexpf(-0.0f, 50)) == (1U << 31) &&
            ldexpf(INFINITY, -10) == INFINITY &&
            ldexpf(-INFINITY, 10) == -INFINITY &&
            (lf_nan != lf_nan) &&
            sub_f > 0.0f &&
            ldexpf(sub_f, 1) == ldexpf(1.0f, -148) &&
            ldexpf(1.0f, INT_MAX) == INFINITY &&
            flt_bits(ldexpf(-1.0f, INT_MIN)) == (1U << 31)) {
            TSK_TEST_PASS("math", "ldexp_ldexpf");
            passed++;
        } else {
            TSK_TEST_FAIL("math", "ldexp_ldexpf", "ldexp/ldexpf mismatch");
            return 9;
        }
    }

    /* 10. frexp / frexpf */
    {
        int e = 99;
        int ok = 1;
        if (frexp(6.0, &e) != 0.75 || e != 3) ok = 0;
        if (frexp(-0.5, &e) != -0.5 || e != 0) ok = 0;
        e = 99;
        if (dbl_bits(frexp(0.0, &e)) != 0ULL || e != 0) ok = 0;
        e = 99;
        if (dbl_bits(frexp(-0.0, &e)) != (1ULL << 63) || e != 0) ok = 0;
        e = 99;
        if (frexp((double)INFINITY, &e) != (double)INFINITY || e != 0) ok = 0;
        e = 99;
        if (frexp(-(double)INFINITY, &e) != -(double)INFINITY || e != 0) ok = 0;
        e = 99;
        double fn = frexp((double)NAN, &e);
        if (!(fn != fn) || e != 0) ok = 0;
        if (frexp(ldexp(1.0, -1074), &e) != 0.5 || e != -1073) ok = 0;
        if (frexp(-ldexp(3.0, -1074), &e) != -0.75 || e != -1072) ok = 0;
        if (frexpf(6.0f, &e) != 0.75f || e != 3) ok = 0;
        e = 99;
        if (flt_bits(frexpf(-0.0f, &e)) != (1U << 31) || e != 0) ok = 0;
        e = 99;
        if (frexpf(INFINITY, &e) != INFINITY || e != 0) ok = 0;
        e = 99;
        float fnf = frexpf(NAN, &e);
        if (!(fnf != fnf) || e != 0) ok = 0;
        if (frexpf(ldexpf(1.0f, -149), &e) != 0.5f || e != -148) ok = 0;

        if (ok) {
            TSK_TEST_PASS("math", "frexp_frexpf");
            passed++;
        } else {
            TSK_TEST_FAIL("math", "frexp_frexpf", "frexp/frexpf mismatch");
            return 10;
        }
    }

    TSK_TEST_DONE("math", passed, total);
    return 0;
}
