/*
 * Project Tsukasa — SYS_GUI userspace-facing window and drawing service
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

#ifndef TSUKASA_GUI_SRV_H
#define TSUKASA_GUI_SRV_H

#include <stdint.h>

#include "../syscall/syscall.h"

void gui_srv_init(void);
void gui_srv_process_cleanup(int pid);

int gui_srv_window_create(int pid,
                          const char *title,
                          int x,
                          int y,
                          int client_w,
                          int client_h);
int gui_srv_window_destroy(int pid, int handle);
int gui_srv_window_set_title(int pid, int handle, const char *title);
int gui_srv_window_set_resizable(int pid, int handle, int resizable);

int gui_srv_draw_rect(int pid, int handle,
                      int x, int y, int w, int h,
                      uint32_t color);
int gui_srv_draw_rounded_rect(int pid, int handle,
                              int x, int y, int w, int h, int radius,
                              uint32_t color);
int gui_srv_draw_text(int pid, int handle,
                      int x, int y, const char *text,
                      uint32_t color);
int gui_srv_draw_image(int pid, int handle,
                       int x, int y, int w, int h,
                       const uint32_t *pixels);
int gui_srv_mark_dirty(int pid, int handle,
                       int x, int y, int w, int h);
int gui_srv_get_event(int pid, int handle, struct tsukasa_gui_event *out);

int gui_srv_get_string_width(const char *str);
int gui_srv_get_font_height(void);
int gui_srv_get_screen_size(uint64_t *out_w, uint64_t *out_h);

#endif /* TSUKASA_GUI_SRV_H */
