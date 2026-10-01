/*
 * Project Tsukasa — Display Server Design Tokens & Theme Specification
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

#ifndef _VANILLA_THEME_H
#define _VANILLA_THEME_H

#include <stdint.h>

/*
 * Colour roles: semantic palette slots.
 * Every paint site uses a role name; no ARGB literal appears outside theme files.
 */
typedef struct {
    /* Backgrounds */
    uint32_t bg_base;                /* desktop/window background */
    uint32_t bg_elevated;            /* card, popover, raised surface */
    uint32_t bg_overlay;             /* modal scrim, menu underlay */

    /* Foregrounds */
    uint32_t fg_primary;             /* primary text, icon */
    uint32_t fg_muted;               /* secondary text, placeholder */
    uint32_t fg_dim;                 /* disabled text */

    /* Borders and separators */
    uint32_t border;                 /* unfocused window border, separator */
    uint32_t border_focus;           /* focused window border / accent ring */

    /* Interactive states */
    uint32_t accent;                 /* primary accent (buttons, selection ring) */
    uint32_t accent_hover;           /* hovered accent */
    uint32_t accent_pressed;         /* pressed/active accent */
    uint32_t selection;              /* text selection highlight */

    /* Semantic feedback */
    uint32_t success;                /* positive feedback */
    uint32_t warning;                /* caution feedback */
    uint32_t danger;                 /* destructive action (close button) */

    /* Window chrome */
    uint32_t titlebar_active;        /* focused titlebar background */
    uint32_t titlebar_inactive;      /* unfocused titlebar background */
    uint32_t titlebar_text_active;   /* focused title text */
    uint32_t titlebar_text_inactive; /* unfocused title text */
    uint32_t titlebar_btn_bg;        /* non-close button background */
    uint32_t titlebar_btn_bg_hover;  /* non-close button hover background */
    uint32_t titlebar_btn_icon;      /* button icon/glyph colour */

    /* Shell */
    uint32_t taskbar_bg;             /* taskbar panel background */
    uint32_t taskbar_item_active;    /* focused/running app indicator */
    uint32_t taskbar_item_open;      /* open but unfocused app */
    uint32_t taskbar_text;           /* taskbar text */

    /* Metric tokens */
    int32_t  titlebar_height;        /* px: replaces TITLEBAR_HEIGHT macro */
    int32_t  border_width;           /* px: replaces WINDOW_BORDER_WIDTH */
    int32_t  btn_size;               /* px: replaces TITLEBAR_BTN_SIZE */
    float    title_font_size;        /* pt/px: replaces TITLEBAR_FONT_SIZE */
    int32_t  radius_sm;              /* px corner radius: small (4) */
    int32_t  radius_md;              /* px corner radius: medium (8) */
    int32_t  radius_lg;              /* px corner radius: large (12) */
    int32_t  shadow_radius;          /* px shadow spread */
    uint8_t  shadow_alpha;           /* 0-255 shadow opacity */
    int32_t  space_1;                /* baseline spacing unit (4 px at 1x) */
    int32_t  space_2;                /* 2x space_1 */
    int32_t  space_4;                /* 4x space_1 */
    int32_t  space_8;                /* 8x space_1 */
    float    scale_factor;           /* DPI scale (1.0 = 96 dpi; 2.0 = 192 dpi) */

    /* Motion tokens */
    int32_t  dur_fast;               /* ms: micro transitions (50) */
    int32_t  dur_base;               /* ms: standard transitions (150) */
    int32_t  dur_slow;               /* ms: deliberate animations (300) */
    /* Easing: cubic-bezier control points [p1x,p1y,p2x,p2y] in 0.0-1.0 */
    float    ease_standard[4];       /* ease-in-out (0.4, 0.0, 0.2, 1.0) */
    float    ease_decelerate[4];     /* ease-out   (0.0, 0.0, 0.2, 1.0) */
    float    ease_accelerate[4];     /* ease-in    (0.4, 0.0, 1.0, 1.0) */
    int32_t  reduce_motion;          /* 1 = honour accessibility preference */
} vanilla_theme_t;

/* Pointer to the currently active theme. Set by theme_load(); read everywhere else. */
extern const vanilla_theme_t *g_theme;

/*
 * Scale a pixel constant by the active theme's scale_factor.
 * Use THEME_PX for integer pixel values.
 * Use THEME_F for float values.
 */
#define THEME_PX(n)  ((int32_t)((float)(n) * g_theme->scale_factor))
#define THEME_F(f)   ((f) * g_theme->scale_factor)

/*
 * Load theme from an INI file. Returns 0 on success, -1 on parse error.
 * Falls back to the compiled-in nord-dark defaults on any failure.
 */
int  theme_load(const char *ini_path);

/* Reload the active INI file in place (call after a SIGHUP or GUI action). */
int  theme_reload(void);

/* Return a pointer to the compiled-in nord-dark defaults. */
const vanilla_theme_t *theme_defaults_nord_dark(void);

#endif /* _VANILLA_THEME_H */
