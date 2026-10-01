/*
 * Project Tsukasa — Display Server Flex Layout Engine API
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

#ifndef _VANILLA_LAYOUT_H
#define _VANILLA_LAYOUT_H

#include <stdint.h>
#include <stddef.h>
#include "draw_cmd.h"
#if __has_include("theme.h")
#include "theme.h"
#endif

/* Flex sizing modes */
typedef enum {
    VSIZE_FIXED,   /* exact px value from .size_px               */
    VSIZE_FIT,     /* shrink to content                          */
    VSIZE_GROW,    /* claim available space (flex-grow: 1)       */
} vanilla_size_mode_t;

/* Element type — determines which draw command(s) are emitted */
typedef enum {
    VELEM_BOX,     /* layout container; may have children        */
    VELEM_TEXT,    /* text label                                 */
    VELEM_IMAGE,   /* image / icon                               */
    VELEM_SPACER,  /* invisible flex spacer                      */
} vanilla_elem_type_t;

/* Flex direction */
typedef enum {
    VDIR_ROW,
    VDIR_COLUMN,
} vanilla_flex_dir_t;

/* Alignment along the cross axis */
typedef enum {
    VALIGN_START,
    VALIGN_CENTER,
    VALIGN_END,
    VALIGN_STRETCH,
} vanilla_align_t;

typedef struct vanilla_elem_t vanilla_elem_t;
struct vanilla_elem_t {
    vanilla_elem_type_t type;

    /* Sizing */
    vanilla_size_mode_t w_mode, h_mode;
    int32_t             w_px,   h_px;   /* used when mode == VSIZE_FIXED     */

    /* Padding (inner spacing) and gap (between children) */
    int32_t pad_top, pad_right, pad_bottom, pad_left;
    int32_t gap;

    /* Flex container properties */
    vanilla_flex_dir_t  direction;
    vanilla_align_t     align_items;

    /* Appearance (for VELEM_BOX) */
    uint32_t            bg_color;       /* 0 = transparent                   */
    int32_t             corner_radius;  /* 0 = square; >0 = rounded (P1-3)   */
    uint32_t            border_color;
    int32_t             border_width;
    uint8_t             clip_children;  /* 1 = scissor clip children to inner bounds */

    /* Content (for VELEM_TEXT) */
    const char         *text;
    uint32_t            text_color;
    float               font_size;

    /* Content (for VELEM_IMAGE) */
    vanilla_surface_t  *image_src;
    vanilla_draw_rect_t image_src_rect;

    /* Tree links */
    vanilla_elem_t     *children;   /* first child                           */
    vanilla_elem_t     *next;       /* next sibling                          */

    /* Computed geometry (filled by vlayout_compute) */
    int32_t             computed_x, computed_y, computed_w, computed_h;
};

/* Opaque layout context; backed by a caller-supplied arena. */
typedef struct vanilla_layout_t vanilla_layout_t;

/* Initialise a layout context using caller-supplied memory.
 * arena_size should be at least 64 KiB for a typical window.       */
vanilla_layout_t *vlayout_init(void *arena, size_t arena_size);

/* Reset the context for a new frame; does not free the arena.       */
void vlayout_reset(vanilla_layout_t *ctx);

/* Allocate an element from the context arena.
 * Returns NULL if the arena is exhausted.                           */
vanilla_elem_t *vlayout_alloc_elem(vanilla_layout_t *ctx);

/* Compute geometry for the subtree rooted at root, constrained to
 * the given bounding rect. Must be called before vlayout_emit().   */
void vlayout_compute(vanilla_layout_t *ctx, vanilla_elem_t *root,
                     int32_t bound_x, int32_t bound_y,
                     int32_t bound_w, int32_t bound_h);

/* Walk the computed tree and append draw commands to out.           */
void vlayout_emit(vanilla_layout_t *ctx, vanilla_elem_t *root,
                  vanilla_draw_cmd_array_t *out);

/* Builder convenience helpers */
vanilla_elem_t *vlayout_box(vanilla_layout_t *ctx);
vanilla_elem_t *vlayout_spacer(vanilla_layout_t *ctx);
vanilla_elem_t *vlayout_text(vanilla_layout_t *ctx, const char *text, uint32_t color, float font_size);
vanilla_elem_t *vlayout_image(vanilla_layout_t *ctx, vanilla_surface_t *src, vanilla_draw_rect_t src_rect);
void vlayout_add_child(vanilla_elem_t *parent, vanilla_elem_t *child);

#endif /* _VANILLA_LAYOUT_H */
