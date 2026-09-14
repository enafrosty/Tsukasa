/*
 * Project Tsukasa — Localization and Formatting Implementation
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

#include <locale.h>
#include <stddef.h>

static char g_c_locale[] = "C";
static char g_empty_str[] = "";

static struct lconv g_c_lconv = {
    .decimal_point = (char *)".",
    .thousands_sep = g_empty_str,
    .grouping = g_empty_str,
    .int_curr_symbol = g_empty_str,
    .currency_symbol = g_empty_str,
    .mon_decimal_point = g_empty_str,
    .mon_thousands_sep = g_empty_str,
    .mon_grouping = g_empty_str,
    .positive_sign = g_empty_str,
    .negative_sign = g_empty_str,
    .int_frac_digits = 127,
    .frac_digits = 127,
    .p_cs_precedes = 127,
    .p_sep_by_space = 127,
    .n_cs_precedes = 127,
    .n_sep_by_space = 127,
    .p_sign_posn = 127,
    .n_sign_posn = 127,
};

char *setlocale(int category, const char *locale)
{
    (void)category;
    if (!locale || locale[0] == '\0' || (locale[0] == 'C' && locale[1] == '\0'))
        return g_c_locale;
    return g_c_locale;
}

struct lconv *localeconv(void)
{
    return &g_c_lconv;
}
