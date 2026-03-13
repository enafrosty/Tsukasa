/*
 * ui.h  -  Modern window chrome and widget API.
 *
 * Replaces win311.h / win311.c.
 * Provides: window frames, buttons, scrollbars, textbox background.
 */

#ifndef UI_H
#define UI_H

#include "blit.h"
#include <stdint.h>

/* Title bar height and border width (match wm.h constants). */
#define UI_TITLE_H      28
#define UI_BORDER       2
#define UI_SHADOW_R     6    /* shadow radius in pixels */
#define UI_CORNER_R     6    /* window corner radius     */

/* Button circle radius (macOS-style title buttons). */
#define UI_TBTN_R       7    /* radius of each circle    */
#define UI_TBTN_SPACING 20   /* center-to-center         */
#define UI_TBTN_Y_OFF   14   /* from top of title bar    */

/* Scrollbar width. */
#define UI_SCROLLBAR_W  10

/**
 * Draw a complete modern window frame (shadow, chrome, border, client bg).
 *
 * @param x,y,w,h  Outer window rectangle.
 * @param title    Title bar text.
 * @param active   Non-zero if this is the focused window.
 * @param accent   Accent color override (0 = use g_accent_color).
 */
void ui_draw_window(int x, int y, int w, int h,
                    const char *title, int active, uint32_t accent);

/**
 * Draw a modern flat push-button with optional hover state.
 *
 * @param label   Text centered in the button.
 * @param pressed Non-zero if button appears sunken.
 * @param hovered Non-zero if mouse is hovering.
 */
void ui_draw_button(int x, int y, int w, int h,
                    const char *label, int pressed, int hovered);

/**
 * Draw a vertical scrollbar track + thumb.
 *
 * @param total_lines  Total logical lines of content.
 * @param visible_lines Lines visible in the viewport.
 * @param scroll_line  First visible line (scroll offset).
 */
void ui_draw_scrollbar(int x, int y, int h,
                       int total_lines, int visible_lines, int scroll_line);

/**
 * Draw a textbox background (flat, inset style).
 */
void ui_draw_textbox_bg(int x, int y, int w, int h);

/**
 * Draw a sidebar panel (slightly lighter background than window).
 */
void ui_draw_sidebar(int x, int y, int w, int h);

/**
 * Draw a sidebar item (highlighted if selected).
 */
void ui_draw_sidebar_item(int x, int y, int w, int h,
                           const char *label, int selected);

/**
 * Draw a color swatch (filled rounded square with a thin border).
 */
void ui_draw_color_swatch(int x, int y, int size,
                           uint32_t color, int selected);

/**
 * Draw an icon representation:
 *   type 0 = folder, 1 = text file, 2 = image file, 3 = app, 4 = settings
 */
void ui_draw_icon(int x, int y, int size, int type);

#endif /* UI_H */
