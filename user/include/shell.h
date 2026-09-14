/*
 * Project Tsukasa — Shell execution interface
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

#ifndef TSUKASA_SHELL_H
#define TSUKASA_SHELL_H

int shell_exec_line(const char *line, int out_fd, int err_fd);
int shell_run_rc_file(const char *path, int out_fd, int err_fd);

#endif /* TSUKASA_SHELL_H */
