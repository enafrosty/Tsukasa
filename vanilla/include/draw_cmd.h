/*
 * Project Tsukasa — Display Server Draw Command Types
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

#ifndef _VANILLA_DRAW_CMD_H
#define _VANILLA_DRAW_CMD_H

#include <stdint.h>
#include "surface.h"

typedef enum {
    VCMD_NONE = 0,
    VCMD_FILL_RECT,       /* solid filled rectangle                          */
    VCMD_ROUNDED_RECT,    /* AA rounded rectangle (added by P1-3)            */
    VCMD_BORDER,          /* 1-sided or 4-sided border outline               */
    VCMD_TEXT,            /* text string at a position                       */
    VCMD_IMAGE,           /* blit a vanilla_surface_t sub-region             */
    VCMD_PUSH_CLIP,       /* push a scissor rectangle onto the clip stack    */
    VCMD_POP_CLIP,        /* pop top scissor rectangle                       */
} vanilla_draw_cmd_type_t;

typedef struct {
    int32_t x, y, w, h;
} vanilla_draw_rect_t;

typedef struct {
    vanilla_draw_cmd_type_t type;
    vanilla_draw_rect_t     bounds;  /* bounding box in window-local coords  */
    union {
        struct { uint32_t color; }                              fill_rect;
        struct { uint32_t color; int32_t radius; }             rounded_rect;
        struct { uint32_t color; int32_t width; }              border;
        struct {
            const char *text;
            uint32_t    color;
            float       font_size;
            int32_t     font_id;    /* 0 = default server font               */
        }                                                       text;
        struct {
            vanilla_surface_t *src;
            vanilla_draw_rect_t src_rect;
        }                                                       image;
        /* push_clip and pop_clip have no additional fields beyond bounds    */
    };
} vanilla_draw_cmd_t;

/* A flat array of draw commands, produced by vlayout_compute(). */
typedef struct {
    vanilla_draw_cmd_t *cmds;
    int32_t             count;
    int32_t             capacity;
} vanilla_draw_cmd_array_t;

void vanilla_execute_draw_commands(vanilla_surface_t *target,
                                   const vanilla_draw_cmd_array_t *cmds,
                                   const vanilla_rect_t *clip);

#endif /* _VANILLA_DRAW_CMD_H */
