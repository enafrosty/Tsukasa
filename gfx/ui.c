/*
 * ui.c  -  Modern window chrome and widget implementation.
 *
 * All drawing uses blit.h primitives and theme.h colours.
 * No libc, no standard headers beyond <stdint.h> / <stddef.h>.
 */

#include "ui.h"
#include "theme.h"
#include "blit.h"
#include "font.h"
#include "font_8x8.h"
#include <stdint.h>
#include <stddef.h>

/* Global accent — can be changed at runtime by the Settings app. */
uint32_t g_accent_color = THEME_ACCENT_DEFAULT;

/* ---- Internal helpers ------------------------------------------------- */

static int kstrlen_ui(const char *s)
{
    int n = 0;
    while (s && s[n]) n++;
    return n;
}

/* Inline abs. */
static inline int absi(int v) { return v < 0 ? -v : v; }

/* ---- Window chrome ---------------------------------------------------- */

/*
 * ui_draw_window
 *
 * Layout (y axis, top-down):
 *   shadow area (UI_SHADOW_R px above and left — drawn BELOW the window)
 *   outer border (UI_BORDER px, UI_WIN_BORDER color)
 *   title bar   (UI_TITLE_H px, gradient)
 *   client area (rest, THEME_WIN_BG)
 *   outer border bottom/right
 */
void ui_draw_window(int x, int y, int w, int h,
                    const char *title, int active, uint32_t accent)
{
    if (w < 2 * UI_BORDER + 20 || h < 2 * UI_BORDER + UI_TITLE_H)
        return;

    if (accent == 0)
        accent = g_accent_color;

    /* --- 1. Drop shadow ------------------------------------------------- */
    fb_draw_shadow_rect(x, y, w, h, UI_SHADOW_R);

    /* --- 2. Outer rounded border ---------------------------------------- */
    color_t border_col = active ? (color_t)accent : (color_t)THEME_WIN_BORDER;
    fb_fill_rounded_rect(x, y, w, h, UI_CORNER_R, border_col);

    /* --- 3. Inner title bar fill (gradient) ----------------------------- */
    {
        int tx = x + UI_BORDER;
        int ty = y + UI_BORDER;
        int tw = w - 2 * UI_BORDER;
        int th = UI_TITLE_H;

        color_t t_top = active ? (color_t)THEME_TITLEBAR_ACTIVE_TOP
                               : (color_t)THEME_TITLEBAR_TOP;
        color_t t_bot = active ? (color_t)THEME_TITLEBAR_ACTIVE_BOT
                               : (color_t)THEME_TITLEBAR_BOT;

        /* Rounded top cap of the title bar. The corners of the gradient
           should match the outer window's corner radius. */
        fb_fill_gradient_v(tx, ty, tw, th, t_top, t_bot);
    }

    /* --- 4. Client area fill -------------------------------------------- */
    {
        int cx = x + UI_BORDER;
        int cy = y + UI_BORDER + UI_TITLE_H;
        int cw = w - 2 * UI_BORDER;
        int ch = h - 2 * UI_BORDER - UI_TITLE_H;
        if (cw > 0 && ch > 0)
            fb_fill_rect(cx, cy, cw, ch, THEME_WIN_BG);
    }

    /* --- 5. Separator line under title bar ------------------------------ */
    fb_draw_hline(x + UI_BORDER, y + UI_BORDER + UI_TITLE_H,
                  w - 2 * UI_BORDER, (color_t)THEME_WIN_BORDER);

    /* --- 6. Traffic-light close/min/max buttons (left side) ------------- */
    {
        int by = y + UI_BORDER + UI_TBTN_Y_OFF;
        int bx = x + UI_BORDER + 14;

        fb_fill_circle(bx, by, UI_TBTN_R, THEME_BTN_CLOSE);
        bx += UI_TBTN_SPACING;
        fb_fill_circle(bx, by, UI_TBTN_R, THEME_BTN_MIN);
        bx += UI_TBTN_SPACING;
        fb_fill_circle(bx, by, UI_TBTN_R, THEME_BTN_MAX);
    }

    /* --- 7. Title text (centered, right of traffic lights) -------------- */
    if (title && kstrlen_ui(title) > 0) {
        int title_x_start = x + UI_BORDER + 14 + 2 * UI_TBTN_SPACING + UI_TBTN_R + 8;
        int title_y  = y + UI_BORDER + (UI_TITLE_H - FONT_HEIGHT) / 2;
        int max_w    = w - (title_x_start - x) - UI_BORDER - 8;
        /* Draw each character but stop when we'd overflow. */
        int cx2 = title_x_start;
        for (int i = 0; title[i] && cx2 + 8 <= title_x_start + max_w; i++) {
            fb_draw_char(cx2, title_y, title[i],
                         (color_t)THEME_TEXT,
                         active ? (color_t)THEME_TITLEBAR_ACTIVE_TOP
                                : (color_t)THEME_TITLEBAR_TOP);
            cx2 += 8;
        }
    }
}

