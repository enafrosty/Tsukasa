/*
 * Project Tsukasa — Modern window chrome and widget API
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

#ifndef UI_H
#define UI_H

#include "blit.h"
#include <stdint.h>

#define UI_TITLE_H      28
#define UI_BORDER       2
#define UI_SHADOW_R     6
#define UI_CORNER_R     6    /* window corner radius     */

#define UI_TBTN_R       7    /* radius of each circle    */
#define UI_TBTN_SPACING 20   /* center-to-center         */
#define UI_TBTN_Y_OFF   14   /* from top of title bar    */

#define UI_SCROLLBAR_W  10

/* Draw a complete modern window frame (shadow, chrome, border, client bg). */
void ui_draw_window(int x, int y, int w, int h,
                    const char *title, int active, uint32_t accent);

/* Draw a modern flat push-button with optional hover state. */
void ui_draw_button(int x, int y, int w, int h,
                    const char *label, int pressed, int hovered);

/* Draw a vertical scrollbar track + thumb. */
void ui_draw_scrollbar(int x, int y, int h,
                       int total_lines, int visible_lines, int scroll_line);

/* Draw a textbox background (flat, inset style). */
void ui_draw_textbox_bg(int x, int y, int w, int h);

/* Draw a sidebar panel (slightly lighter background than window). */
void ui_draw_sidebar(int x, int y, int w, int h);

/* Draw a sidebar item (highlighted if selected). */
void ui_draw_sidebar_item(int x, int y, int w, int h,
                           const char *label, int selected);

/* Draw a color swatch (filled rounded square with a thin border). */
void ui_draw_color_swatch(int x, int y, int size,
                           uint32_t color, int selected);

/* Draw an icon representation: type 0 = folder, 1 = text file, 2 = image file, 3 = app, 4 = settings */
void ui_draw_icon(int x, int y, int size, int type);

#endif /* UI_H */
