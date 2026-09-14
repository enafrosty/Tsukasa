/*
 * Project Tsukasa — tk_painter: client-side pixel painter for shm surfaces
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

#ifndef TSUKASA_TK_PAINTER_H
#define TSUKASA_TK_PAINTER_H

#include "../include/libwidget.h"
#include "tk_client.h"

/* A paint target: the surface plus an accumulated dirty rect (union of all mark_dirty calls since the last... */
typedef struct tk_paint_target {
    tk_surface_t *surf;
    int dirty;
    int dx, dy, dw, dh;
} tk_paint_target_t;

/* Bind ctx's callbacks (the classic five + the guide-16 extensions) to paint into `target`. */
void tk_painter_bind(widget_context_t *ctx, tk_paint_target_t *target,
                     bool light_theme);

/* Reset the accumulated dirty rect (after a flush). */
void tk_paint_target_reset(tk_paint_target_t *t);

/* Raw primitives (also used directly by apps for custom painting). */
void tk_paint_fill_rect(tk_surface_t *s, int x, int y, int w, int h, uint32_t color);
void tk_paint_line(tk_surface_t *s, int x0, int y0, int x1, int y1, uint32_t color);
void tk_paint_ellipse(tk_surface_t *s, int cx, int cy, int rx, int ry, uint32_t color);
void tk_paint_gradient(tk_surface_t *s, int x, int y, int w, int h, const ui_gradient_t *g);
void tk_paint_text(tk_surface_t *s, int x, int y, const char *str, uint32_t color);

#endif /* TSUKASA_TK_PAINTER_H */
