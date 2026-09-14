/*
 * Project Tsukasa — About Tsukasa dialog
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

#include "apps.h"
#include "../wm.h"
#include "../font.h"
#include "../blit.h"
#include "../theme.h"
#include <stddef.h>

static void about_draw(wm_window_t *win)
{
    int cx, cy, cw, ch;
    wm_client_rect(win, &cx, &cy, &cw, &ch);

    fb_fill_rect(cx, cy, cw, ch, (color_t)THEME_WIN_BG);

    fb_draw_string(cx + 16, cy + 12,
                   "Tsukasa Project",
                   (color_t)THEME_TEXT, (color_t)THEME_WIN_BG);
    fb_draw_string(cx + 16, cy + 28,
                   "PreAlpha 0.7.3",
                   (color_t)THEME_TEXT, (color_t)THEME_WIN_BG);
    fb_draw_string(cx + 16, cy + 48,
                   "x86 Multiboot OS",
                   (color_t)THEME_TEXT, (color_t)THEME_WIN_BG);
    fb_draw_string(cx + 16, cy + 64,
                   "Built from scratch.",
                   (color_t)THEME_TEXT, (color_t)THEME_WIN_BG);
    fb_draw_string(cx + 16, cy + 88,
                   "WE ALL LOVE REI AYANAMI",
                   (color_t)THEME_TEXT, (color_t)THEME_WIN_BG);
}

void app_about_open(void)
{
    wm_create_window(200, 150, 280, 160, "About Tsukasa",
                     about_draw, NULL, NULL);
}
