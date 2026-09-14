/*
 * Project Tsukasa — Minimal executable resolver for Phase 2 process lifecycle
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

#ifndef TSUKASA_EXEC_H
#define TSUKASA_EXEC_H

typedef void (*exec_entry_t)(void);

int exec_register_builtin(const char *path, exec_entry_t entry);
int exec_resolve_builtin(const char *path, exec_entry_t *entry_out);

#endif /* TSUKASA_EXEC_H */
