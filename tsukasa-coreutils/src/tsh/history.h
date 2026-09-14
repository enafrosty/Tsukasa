/*
 * Project Tsukasa — tsh Command History Buffer Header
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

#ifndef _TSH_HISTORY_H
#define _TSH_HISTORY_H

#include <stddef.h>

#define TSH_HISTORY_MAX 64
#define TSH_HISTORY_LINE_MAX 1024

void history_init(void);
void history_add(const char *line);
int history_count(void);
const char *history_get(int index);
void history_print(int out_fd);

#endif /* _TSH_HISTORY_H */
