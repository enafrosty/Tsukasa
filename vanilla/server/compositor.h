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
#include "font.h"
#include "image.h"

#define MAX_DIRTY_RECTS 32

/* Server-side decoration dimensions */
#define TITLEBAR_HEIGHT 24
#define WINDOW_BORDER_WIDTH 1
#define SHADOW_RADIUS 6
#define SHADOW_ALPHA 70
#define TITLEBAR_BTN_SIZE 12
#define TITLEBAR_BTN_PAD  4
#define TITLEBAR_FONT_SIZE 13.0f

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

#endif /* _VANILLA_COMPOSITOR_H */
