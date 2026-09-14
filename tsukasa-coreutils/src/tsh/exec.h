/*
 * Project Tsukasa — tsh Execution Engine Header
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

#ifndef _TSH_EXEC_H
#define _TSH_EXEC_H

#include "parser.h"

extern int g_last_exit_status;
extern int g_exit_requested;

char *exec_resolve_path(const char *cmd);
int exec_pipeline(tsh_pipeline_t *pipeline);
int exec_string(const char *line);
int exec_script(const char *path);

#endif /* _TSH_EXEC_H */