/* ---- Button ----------------------------------------------------------- */

void ui_draw_button(int x, int y, int w, int h,
                    const char *label, int pressed, int hovered)
{
    if (w < 4 || h < 4) return;

    color_t bg;
    if (pressed)      bg = (color_t)THEME_WIN_BORDER;
    else if (hovered) bg = (color_t)THEME_TASKBAR_BTN_HOV;
    else              bg = (color_t)THEME_TASKBAR_BTN;

    fb_fill_rounded_rect(x, y, w, h, 4, bg);

    /* 1px border using the accent color. */
    fb_draw_rounded_rect(x, y, w, h, 4, (color_t)g_accent_color);

    /* Center text. */
    if (label) {
        int len   = kstrlen_ui(label);
        int tx    = x + (w - len * 8) / 2;
        int ty    = y + (h - FONT_HEIGHT) / 2;
        for (int i = 0; label[i]; i++) {
            fb_draw_char(tx, ty, label[i], (color_t)THEME_TEXT, bg);
            tx += 8;
        }
    }
}

/* ---- Scrollbar -------------------------------------------------------- */

void ui_draw_scrollbar(int x, int y, int h,
                       int total_lines, int visible_lines, int scroll_line)
{
    if (h <= 0 || total_lines <= 0) return;

    /* Track. */
    fb_fill_rect(x, y, UI_SCROLLBAR_W, h, (color_t)THEME_TITLEBAR_TOP);

    if (total_lines <= visible_lines) return;  /* no scrolling needed */

    /* Thumb proportional size. */
    int thumb_h = (h * visible_lines) / total_lines;
    if (thumb_h < 12) thumb_h = 12;

    int thumb_y = y + (h - thumb_h) * scroll_line / (total_lines - visible_lines);

    fb_fill_rounded_rect(x + 1, thumb_y, UI_SCROLLBAR_W - 2, thumb_h,
                         3, (color_t)g_accent_color);
}

/* ---- Textbox background ----------------------------------------------- */

void ui_draw_textbox_bg(int x, int y, int w, int h)
{
    /* Slightly darker than normal window bg, with a 1px inset border. */
    color_t bg = rgba(13, 19, 28, 0xFF);   /* #0D131C */
    fb_fill_rect(x, y, w, h, bg);
    fb_draw_rounded_rect(x, y, w, h, 3, (color_t)THEME_WIN_BORDER);
}

/* ---- Sidebar ---------------------------------------------------------- */

void ui_draw_sidebar(int x, int y, int w, int h)
{
    color_t bg = rgba(14, 22, 34, 0xFF);   /* slightly darker navy */
    fb_fill_rect(x, y, w, h, bg);
    /* Right edge separator. */
    fb_draw_vline(x + w - 1, y, h, (color_t)THEME_WIN_BORDER);
}

void ui_draw_sidebar_item(int x, int y, int w, int h,
                           const char *label, int selected)
{
    if (selected) {
        fb_fill_rect_alpha(x, y, w, h,
                           rgba(79, 195, 247, 30));   /* subtle accent tint */
        fb_draw_vline(x, y, h, (color_t)g_accent_color);  /* accent left bar */
    }

    color_t fg = selected ? (color_t)THEME_TEXT_ACCENT : (color_t)THEME_TEXT_DIM;
    if (label) {
        int ty = y + (h - FONT_HEIGHT) / 2;
        int tx = x + 16;
        for (int i = 0; label[i] && tx + 8 <= x + w - 4; i++) {
            fb_draw_char(tx, ty, label[i], fg,
                         selected ? rgba(14, 22, 34, 0xFF)
                                  : rgba(14, 22, 34, 0xFF));
            tx += 8;
        }
    }
}

/* ---- Color swatch ----------------------------------------------------- */

void ui_draw_color_swatch(int x, int y, int size, uint32_t color, int selected)
{
    fb_fill_rounded_rect(x, y, size, size, 4, (color_t)color);
    if (selected)
        fb_draw_rounded_rect(x - 2, y - 2, size + 4, size + 4, 5,
                             (color_t)THEME_TEXT);
    else
        fb_draw_rounded_rect(x, y, size, size, 4, (color_t)THEME_WIN_BORDER);
}

