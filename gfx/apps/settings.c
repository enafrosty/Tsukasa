/*
 * Project Tsukasa — System Settings application
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
#include "../ui.h"
#include "../theme.h"
#include "../font.h"
#include "../blit.h"
#include "../../input/event.h"
#include "../../fs/vfs.h"
#include "../../drv/fb.h"
#include "../../mm/heap.h"
#include <stddef.h>
#include <stdint.h>

/* Defined in desktop.c. */
extern void desktop_set_wallpaper(const char *path);

#define ST_W          420
#define ST_H          300
#define ST_SIDEBAR_W   96
#define ST_TABS         2

static const char *tab_labels[ST_TABS] = { "Personalize", "Display" };

/* Accent color palette. */
#define NUM_ACCENTS  6
static const uint32_t accent_palette[NUM_ACCENTS] = {
    0xFF4FC3F7,
    0xFF81D4FA,
    0xFF80CBC4,
    0xFF80DEEA,
    0xFFCE93D8,
    0xFFEF9A9A,
};

#define SWATCH_SIZE    28
#define SWATCH_GAP      8

#define WP_MAX  16

typedef struct {
    int    active_tab;
    int    selected_accent;
    char   wallpapers[WP_MAX][VFS_NAME_MAX];
    int    wp_count;
    int    selected_wp;
    char   status[64];
} settings_data_t;

static int st_strlen(const char *s)
{ int n=0; while(s&&s[n])n++; return n; }
static int st_streq_suffix(const char *name, const char *suf)
{
    int nl=st_strlen(name), sl=st_strlen(suf);
    if(sl>nl) return 0;
    const char *t=name+nl-sl;
    for(int i=0;suf[i];i++){
        char a=t[i],b=suf[i];
        if(a>='a'&&a<='z')a-=32;
        if(b>='a'&&b<='z')b-=32;
        if(a!=b)return 0;
    } return 1;
}
static void st_strcpy(char *dst, const char *src, int max)
{ int i=0;while(src[i]&&i<max-1){dst[i]=src[i];i++;}dst[i]='\0'; }

static void st_refresh_wallpapers(settings_data_t *st)
{
    char names[WP_MAX][VFS_NAME_MAX];
    int n = vfs_list("/", names, WP_MAX);
    st->wp_count = 0;
    for (int i = 0; i < n && st->wp_count < WP_MAX; i++) {
        if (st_streq_suffix(names[i], ".bmp") ||
            st_streq_suffix(names[i], ".BMP")) {
            st_strcpy(st->wallpapers[st->wp_count], names[i], VFS_NAME_MAX);
            st->wp_count++;
        }
    }
}

static void draw_string(int x, int y, const char *s, color_t fg, color_t bg)
{
    while (*s) { fb_draw_char(x, y, *s, fg, bg); x += 8; s++; }
}

