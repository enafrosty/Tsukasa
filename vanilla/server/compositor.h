/*
 * Project Tsukasa — Dirty-Rectangle Compositor Header
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

#ifndef _VANILLA_COMPOSITOR_H
#define _VANILLA_COMPOSITOR_H

#include <stdint.h>
#include <stddef.h>
#include "../include/surface.h"
#include "../include/theme.h"
#include "font.h"
#include "image.h"

#define MAX_DIRTY_RECTS 32

typedef struct {
    uint32_t         width;
    uint32_t         height;
    uint32_t         pitch_px;
    uint32_t         bpp;
    int              fb_fd;
    void            *fb_mem;
    size_t           fb_size;
    uint32_t        *backbuffer;
    vanilla_rect_t   dirty_rects[MAX_DIRTY_RECTS];
    int              dirty_count;
    uint32_t         bg_color;
    int              is_offscreen;
    vanilla_font_t   font;
    vanilla_image_t  wallpaper;
    int              has_wallpaper;
} vanilla_compositor_t;


struct vanilla_server;

/* Compositor lifecycle */
int  compositor_init(vanilla_compositor_t *comp, const char *fb_dev);
int  compositor_init_offscreen(vanilla_compositor_t *comp, uint32_t width, uint32_t height);
void compositor_destroy(vanilla_compositor_t *comp);

/* Damage management */
void compositor_add_damage(vanilla_compositor_t *comp, const vanilla_rect_t *rect);
void compositor_merge_damage(vanilla_compositor_t *comp);
void compositor_damage_all(vanilla_compositor_t *comp);

/* Frame rendering */
void compositor_render_frame(struct vanilla_server *srv);

/*
 * Button geometry within a frame rect.
 * All values are in screen pixels at current scale.
 * Both the compositor paint path and the WM hit-test path call this function.
 */
typedef struct {
    vanilla_rect_t close_btn;    /* Bounding box of close [X] button */
    vanilla_rect_t max_btn;      /* Bounding box of maximize [] button */
    vanilla_rect_t min_btn;      /* Bounding box of minimize [_] button */
} chrome_btn_rects_t;

/*
 * Derive all titlebar button rects from the active metrics and the given frame rect.
 * frame: the full frame rect including titlebar and border.
 */
chrome_btn_rects_t chrome_metrics(const vanilla_rect_t *frame);

static inline int vanilla_rect_contains(const vanilla_rect_t *r, int32_t x, int32_t y)
{
    return x >= r->x && x < r->x + r->w && y >= r->y && y < r->y + r->h;
}

#endif /* _VANILLA_COMPOSITOR_H */
