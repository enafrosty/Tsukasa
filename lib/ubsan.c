/*
 * Project Tsukasa — Undefined Behavior Sanitizer Runtime Handlers
 *
 * Copyright (C) 2025-2026 frosty (@enafrosty) and Project Tsukasa contributors.
 *
 * Project Tsukasa was created and is maintained by frosty (@enafrosty).
 * This program is free software: you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation, either version 3 of the License, or (at you
 * option) any later version. See the top-level LICENSE file.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 */

#include <stddef.h>
#include <stdint.h>
#include "include/ubsan.h"
#include "include/kprintf.h"
#include "include/ksymbols.h"

#define UBSAN_HISTORY_CAP 128

static const struct source_location *g_reported_locs[UBSAN_HISTORY_CAP];
static size_t g_reported_count = 0;
static size_t g_evict_idx = 0;

void kernel_panic(void *regs, const char *msg);

static int ubsan_already_reported(const struct source_location *loc)
{
    if (!loc)
        return 0;

    for (size_t i = 0; i < g_reported_count && i < UBSAN_HISTORY_CAP; i++) {
        if (g_reported_locs[i] == loc)
            return 1;
    }

    if (g_reported_count < UBSAN_HISTORY_CAP) {
        g_reported_locs[g_reported_count++] = loc;
    } else {
        g_reported_locs[g_evict_idx] = loc;
        g_evict_idx = (g_evict_idx + 1) % UBSAN_HISTORY_CAP;
    }

    return 0;
}

static void ubsan_report(const struct source_location *loc, const char *what)
{
    if (ubsan_already_reported(loc))
        return;

    const char *file = (loc && loc->file) ? loc->file : "??";
    uint32_t line = loc ? loc->line : 0;
    uint32_t col = loc ? loc->column : 0;

    kprintf("[ubsan] %s:%u:%u: %s\n", file, line, col, what);
    k_backtrace(0, 0, 8);

#if defined(CONFIG_UBSAN_PANIC)
    kernel_panic(NULL, "ubsan check failed");
#endif
}

void __ubsan_handle_add_overflow(struct overflow_data *data, unsigned long lhs, unsigned long rhs)
{
    (void)lhs;
    (void)rhs;
    ubsan_report(data ? &data->loc : NULL, "signed integer overflow (add)");
}

void __ubsan_handle_sub_overflow(struct overflow_data *data, unsigned long lhs, unsigned long rhs)
{
    (void)lhs;
    (void)rhs;
    ubsan_report(data ? &data->loc : NULL, "signed integer overflow (sub)");
}

void __ubsan_handle_mul_overflow(struct overflow_data *data, unsigned long lhs, unsigned long rhs)
{
    (void)lhs;
    (void)rhs;
    ubsan_report(data ? &data->loc : NULL, "signed integer overflow (mul)");
}

void __ubsan_handle_negate_overflow(struct overflow_data *data, unsigned long old_val)
{
    (void)old_val;
    ubsan_report(data ? &data->loc : NULL, "signed integer overflow (negate)");
}

void __ubsan_handle_divrem_overflow(struct overflow_data *data, unsigned long lhs, unsigned long rhs)
{
    (void)lhs;
    (void)rhs;
    ubsan_report(data ? &data->loc : NULL, "division by zero or signed div overflow");
}

void __ubsan_handle_shift_out_of_bounds(struct shift_out_of_bounds_data *data, unsigned long lhs, unsigned long rhs)
{
    (void)lhs;
    (void)rhs;
    ubsan_report(data ? &data->loc : NULL, "shift exponent out of bounds or negative");
}

void __ubsan_handle_out_of_bounds(struct out_of_bounds_data *data, unsigned long index)
{
    (void)index;
    ubsan_report(data ? &data->loc : NULL, "array index out of bounds");
}

void __ubsan_handle_builtin_unreachable(struct unreachable_data *data)
{
    ubsan_report(data ? &data->loc : NULL, "execution reached unreachable code");
}

void __ubsan_handle_load_invalid_value(struct invalid_value_data *data, unsigned long val)
{
    (void)val;
    ubsan_report(data ? &data->loc : NULL, "load of invalid boolean or enum value");
}

void __ubsan_handle_type_mismatch_v1(struct type_mismatch_data_v1 *data, unsigned long ptr)
{
    (void)ptr;
    ubsan_report(data ? &data->loc : NULL, "type mismatch or null pointer access");
}

/* Abort variants emitted when recovery is disabled */
void __ubsan_handle_add_overflow_abort(struct overflow_data *data, unsigned long lhs, unsigned long rhs)
{
    __ubsan_handle_add_overflow(data, lhs, rhs);
}

void __ubsan_handle_sub_overflow_abort(struct overflow_data *data, unsigned long lhs, unsigned long rhs)
{
    __ubsan_handle_sub_overflow(data, lhs, rhs);
}

void __ubsan_handle_mul_overflow_abort(struct overflow_data *data, unsigned long lhs, unsigned long rhs)
{
    __ubsan_handle_mul_overflow(data, lhs, rhs);
}

void __ubsan_handle_negate_overflow_abort(struct overflow_data *data, unsigned long old_val)
{
    __ubsan_handle_negate_overflow(data, old_val);
}

void __ubsan_handle_divrem_overflow_abort(struct overflow_data *data, unsigned long lhs, unsigned long rhs)
{
    __ubsan_handle_divrem_overflow(data, lhs, rhs);
}

void __ubsan_handle_shift_out_of_bounds_abort(struct shift_out_of_bounds_data *data, unsigned long lhs, unsigned long rhs)
{
    __ubsan_handle_shift_out_of_bounds(data, lhs, rhs);
}

void __ubsan_handle_out_of_bounds_abort(struct out_of_bounds_data *data, unsigned long index)
{
    __ubsan_handle_out_of_bounds(data, index);
}

void __ubsan_handle_builtin_unreachable_abort(struct unreachable_data *data)
{
    __ubsan_handle_builtin_unreachable(data);
}

void __ubsan_handle_load_invalid_value_abort(struct invalid_value_data *data, unsigned long val)
{
    __ubsan_handle_load_invalid_value(data, val);
}

void __ubsan_handle_type_mismatch_v1_abort(struct type_mismatch_data_v1 *data, unsigned long ptr)
{
    __ubsan_handle_type_mismatch_v1(data, ptr);
}
