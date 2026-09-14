/*
 * Project Tsukasa — Kernel console interface
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

#ifndef KCONSOLE_H
#define KCONSOLE_H

#include <stdint.h>
#include <stdbool.h>

void kconsole_init(void);
void kconsole_set_color(uint32_t color);
void kconsole_putc(char c);
void kconsole_write(const char *s);
void kconsole_set_active(bool active);

void log_ok(const char *msg);
void log_fail(const char *msg);

#endif // KCONSOLE_H
