/*
 * about.c - About Tsukasa dialog.
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
