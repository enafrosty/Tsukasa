/*
 * Project Tsukasa — Built-in application declarations
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

#ifndef APPS_H
#define APPS_H

/* Notepad: open blank. */
void app_notepad_open(void);

/* Notepad: open an existing file by VFS path. */
void app_notepad_open_file(const char *path);

/* File manager. */
void app_filemgr_open(void);

/* System settings. */
void app_settings_open(void);

/* About dialog. */
void app_about_open(void);

/* Calculator. */
void app_calc_open(void);

/* Terminal emulator. */
void app_terminal_open(void);

#endif /* APPS_H */
