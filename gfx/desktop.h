/*
 * Project Tsukasa — Desktop shell (taskbar, icons, event loop)
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

#ifndef DESKTOP_H
#define DESKTOP_H

#include <stdint.h>

/* Run the desktop shell. This is the main event loop. Called from main_kernel_task. Does not return. */
void desktop_run(void);

/* Set the desktop wallpaper from a VFS path. */
int desktop_set_wallpaper(const char *path);

/* Background modes shared with SYSTEM_CMD_THEME_* state. */
#define DESKTOP_BG_MODE_GRADIENT  0u
#define DESKTOP_BG_MODE_SOLID     1u
#define DESKTOP_BG_MODE_WALLPAPER 2u

/* Wallpaper layout modes. */
#define DESKTOP_WALLPAPER_SCALE_FILL 0u
#define DESKTOP_WALLPAPER_CENTER     1u

/* Apply a full desktop theme state atomically. Wallpaper decode failure falls back to gradient mode and... */
int desktop_apply_theme(uint32_t accent_color,
                        uint32_t background_mode,
                        uint32_t solid_color,
                        uint32_t wallpaper_style,
                        const char *wallpaper_path);

#endif /* DESKTOP_H */