static void st_draw_personalize(settings_data_t *st,
                                 int cx, int cy, int cw, int ch)
{
    int px = cx + ST_SIDEBAR_W + 12;
    int py = cy + 8;

    draw_string(px, py, "Accent Color",
                (color_t)THEME_TEXT_ACCENT, (color_t)THEME_WIN_BG);
    py += 14;

    fb_draw_hline(px, py, cw - ST_SIDEBAR_W - 20,
                  (color_t)THEME_WIN_BORDER);
    py += 6;

    for (int i = 0; i < NUM_ACCENTS; i++) {
        int sx = px + i * (SWATCH_SIZE + SWATCH_GAP);
        ui_draw_color_swatch(sx, py, SWATCH_SIZE,
                             accent_palette[i], (i == st->selected_accent));
    }
    py += SWATCH_SIZE + 14;

    draw_string(px, py, "Wallpaper",
                (color_t)THEME_TEXT_ACCENT, (color_t)THEME_WIN_BG);
    py += 14;
    fb_draw_hline(px, py, cw - ST_SIDEBAR_W - 20,
                  (color_t)THEME_WIN_BORDER);
    py += 6;

    if (st->wp_count == 0) {
        draw_string(px, py, "No .bmp files found in /",
                    (color_t)THEME_TEXT_DIM, (color_t)THEME_WIN_BG);
        py += 12;
        draw_string(px, py, "Place a .bmp on the FAT12 ramdisk.",
                    (color_t)THEME_TEXT_DIM, (color_t)THEME_WIN_BG);
    } else {
        for (int i = 0; i < st->wp_count; i++) {
            int wy = py + i * 18;
            if (wy + 12 > cy + ch - 20) break;
            color_t bg = (i == st->selected_wp) ?
                         rgba(79, 195, 247, 20) : (color_t)THEME_WIN_BG;
            fb_fill_rect(px, wy, cw - ST_SIDEBAR_W - 20, 16, bg);
            color_t fg = (i == st->selected_wp) ?
                         (color_t)THEME_TEXT_ACCENT : (color_t)THEME_TEXT;
            draw_string(px + 4, wy + 4, st->wallpapers[i], fg, bg);
        }
    }

    {
        int by = cy + ch - 32;
        int bx = px;
        ui_draw_button(bx, by, 108, 22, "Set Wallpaper", 0, 0);
    }

    if (st->status[0])
        draw_string(px, cy + ch - 10, st->status,
                    (color_t)THEME_TEXT_DIM, (color_t)THEME_WIN_BG);
}

static void st_draw_display(settings_data_t *st,
                             int cx, int cy, int cw, int ch)
{
    (void)st;
    (void)ch;
    int px = cx + ST_SIDEBAR_W + 12;
    int py = cy + 8;

    draw_string(px, py, "Display Information",
                (color_t)THEME_TEXT_ACCENT, (color_t)THEME_WIN_BG);
    py += 16;
    fb_draw_hline(px, py, cw - ST_SIDEBAR_W - 20, (color_t)THEME_WIN_BORDER);
    py += 10;

    char buf[64];
    {
        uint32_t w = fb_info.width, h = fb_info.height;
        int bi = 0;
        const char *lbl = "Resolution:  ";
        while (*lbl) buf[bi++] = *lbl++;
        char tmp[12]; int ti=0;
        uint32_t ww = w;
        if (!ww) tmp[ti++]='0';
        while (ww) { tmp[ti++]='0'+(char)(ww%10); ww/=10; }
        for (int k=ti-1;k>=0;k--) buf[bi++]=tmp[k];
        buf[bi++]=' '; buf[bi++]='x'; buf[bi++]=' ';
        ti=0; uint32_t hh = h;
        if (!hh) tmp[ti++]='0';
        while (hh) { tmp[ti++]='0'+(char)(hh%10); hh/=10; }
        for (int k=ti-1;k>=0;k--) buf[bi++]=tmp[k];
        const char *bpp = " @ 32 bpp";
        while (*bpp) buf[bi++]=*bpp++;
        buf[bi]='\0';
    }
    draw_string(px, py, buf, (color_t)THEME_TEXT, (color_t)THEME_WIN_BG);
    py += 14;
    draw_string(px, py, "Format:  0xAARRGGBB linear framebuffer",
                (color_t)THEME_TEXT_DIM, (color_t)THEME_WIN_BG);
    py += 14;
    draw_string(px, py, "Renderer: bare-metal CPU blit",
                (color_t)THEME_TEXT_DIM, (color_t)THEME_WIN_BG);
}

