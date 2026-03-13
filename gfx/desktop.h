/*
 * desktop.h - Desktop shell (taskbar, icons, event loop).
 */

#ifndef DESKTOP_H
#define DESKTOP_H

/**
 * Run the desktop shell. This is the main event loop.
 * Called from main_kernel_task. Does not return.
 */
void desktop_run(void);

/**
 * Set the desktop wallpaper from a VFS path.
 * Pass NULL or "" to revert to the gradient background.
 * Takes effect on the next full redraw.
 */
void desktop_set_wallpaper(const char *path);

#endif /* DESKTOP_H */
