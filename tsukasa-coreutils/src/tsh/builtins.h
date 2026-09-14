/*
 * Project Tsukasa — tsh Built-in Commands Header
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

#ifndef _TSH_BUILTINS_H
#define _TSH_BUILTINS_H

#include "parser.h"

void env_init(char **initial_envp);
const char *env_get(const char *name);
int env_set(const char *name, const char *value);
char **env_get_all(void);
void env_cleanup(void);

int builtin_lookup(const char *name);
int builtin_execute(tsh_cmd_t *cmd, int in_fd, int out_fd, int err_fd, int *exit_shell);

#endif /* _TSH_BUILTINS_H */
