/*
 * desktop.h - Desktop shell (taskbar, icons, event loop).
 */

#ifndef DESKTOP_H
#define DESKTOP_H

#include <stdint.h>

/**
 * Run the desktop shell. This is the main event loop.
 * Called from main_kernel_task. Does not return.
 */
void desktop_run(void);

/**
 * Set the desktop wallpaper from a VFS path.
 * Pass NULL or "" to revert to the gradient background.
 * Takes effect on the next full redraw.
 * Returns 0 on success, -1 on decode/load failure.
 */
int desktop_set_wallpaper(const char *path);

/* Background modes shared with SYSTEM_CMD_THEME_* state. */
#define DESKTOP_BG_MODE_GRADIENT  0u
#define DESKTOP_BG_MODE_SOLID     1u
#define DESKTOP_BG_MODE_WALLPAPER 2u

/* Wallpaper layout modes. */
#define DESKTOP_WALLPAPER_SCALE_FILL 0u
#define DESKTOP_WALLPAPER_CENTER     1u

/*
 * Apply a full desktop theme state atomically.
 * Wallpaper decode failure falls back to gradient mode and returns -1.
 */
int desktop_apply_theme(uint32_t accent_color,
                        uint32_t background_mode,
                        uint32_t solid_color,
                        uint32_t wallpaper_style,
                        const char *wallpaper_path);

#endif /* DESKTOP_H */
