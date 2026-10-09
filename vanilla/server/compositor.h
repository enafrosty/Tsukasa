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
#define COMPOSITOR_TARGET_FPS 60

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
    uint64_t         last_frame_ticks;
    uint32_t         frame_target_ticks;
    int              panning_enabled;

    /* Snap preview overlay */
    int              snap_preview_visible;
    vanilla_rect_t   snap_preview_rect;
    float            snap_preview_alpha;
    uint64_t         snap_preview_start;
    uint32_t         snap_preview_dur;

    /* Performance metrics */
    uint32_t         last_frame_time_us;
    uint32_t         last_dirty_count;
    uint32_t         last_damage_area_px;
} vanilla_compositor_t;

extern int g_perf_overlay_enabled;

struct vanilla_server;
struct vanilla_server_window;

/* Compositor lifecycle */
int  compositor_init(vanilla_compositor_t *comp, const char *fb_dev);
int  compositor_init_offscreen(vanilla_compositor_t *comp, uint32_t width, uint32_t height);
void compositor_destroy(vanilla_compositor_t *comp);

/* Damage management */
void compositor_add_damage(vanilla_compositor_t *comp, const vanilla_rect_t *rect);
void compositor_merge_damage(vanilla_compositor_t *comp);
void compositor_damage_all(vanilla_compositor_t *comp);

/* Frame pacing */
int  compositor_frame_due(vanilla_compositor_t *comp);
void compositor_frame_rendered(vanilla_compositor_t *comp);

/* Frame rendering */
void compositor_render_frame(struct vanilla_server *srv);
void compositor_render_overlay(vanilla_compositor_t *comp,
                               uint32_t frame_time_us,
                               int dirty_count,
                               int client_count);

/* Animation API */
void compositor_start_anim(struct vanilla_server_window *w,
                           int                          state,
                           uint32_t                     dur_ms,
                           float                        scale_start,
                           float                        scale_end,
                           float                        alpha_start,
                           float                        alpha_end,
                           int32_t                      origin_x,
                           int32_t                      origin_y,
                           int                          destroy_on_done);

void compositor_animate_windows(struct vanilla_server *srv);
int  compositor_has_active_animations(struct vanilla_server *srv);
void compositor_snap_preview_show(vanilla_compositor_t *comp, const vanilla_rect_t *target_rect);
void compositor_snap_preview_hide(vanilla_compositor_t *comp);
void compositor_snap_preview_update(vanilla_compositor_t *comp);
void compositor_paint_animated_window(vanilla_compositor_t *comp, struct vanilla_server_window *w, const vanilla_rect_t *dirty);

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