/* ---- Icons ------------------------------------------------------------ */

/*
 * Simple pixel-art style icons drawn procedurally.
 * type: 0=folder, 1=text, 2=image, 3=app, 4=settings
 */
void ui_draw_icon(int x, int y, int size, int type)
{
    /* Icon background (subtle). */
    color_t bg;
    switch (type) {
    case 0:  bg = rgba( 40,  28,   0, 200); break;   /* folder: dark amber */
    case 1:  bg = rgba(  0,  28,  60, 200); break;   /* text:   dark blue  */
    case 2:  bg = rgba(  0,  40,  20, 200); break;   /* image:  dark green */
    case 3:  bg = rgba( 40,   0,  60, 200); break;   /* terminal: dark purple*/
    case 4:  bg = rgba(  0,  35,  42, 200); break;   /* settings: dark cyan*/
    case 5:  bg = rgba( 60,  30,  10, 200); break;   /* calc: orange/brown */
    default: bg = rgba( 30,  30,  40, 200); break;   /* about: dark grey */
    }

    fb_fill_rounded_rect(x, y, size, size, size / 6, bg);

    /* Icon glyph (large, centered). */
    int mid_x = x + size / 2;
    int mid_y = y + size / 2;
    int s3    = size / 3;

    switch (type) {
    case 0: /* Folder — two rectangles (tab + body). */
        {
            color_t fc = THEME_ICON_FOLDER;
            fb_fill_rounded_rect(x + 4,         y + size / 3,
                                 size - 8,       size / 2, 2, fc);
            fb_fill_rounded_rect(x + 4,         y + 4,
                                 size / 2 - 2,  size / 4, 2, fc);
        }
        break;
    case 1: /* Text file — lines. */
        {
            color_t fc = THEME_ICON_TEXT;
            int ly = mid_y - s3;
            int lw = size - 12;
            for (int l = 0; l < 4; l++, ly += (s3 * 2) / 4) {
                int w2 = (l == 3) ? lw * 2 / 3 : lw;
                fb_fill_rect(x + 6, ly, w2, 2, fc);
            }
        }
        break;
    case 2: /* Image — mountain silhouette + circle sun. */
        {
            color_t fc = THEME_ICON_IMAGE;
            /* Sun. */
            fb_fill_circle(x + size * 3 / 4, y + size / 3, size / 7, fc);
            /* Mountain triangle (drawn as filled rects). */
            for (int ly = 0; ly < size / 2; ly++) {
                int xspan = ly * (size / 2) / (size / 2);
                fb_fill_rect(mid_x - xspan, y + size / 2 + ly,
                             2 * xspan, 1, fc);
            }
        }
        break;
    case 3: /* Terminal — stylised window icon. */
        {
            color_t fc = THEME_ICON_APP;
            fb_fill_rounded_rect(x + 4, y + 4, size - 8, size - 8, 3, fc);
            fb_fill_rect(x + 4, y + 4, size - 8, (size - 8) / 3,
                         rgba(80, 20, 120, 255));
        }
        break;
    case 4: /* Settings — gear-like circles. */
        {
            color_t fc = THEME_ICON_SETTINGS;
            fb_fill_circle(mid_x, mid_y, size / 4, fc);
            fb_fill_circle(mid_x, mid_y, size / 8,
                           rgba(14, 22, 34, 255));
        }
        break;
    case 5: /* Calc — 4x4 grid. */
        {
            color_t fc = rgba(255, 180, 100, 255); /* Orange-ish */
            int sq = size / 4;
            int px = x + (size - (sq*2+2))/2;
            int py = y + (size - (sq*2+2))/2;
            fb_fill_rounded_rect(px, py, sq, sq, 2, fc);
            fb_fill_rounded_rect(px+sq+2, py, sq, sq, 2, fc);
            fb_fill_rounded_rect(px, py+sq+2, sq, sq, 2, fc);
            fb_fill_rounded_rect(px+sq+2, py+sq+2, sq, sq, 2, fc);
        }
        break;
    default: /* About — 'i' symbol. */
        {
            color_t fc = rgba(200, 200, 220, 255);
            /* Dot */
            fb_fill_circle(mid_x, mid_y - size/5, 2, fc);
            /* Stem */
            fb_fill_rect(mid_x - 1, mid_y, 3, size/4, fc);
        }
        break;
    }
}
