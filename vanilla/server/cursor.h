/*
 * Project Tsukasa — Display Server ARGB Cursor System
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

#ifndef _VANILLA_CURSOR_H
#define _VANILLA_CURSOR_H

#include <stdint.h>
#include "surface.h"

typedef enum {
    CURSOR_ARROW = 0,
    CURSOR_IBEAM,
    CURSOR_RESIZE_N,
    CURSOR_RESIZE_S,
    CURSOR_RESIZE_E,
    CURSOR_RESIZE_W,
    CURSOR_RESIZE_NE,
    CURSOR_RESIZE_NW,
    CURSOR_RESIZE_SE,
    CURSOR_RESIZE_SW,
    CURSOR_HAND,
    CURSOR_WAIT,
    CURSOR_COUNT,
} vanilla_cursor_shape_t;

typedef struct {
    uint32_t *pixels;     /* ARGB; width * height entries; NULL = use built-in fallback */
    uint16_t  width;
    uint16_t  height;
    int16_t   hotspot_x;
    int16_t   hotspot_y;
} vanilla_cursor_t;

/* Initialise the cursor manager. Loads .argb files from assets_dir.
 * Falls back to built-in bitmaps for any shape that fails to load.
 * Returns 0 on success, -1 if the fallback was used for any shape. */
int  cursor_manager_init(const char *assets_dir);

/* Free all loaded cursor pixel buffers. */
void cursor_manager_destroy(void);

/* Return a pointer to the cursor for the given shape. Never NULL. */
const vanilla_cursor_t *cursor_get(vanilla_cursor_shape_t shape);

/* Set the currently active cursor shape. */
void cursor_set_active(vanilla_cursor_shape_t shape);

/* Return the currently active cursor shape. */
vanilla_cursor_shape_t cursor_get_active(void);

/* Compute bounding rectangle for active cursor at pointer position (cx, cy). */
void cursor_get_rect(int32_t cx, int32_t cy, vanilla_rect_t *out_rect);

/*
 * Alpha-blend the active cursor onto dst at (cx - hotspot_x, cy - hotspot_y),
 * clipped to dirty. Replaces wm_render_cursor.
 */
void cursor_render(uint32_t *dst, uint32_t dst_pitch,
                   uint32_t dst_w, uint32_t dst_h,
                   int32_t cx, int32_t cy,
                   const vanilla_rect_t *dirty);

/*
 * Select cursor shape based on pointer position and window hit regions.
 * Called from the WM input handler on every mouse-move event.
 * win_rect is the frame rect of the window under the pointer, or NULL.
 * Returns the selected shape and calls cursor_set_active().
 */
vanilla_cursor_shape_t cursor_select_for_hit_region(int32_t px, int32_t py,
                                                    const vanilla_rect_t *win_rect,
                                                    int32_t border_width,
                                                    int32_t titlebar_height);

#endif /* _VANILLA_CURSOR_H */
