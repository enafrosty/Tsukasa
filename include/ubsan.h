/*
 * Project Tsukasa — Undefined Behavior Sanitizer Runtime Definitions
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

#ifndef TSUKASA_UBSAN_H
#define TSUKASA_UBSAN_H

#include <stdint.h>

struct source_location {
    const char *file;
    uint32_t line;
    uint32_t column;
};

struct type_descriptor {
    uint16_t type_kind;
    uint16_t type_info;
    char type_name[1];
};

struct overflow_data {
    struct source_location loc;
    struct type_descriptor *type;
};

struct out_of_bounds_data {
    struct source_location loc;
    struct type_descriptor *array_type;
    struct type_descriptor *index_type;
};

struct type_mismatch_data_v1 {
    struct source_location loc;
    struct type_descriptor *type;
    uint8_t log_alignment;
    uint8_t type_check_kind;
};

struct shift_out_of_bounds_data {
    struct source_location loc;
    struct type_descriptor *lhs_type;
    struct type_descriptor *rhs_type;
};

struct invalid_value_data {
    struct source_location loc;
    struct type_descriptor *type;
};

struct unreachable_data {
    struct source_location loc;
};

void __ubsan_handle_add_overflow(struct overflow_data *data, unsigned long lhs, unsigned long rhs);
void __ubsan_handle_sub_overflow(struct overflow_data *data, unsigned long lhs, unsigned long rhs);
void __ubsan_handle_mul_overflow(struct overflow_data *data, unsigned long lhs, unsigned long rhs);
void __ubsan_handle_negate_overflow(struct overflow_data *data, unsigned long old_val);
void __ubsan_handle_divrem_overflow(struct overflow_data *data, unsigned long lhs, unsigned long rhs);
void __ubsan_handle_shift_out_of_bounds(struct shift_out_of_bounds_data *data, unsigned long lhs, unsigned long rhs);
void __ubsan_handle_out_of_bounds(struct out_of_bounds_data *data, unsigned long index);
void __ubsan_handle_builtin_unreachable(struct unreachable_data *data);
void __ubsan_handle_load_invalid_value(struct invalid_value_data *data, unsigned long val);
void __ubsan_handle_type_mismatch_v1(struct type_mismatch_data_v1 *data, unsigned long ptr);

#endif /* TSUKASA_UBSAN_H */
