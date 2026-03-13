/*
 * apps.h  -  Built-in application declarations.
 */

#ifndef APPS_H
#define APPS_H

/** Notepad: open blank. */
void app_notepad_open(void);

/** Notepad: open an existing file by VFS path. */
void app_notepad_open_file(const char *path);

/** File manager. */
void app_filemgr_open(void);

/** System settings. */
void app_settings_open(void);

/** About dialog. */
void app_about_open(void);

/** Calculator. */
void app_calc_open(void);

/** Terminal emulator. */
void app_terminal_open(void);

#endif /* APPS_H */