static void settings_draw(wm_window_t *win)
{
    settings_data_t *st = (settings_data_t *)win->app_data;
    if (!st) return;

    int cx, cy, cw, ch;
    wm_client_rect(win, &cx, &cy, &cw, &ch);

    fb_fill_rect(cx, cy, cw, ch, (color_t)THEME_WIN_BG);

    ui_draw_sidebar(cx, cy, ST_SIDEBAR_W, ch);

    draw_string(cx + 8, cy + 8, "Settings",
                (color_t)THEME_TEXT_ACCENT,
                rgba(14, 22, 34, 255));

    fb_draw_hline(cx, cy + 24, ST_SIDEBAR_W, (color_t)THEME_WIN_BORDER);

    for (int i = 0; i < ST_TABS; i++) {
        ui_draw_sidebar_item(cx, cy + 32 + i * 28,
                             ST_SIDEBAR_W, 26,
                             tab_labels[i], (i == st->active_tab));
    }

    fb_fill_rect(cx + ST_SIDEBAR_W, cy, cw - ST_SIDEBAR_W, ch,
                 (color_t)THEME_WIN_BG);

    if (st->active_tab == 0)
        st_draw_personalize(st, cx, cy, cw, ch);
    else
        st_draw_display(st, cx, cy, cw, ch);
}

static void settings_event(wm_window_t *win, const void *event)
{
    settings_data_t *st = (settings_data_t *)win->app_data;
    if (!st || !event) return;

    const struct gui_event *ev = (const struct gui_event *)event;
    if (ev->type != EVENT_MOUSE) return;

    int mx = ev->x, my = ev->y;
    int left_down = (ev->keycode & 1) && (ev->subtype == MOUSE_BTN_DOWN);

    if (!left_down) return;

    int cx, cy, cw, ch;
    wm_client_rect(win, &cx, &cy, &cw, &ch);

    if (mx >= cx && mx < cx + ST_SIDEBAR_W) {
        for (int i = 0; i < ST_TABS; i++) {
            int ty = cy + 32 + i * 28;
            if (my >= ty && my < ty + 26) {
                st->active_tab = i;
                return;
            }
        }
        return;
    }

    if (st->active_tab != 0) return;

    int px = cx + ST_SIDEBAR_W + 12;

    {
        int sy = cy + 28;
        for (int i = 0; i < NUM_ACCENTS; i++) {
            int sx = px + i * (SWATCH_SIZE + SWATCH_GAP);
            if (mx >= sx && mx < sx + SWATCH_SIZE &&
                my >= sy && my < sy + SWATCH_SIZE) {
                st->selected_accent = i;
                g_accent_color = accent_palette[i];
                return;
            }
        }
    }

    {
        int wy0 = cy + 28 + SWATCH_SIZE + 34;
        for (int i = 0; i < st->wp_count; i++) {
            int wy = wy0 + i * 18;
            if (my >= wy && my < wy + 16 &&
                mx >= px && mx < cx + cw - 8) {
                st->selected_wp = i;
                return;
            }
        }
    }

    {
        int by = cy + ch - 32;
        if (my >= by && my < by + 22 && mx >= px && mx < px + 108) {
            if (st->selected_wp >= 0 && st->selected_wp < st->wp_count) {
                char path[VFS_NAME_MAX + 2];
                path[0] = '/';
                const char *n = st->wallpapers[st->selected_wp];
                int i = 0;
                while (n[i] && i < VFS_NAME_MAX) { path[i+1]=n[i]; i++; }
                path[i+1] = '\0';
                desktop_set_wallpaper(path);
                const char *msg = "Wallpaper applied!";
                int j=0; while(msg[j]){st->status[j]=msg[j];j++;} st->status[j]='\0';
            }
        }
    }
}

void app_settings_open(void)
{
    settings_data_t *st = (settings_data_t *)kmalloc(sizeof(settings_data_t));
    if (!st) return;

    st->active_tab    = 0;
    st->selected_accent = 0;
    st->wp_count      = 0;
    st->selected_wp   = -1;
    st->status[0]     = '\0';

    st_refresh_wallpapers(st);

    wm_create_window(60, 60, ST_W, ST_H, "Settings",
                     settings_draw, settings_event, st);
}
