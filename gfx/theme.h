/*
 * theme.h  -  Modern dark-glass color palette and compositing macros.
 *
 * All colors are 0xAARRGGBB (same as color_t / blit.h convention).
 * Alpha = 0xFF means fully opaque.
 */

#ifndef THEME_H
#define THEME_H

#include <stdint.h>
#include "blit.h"

/* ---- Palette ----------------------------------------------------------- */

/* Desktop background (used as gradient stops). */
#define THEME_BG_TOP            0xFF0D1B2A   /* deep navy      */
#define THEME_BG_BOT            0xFF1B2838   /* dark slate     */

/* Window chrome. */
#define THEME_TITLEBAR_TOP      0xFF1E2A3A   /* dark blue-grey */
#define THEME_TITLEBAR_BOT      0xFF162030   /* darker         */
#define THEME_TITLEBAR_ACTIVE_TOP 0xFF1E3A5A /* accent blue    */
#define THEME_TITLEBAR_ACTIVE_BOT 0xFF0F2A45

/* Client area. */
#define THEME_WIN_BG            0xFF1A2332   /* dark navy      */
#define THEME_WIN_BORDER        0xFF2A3A4E   /* subtle border  */
#define THEME_WIN_BORDER_ACTIVE 0xFF4FC3F7   /* sky-blue glow  */

/* Text. */
#define THEME_TEXT              0xFFE8EAF0   /* near-white     */
#define THEME_TEXT_DIM          0xFF8090A8   /* muted          */
#define THEME_TEXT_ACCENT       0xFF4FC3F7   /* sky-blue       */

/* Taskbar. */
#define THEME_TASKBAR_BG        0xE0101824   /* 88% opaque     */
#define THEME_TASKBAR_BTN       0xFF1E2A3A
#define THEME_TASKBAR_BTN_HOV   0xFF2A3A4E

/* Start button. */
#define THEME_START_TOP         0xFF1565C0
#define THEME_START_BOT         0xFF0D47A1

/* Shadow (semi-transparent black). */
#define THEME_SHADOW            0x88000000   /* 53% black      */

/* Window button colours (macOS-style circles). */
#define THEME_BTN_CLOSE         0xFFFF5F57   /* red            */
#define THEME_BTN_MIN           0xFFFFBD2E   /* yellow         */
#define THEME_BTN_MAX           0xFF28C940   /* green          */

/* Accent (can be changed at runtime via g_accent_color). */
#define THEME_ACCENT_DEFAULT    0xFF4FC3F7

/* ---- Global mutable accent colour ------------------------------------- */
extern uint32_t g_accent_color;   /* defined in ui.c */

/* ---- Inline helpers --------------------------------------------------- */

/*
 * ARGB – pack explicit components into a color_t.
 */
static inline color_t ARGB(uint8_t a, uint8_t r, uint8_t g, uint8_t b)
{
    return ((uint32_t)a << 24) | ((uint32_t)r << 16) |
           ((uint32_t)g << 8)  | (uint32_t)b;
}

/*
 * alpha_blend_color – software alpha blend of two fully-opaque colors.
 * @alpha: 0 = fully background, 255 = fully foreground.
 */
static inline color_t alpha_blend_color(color_t fg, color_t bg, uint8_t alpha)
{
    uint32_t a  = alpha;
    uint32_t ia = 255u - a;

    uint8_t fr = (fg >> 16) & 0xFF;
    uint8_t fg_ = (fg >> 8)  & 0xFF;
    uint8_t fb_ = (fg)       & 0xFF;

    uint8_t br = (bg >> 16) & 0xFF;
    uint8_t bg_ = (bg >> 8)  & 0xFF;
    uint8_t bb_ = (bg)       & 0xFF;

    uint8_t rr = (uint8_t)((fr * a + br * ia) >> 8);
    uint8_t rg = (uint8_t)((fg_ * a + bg_ * ia) >> 8);
    uint8_t rb = (uint8_t)((fb_ * a + bb_ * ia) >> 8);

    return 0xFF000000u | ((uint32_t)rr << 16) | ((uint32_t)rg << 8) | rb;
}

/* ---- Icon palette (for drawn icons) ----------------------------------- */
#define THEME_ICON_FOLDER       0xFFFFC107   /* amber          */
#define THEME_ICON_TEXT         0xFF90CAF9   /* light blue     */
#define THEME_ICON_IMAGE        0xFFA5D6A7   /* light green    */
#define THEME_ICON_APP          0xFFCE93D8   /* purple         */
#define THEME_ICON_SETTINGS     0xFF80DEEA   /* cyan           */

#endif /* THEME_H */
