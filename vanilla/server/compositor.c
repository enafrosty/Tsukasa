/*
 * Project Tsukasa — Dirty-Rectangle Compositor Implementation
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

#include "compositor.h"
#include "server.h"
#include "blitter.h"
#include "anim.h"

#include <fcntl.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <errno.h>
#include <sys/ioctl.h>
#include <sys/mman.h>

#ifndef FBIOPUT_VSCREENINFO
#define FBIOPUT_VSCREENINFO 0x4601
#endif

#if defined(VANILLA_HOST) || defined(_WIN32) || (defined(__STDC_HOSTED__) && __STDC_HOSTED__ == 1)
#if defined(_WIN32)
#include <windows.h>
__attribute__((weak)) uint64_t pit_ticks(void)
{
    return (uint64_t)GetTickCount64();
}
#else
#include <time.h>
__attribute__((weak)) uint64_t pit_ticks(void)
{
    struct timespec ts;
    if (clock_gettime(CLOCK_MONOTONIC, &ts) == 0)
        return (uint64_t)ts.tv_sec * 1000ULL + (uint64_t)(ts.tv_nsec / 1000000L);
    return 0;
}
#endif

__attribute__((weak)) uint32_t pit_frequency(void)
{
    return 1000;
}
#else
#include <sys/syscall.h>
#ifndef SYS_ticks
#define SYS_ticks 320
#endif

__attribute__((weak)) uint64_t pit_ticks(void)
{
    long t = __syscall0(SYS_ticks);
    return (t > 0) ? (uint64_t)t : 0;
}

__attribute__((weak)) uint32_t pit_frequency(void)
{
    return 100;
}
#endif

int compositor_frame_due(vanilla_compositor_t *comp)
{
    if (!comp)
        return 0;

    if (comp->last_frame_ticks == 0)
        return 1;

    uint64_t now = pit_ticks();
    if (now < comp->last_frame_ticks)
        return 1;

    uint64_t delta = now - comp->last_frame_ticks;
    return (delta >= comp->frame_target_ticks) ? 1 : 0;
}

void compositor_frame_rendered(vanilla_compositor_t *comp)
{
    if (!comp)
        return;

    comp->last_frame_ticks = pit_ticks();
    if (comp->last_frame_ticks == 0)
        comp->last_frame_ticks = 1;
}

int compositor_init_offscreen(vanilla_compositor_t *comp, uint32_t width, uint32_t height)
{
    if (!comp || width == 0 || height == 0)
        return -1;

    memset(comp, 0, sizeof(*comp));
    comp->width = width;
    comp->height = height;
    comp->pitch_px = width;
    comp->bpp = 32;
    comp->fb_fd = -1;
    comp->fb_mem = NULL;
    comp->fb_size = 0;
    comp->is_offscreen = 1;
    comp->bg_color = g_theme->bg_base;
    comp->has_wallpaper = 0;
    font_init(&comp->font, NULL, 0);

    uint32_t freq = pit_frequency();
    if (freq == 0)
        freq = 1000;
    comp->frame_target_ticks = (freq + COMPOSITOR_TARGET_FPS - 1) / COMPOSITOR_TARGET_FPS;
    if (comp->frame_target_ticks == 0)
        comp->frame_target_ticks = 1;
    comp->last_frame_ticks = 0;
    comp->panning_enabled = 0;

    size_t backbuffer_size = (size_t)width * height * sizeof(uint32_t);
    comp->backbuffer = (uint32_t *)malloc(backbuffer_size);
    if (!comp->backbuffer) {
        font_destroy(&comp->font);
        return -1;
    }

    compositor_damage_all(comp);
    return 0;
}

int compositor_init(vanilla_compositor_t *comp, const char *fb_dev)
{
    if (!comp)
        return -1;

    memset(comp, 0, sizeof(*comp));
    comp->fb_fd = -1;
    comp->bg_color = g_theme->bg_base;
    comp->has_wallpaper = 0;
    font_init(&comp->font, NULL, 0);

    uint32_t freq = pit_frequency();
    if (freq == 0)
        freq = 1000;
    comp->frame_target_ticks = (freq + COMPOSITOR_TARGET_FPS - 1) / COMPOSITOR_TARGET_FPS;
    if (comp->frame_target_ticks == 0)
        comp->frame_target_ticks = 1;
    comp->last_frame_ticks = 0;
    comp->panning_enabled = 0;

    if (image_load_file(&comp->wallpaper, "/assets/wallpaper.bmp") == 0 ||
        image_load_file(&comp->wallpaper, "assets/wallpaper.bmp") == 0) {
        comp->has_wallpaper = 1;
    }

    const char *dev_path = fb_dev ? fb_dev : "/dev/fb0";
    int fd = open(dev_path, O_RDWR, 0);
    if (fd < 0) {
        printf("[vanilla] Warning: Failed to open %s (errno=%d), falling back to offscreen\n", dev_path, errno);
        if (comp->has_wallpaper) {
            image_destroy(&comp->wallpaper);
            comp->has_wallpaper = 0;
        }
        font_destroy(&comp->font);
        return compositor_init_offscreen(comp, 1024, 768);
    }

    ioctl(fd, KDSETMODE, (void *)KD_GRAPHICS);

    struct fb_var_screeninfo vinfo;
    struct fb_fix_screeninfo finfo;
    if (ioctl(fd, FBIOGET_VSCREENINFO, &vinfo) < 0 ||
        ioctl(fd, FBIOGET_FSCREENINFO, &finfo) < 0) {
        printf("[vanilla] Warning: Failed to query fb info, falling back to offscreen\n");
        close(fd);
        if (comp->has_wallpaper) {
            image_destroy(&comp->wallpaper);
            comp->has_wallpaper = 0;
        }
        font_destroy(&comp->font);
        return compositor_init_offscreen(comp, 1024, 768);
    }

    if (vinfo.bits_per_pixel != 32) {
        printf("[vanilla] Error: Unsupported bpp %u (must be 32 bpp), falling back to offscreen\n", vinfo.bits_per_pixel);
        close(fd);
        if (comp->has_wallpaper) {
            image_destroy(&comp->wallpaper);
            comp->has_wallpaper = 0;
        }
        font_destroy(&comp->font);
        return compositor_init_offscreen(comp, 1024, 768);
    }

    comp->width = vinfo.xres;
    comp->height = vinfo.yres;
    comp->bpp = vinfo.bits_per_pixel;
    comp->pitch_px = finfo.line_length / (comp->bpp / 8);
    comp->fb_size = finfo.smem_len;
    comp->fb_fd = fd;

    comp->fb_mem = mmap(NULL, comp->fb_size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (comp->fb_mem == MAP_FAILED) {
        printf("[vanilla] Warning: Failed to mmap fb (%u bytes, errno=%d), falling back to offscreen\n",
               (unsigned)comp->fb_size, errno);
        comp->fb_mem = NULL;
        close(fd);
        comp->fb_fd = -1;
        if (comp->has_wallpaper) {
            image_destroy(&comp->wallpaper);
            comp->has_wallpaper = 0;
        }
        font_destroy(&comp->font);
        return compositor_init_offscreen(comp, comp->width, comp->height);
    }

    printf("[vanilla] Framebuffer mapped: %ux%u @ %u bpp, pitch=%u px, size=%u bytes\n",
           comp->width, comp->height, comp->bpp, comp->pitch_px, (unsigned)comp->fb_size);

#if !defined(_WIN32) && !defined(VANILLA_HOST) && (!defined(__STDC_HOSTED__) || __STDC_HOSTED__ == 0)
    struct fb_var_screeninfo pan_vinfo = vinfo;
    pan_vinfo.yres_virtual = vinfo.yres * 2;
    if (ioctl(fd, FBIOPUT_VSCREENINFO, &pan_vinfo) == 0 &&
        pan_vinfo.yres_virtual >= vinfo.yres * 2) {
        comp->panning_enabled = 1;
    }
#endif
    printf("[vanilla] Hardware panning: %s\n", comp->panning_enabled ? "enabled" : "unavailable");

    size_t backbuffer_size = (size_t)comp->pitch_px * comp->height * sizeof(uint32_t);
    comp->backbuffer = (uint32_t *)malloc(backbuffer_size);
    if (!comp->backbuffer) {
        munmap(comp->fb_mem, comp->fb_size);
        comp->fb_mem = NULL;
        close(comp->fb_fd);
        comp->fb_fd = -1;
        font_destroy(&comp->font);
        if (comp->has_wallpaper) {
            image_destroy(&comp->wallpaper);
            comp->has_wallpaper = 0;
        }
        return -1;
    }

    compositor_damage_all(comp);
    return 0;
}

void compositor_destroy(vanilla_compositor_t *comp)
{
    if (!comp)
        return;

    vanilla_shadow_cache_clear();

    font_destroy(&comp->font);
    if (comp->has_wallpaper) {
        image_destroy(&comp->wallpaper);
        comp->has_wallpaper = 0;
    }

    if (comp->backbuffer) {
        free(comp->backbuffer);
        comp->backbuffer = NULL;
    }

    if (comp->fb_mem && comp->fb_size > 0) {
        munmap(comp->fb_mem, comp->fb_size);
        comp->fb_mem = NULL;
    }

    if (comp->fb_fd >= 0) {
        close(comp->fb_fd);
        comp->fb_fd = -1;
    }

    comp->dirty_count = 0;
}

void compositor_damage_all(vanilla_compositor_t *comp)
{
    if (!comp)
        return;

    vanilla_shadow_cache_clear();

    vanilla_rect_t full;
    full.x = 0;
    full.y = 0;
    full.w = (int32_t)comp->width;
    full.h = (int32_t)comp->height;

    comp->dirty_rects[0] = full;
    comp->dirty_count = 1;
}

void compositor_add_damage(vanilla_compositor_t *comp, const vanilla_rect_t *rect)
{
    if (!comp || !rect || rect->w <= 0 || rect->h <= 0)
        return;

    vanilla_rect_t screen_rect;
    screen_rect.x = 0;
    screen_rect.y = 0;
    screen_rect.w = (int32_t)comp->width;
    screen_rect.h = (int32_t)comp->height;

    vanilla_rect_t clipped;
    if (!vanilla_rect_intersect(rect, &screen_rect, &clipped))
        return;

    if (comp->dirty_count >= MAX_DIRTY_RECTS) {
        vanilla_rect_t bounding = comp->dirty_rects[0];
        for (int i = 1; i < comp->dirty_count; i++)
            vanilla_rect_union(&bounding, &comp->dirty_rects[i], &bounding);
        vanilla_rect_union(&bounding, &clipped, &bounding);

        comp->dirty_rects[0] = bounding;
        comp->dirty_count = 1;
        return;
    }

    comp->dirty_rects[comp->dirty_count++] = clipped;
}

void compositor_merge_damage(vanilla_compositor_t *comp)
{
    if (!comp || comp->dirty_count <= 1)
        return;

    int merged = 1;
    while (merged) {
        merged = 0;
        for (int i = 0; i < comp->dirty_count; i++) {
            for (int j = i + 1; j < comp->dirty_count; j++) {
                vanilla_rect_t *r1 = &comp->dirty_rects[i];
                vanilla_rect_t *r2 = &comp->dirty_rects[j];

                vanilla_rect_t expanded_r1 = *r1;
                expanded_r1.x -= 1;
                expanded_r1.y -= 1;
                expanded_r1.w += 2;
                expanded_r1.h += 2;

                vanilla_rect_t isect;
                if (vanilla_rect_intersect(&expanded_r1, r2, &isect)) {
                    vanilla_rect_union(r1, r2, r1);
                    comp->dirty_rects[j] = comp->dirty_rects[comp->dirty_count - 1];
                    comp->dirty_count--;
                    merged = 1;
                    break;
                }
            }
            if (merged)
                break;
        }
    }
}

chrome_btn_rects_t chrome_metrics(const vanilla_rect_t *frame)
{
    chrome_btn_rects_t r;
    int32_t btn   = g_theme->btn_size;
    int32_t pad   = (g_theme->titlebar_height - btn) / 2;
    int32_t gap   = btn + g_theme->space_1;
    int32_t right = frame->x + frame->w;
    /* Right button margin matches titlebar vertical padding: right - pad - btn */
    r.close_btn.x = right - (btn + pad);
    r.close_btn.y = frame->y + pad;
    r.close_btn.w = btn;
    r.close_btn.h = btn;

    r.max_btn.x = r.close_btn.x - gap;
    r.max_btn.y = frame->y + pad;
    r.max_btn.w = btn;
    r.max_btn.h = btn;

    r.min_btn.x = r.max_btn.x - gap;
    r.min_btn.y = frame->y + pad;
    r.min_btn.w = btn;
    r.min_btn.h = btn;

    return r;
}

void compositor_start_anim(vanilla_server_window_t *w,
                           int                      state,
                           uint32_t                 dur_ms,
                           float                    scale_start,
                           float                    scale_end,
                           float                    alpha_start,
                           float                    alpha_end,
                           int32_t                  origin_x,
                           int32_t                  origin_y,
                           int                      destroy_on_done)
{
    if (!w)
        return;

    if (g_theme->reduce_motion != 0) {
        w->anim_state = ANIM_IDLE;
        w->anim_current_scale = scale_end;
        w->anim_current_alpha = alpha_end;
        w->anim_destroy_on_done = 0;
        w->anim_unmap_on_done = 0;
        return;
    }

    uint32_t freq = pit_frequency();
    if (freq == 0)
        freq = 1000;

    uint32_t dur_ticks = (dur_ms * freq + 999) / 1000;
    if (dur_ticks == 0)
        dur_ticks = 1;

    w->anim_state = state;
    w->anim_start_ticks = pit_ticks();
    w->anim_dur_ticks = dur_ticks;
    w->anim_scale_start = scale_start;
    w->anim_scale_end = scale_end;
    w->anim_alpha_start = alpha_start;
    w->anim_alpha_end = alpha_end;
    w->anim_origin_x = origin_x;
    w->anim_origin_y = origin_y;
    w->anim_destroy_on_done = destroy_on_done;
    w->anim_unmap_on_done = (state == ANIM_MINIMIZING) ? 1 : 0;
    w->anim_current_scale = scale_start;
    w->anim_current_alpha = alpha_start;
}

void compositor_snap_preview_show(vanilla_compositor_t *comp, const vanilla_rect_t *target_rect)
{
    if (!comp || !target_rect || g_theme->reduce_motion != 0)
        return;

    if (comp->snap_preview_visible &&
        comp->snap_preview_rect.x == target_rect->x &&
        comp->snap_preview_rect.y == target_rect->y &&
        comp->snap_preview_rect.w == target_rect->w &&
        comp->snap_preview_rect.h == target_rect->h) {
        return;
    }

    if (comp->snap_preview_visible)
        compositor_add_damage(comp, &comp->snap_preview_rect);

    comp->snap_preview_visible = 1;
    comp->snap_preview_rect = *target_rect;
    comp->snap_preview_start = pit_ticks();

    uint32_t freq = pit_frequency();
    if (freq == 0)
        freq = 1000;
    comp->snap_preview_dur = (80 * freq + 999) / 1000;
    if (comp->snap_preview_dur == 0)
        comp->snap_preview_dur = 1;
    comp->snap_preview_alpha = 0.0f;

    compositor_add_damage(comp, &comp->snap_preview_rect);
}

void compositor_snap_preview_hide(vanilla_compositor_t *comp)
{
    if (!comp || !comp->snap_preview_visible)
        return;

    comp->snap_preview_visible = 0;
    compositor_add_damage(comp, &comp->snap_preview_rect);
}

void compositor_snap_preview_update(vanilla_compositor_t *comp)
{
    if (!comp || !comp->snap_preview_visible)
        return;

    if (g_theme->reduce_motion != 0) {
        comp->snap_preview_visible = 0;
        return;
    }

    uint64_t now = pit_ticks();
    uint64_t elapsed = (now >= comp->snap_preview_start) ? (now - comp->snap_preview_start) : 0;
    float t = (float)elapsed / (float)comp->snap_preview_dur;
    if (t > 1.0f)
        t = 1.0f;

    comp->snap_preview_alpha = anim_ease_out_quad(t);
    if (t < 1.0f)
        compositor_add_damage(comp, &comp->snap_preview_rect);
}

void compositor_animate_windows(vanilla_server_t *srv)
{
    if (!srv)
        return;

    compositor_snap_preview_update(&srv->compositor);

    if (g_theme->reduce_motion != 0)
        return;

    uint64_t now = pit_ticks();

    for (int i = 0; i < VANILLA_MAX_WINDOWS; i++) {
        vanilla_server_window_t *w = &srv->windows[i];
        if (!w->in_use || w->anim_state == ANIM_IDLE)
            continue;

        uint64_t elapsed = (now >= w->anim_start_ticks) ? (now - w->anim_start_ticks) : 0;
        float t = (float)elapsed / (float)w->anim_dur_ticks;
        if (t > 1.0f)
            t = 1.0f;

        float et;
        switch (w->anim_state) {
        case ANIM_OPENING:
        case ANIM_RESTORING:
            et = anim_ease_out_cubic(t);
            break;
        case ANIM_CLOSING:
        case ANIM_MINIMIZING:
            et = anim_ease_out_quad(t);
            break;
        default:
            et = t;
            break;
        }

        w->anim_current_scale = anim_lerp(w->anim_scale_start, w->anim_scale_end, et);
        w->anim_current_alpha = anim_lerp(w->anim_alpha_start, w->anim_alpha_end, et);

        wm_invalidate_window(srv, w);

        if (t >= 1.0f) {
            if (w->anim_destroy_on_done) {
                vanilla_server_destroy_window_record(srv, w);
            } else if (w->anim_unmap_on_done) {
                w->is_mapped = 0;
                w->is_focused = 0;
                if (srv->focused_window_id == w->window_id)
                    srv->focused_window_id = 0;
                w->anim_state = ANIM_IDLE;
                w->anim_unmap_on_done = 0;
                w->anim_current_scale = 1.0f;
                w->anim_current_alpha = 1.0f;
                wm_invalidate_window(srv, w);
            } else {
                w->anim_state = ANIM_IDLE;
                w->anim_current_scale = w->anim_scale_end;
                w->anim_current_alpha = w->anim_alpha_end;
                wm_invalidate_window(srv, w);
            }
        }
    }
}

int compositor_has_active_animations(vanilla_server_t *srv)
{
    if (!srv || g_theme->reduce_motion != 0)
        return 0;

    if (srv->compositor.snap_preview_visible && srv->compositor.snap_preview_alpha < 1.0f)
        return 1;

    for (int i = 0; i < VANILLA_MAX_WINDOWS; i++) {
        if (srv->windows[i].in_use && srv->windows[i].anim_state != ANIM_IDLE)
            return 1;
    }

    for (int d = 0; d < srv->context_menu_depth; d++) {
        if (srv->context_menu_stack[d].active && srv->context_menu_stack[d].anim_alpha < 1.0f)
            return 1;
    }
    return 0;
}

void compositor_paint_animated_window(vanilla_compositor_t *comp, vanilla_server_window_t *w, const vanilla_rect_t *dirty)
{
    if (!comp || !w || !w->surface.pixels || w->width == 0 || w->height == 0)
        return;

    float scale = w->anim_current_scale;
    float alpha = w->anim_current_alpha;

    if (scale <= 0.01f || alpha <= 0.01f)
        return;

    if (scale > 2.0f)
        scale = 2.0f;
    if (alpha > 1.0f)
        alpha = 1.0f;

    uint8_t global_alpha = (uint8_t)(alpha * 255.0f);
    if (global_alpha == 0)
        return;

    int32_t dest_w = (int32_t)((float)w->width * scale);
    int32_t dest_h = (int32_t)((float)w->height * scale);
    if (dest_w < 1) dest_w = 1;
    if (dest_h < 1) dest_h = 1;

    int32_t dest_x = w->anim_origin_x + (int32_t)((float)(w->x - w->anim_origin_x) * scale);
    int32_t dest_y = w->anim_origin_y + (int32_t)((float)(w->y - w->anim_origin_y) * scale);

    vanilla_rect_t dest_rect = { dest_x, dest_y, dest_w, dest_h };
    vanilla_rect_t screen_rect = { 0, 0, (int32_t)comp->width, (int32_t)comp->height };
    vanilla_rect_t clamped_dest;
    if (!vanilla_rect_intersect(&dest_rect, &screen_rect, &clamped_dest))
        return;

    vanilla_rect_t vis_dest;
    if (dirty) {
        if (!vanilla_rect_intersect(&clamped_dest, dirty, &vis_dest))
            return;
    } else {
        vis_dest = clamped_dest;
    }

    int32_t surf_w = (int32_t)w->surface.width;
    int32_t surf_h = (int32_t)w->surface.height;
    if (surf_w <= 0 || surf_h <= 0)
        return;

    int32_t src_sub_w = dest_w > surf_w ? surf_w : dest_w;
    int32_t src_sub_h = dest_h > surf_h ? surf_h : dest_h;
    int32_t src_base_x = (surf_w - src_sub_w) / 2;
    int32_t src_base_y = (surf_h - src_sub_h) / 2;
    if (src_base_x < 0) src_base_x = 0;
    if (src_base_y < 0) src_base_y = 0;

    int32_t offset_x = vis_dest.x - dest_rect.x;
    int32_t offset_y = vis_dest.y - dest_rect.y;

    int32_t src_x = src_base_x + offset_x;
    int32_t src_y = src_base_y + offset_y;

    if (src_x < 0 || src_y < 0 || src_x >= surf_w || src_y >= surf_h)
        return;

    if (src_x + vis_dest.w > surf_w)
        vis_dest.w = surf_w - src_x;
    if (src_y + vis_dest.h > surf_h)
        vis_dest.h = surf_h - src_y;

    if (vis_dest.w <= 0 || vis_dest.h <= 0)
        return;

    uint32_t surf_pitch_px = w->surface.pitch / sizeof(uint32_t);
    if (surf_pitch_px == 0)
        surf_pitch_px = (uint32_t)surf_w;

    blt_blend_subrect(comp->backbuffer, comp->pitch_px,
                      vis_dest.x, vis_dest.y,
                      w->surface.pixels, surf_pitch_px,
                      src_x, src_y,
                      vis_dest.w, vis_dest.h, global_alpha);
}

void compositor_render_frame(struct vanilla_server *srv)
{
    if (!srv || srv->compositor.dirty_count <= 0)
        return;

    compositor_animate_windows(srv);

    printf("[vanilla] compositor: render\n");

    vanilla_compositor_t *comp = &srv->compositor;
    compositor_merge_damage(comp);

    vanilla_server_window_t *sorted[VANILLA_MAX_WINDOWS];
    int win_count = 0;

    for (int i = 0; i < VANILLA_MAX_WINDOWS; i++) {
        if (srv->windows[i].in_use && srv->windows[i].is_mapped)
            sorted[win_count++] = &srv->windows[i];
    }

    /* Stable insertion sort by (layer, z_index) ascending */
    for (int i = 1; i < win_count; i++) {
        vanilla_server_window_t *key = sorted[i];
        int j = i - 1;
        while (j >= 0 && (sorted[j]->layer > key->layer ||
               (sorted[j]->layer == key->layer && sorted[j]->z_index > key->z_index))) {
            sorted[j + 1] = sorted[j];
            j--;
        }
        sorted[j + 1] = key;
    }

    for (int d = 0; d < comp->dirty_count; d++) {
        const vanilla_rect_t *dirty = &comp->dirty_rects[d];
        if (dirty->w <= 0 || dirty->h <= 0)
            continue;

        /* 1. Fill background (wallpaper or solid color) */
        if (comp->has_wallpaper && comp->wallpaper.pixels) {
            vanilla_rect_t screen_rect = { 0, 0, (int32_t)comp->width, (int32_t)comp->height };
            image_draw_scaled(comp->backbuffer, comp->pitch_px, dirty, &comp->wallpaper, &screen_rect);
        } else {
            blt_fill_rect(comp->backbuffer, comp->pitch_px, dirty, comp->bg_color);
        }

        /* 2. Draw drop shadows */
        for (int w = 0; w < win_count; w++) {
            vanilla_server_window_t *win = sorted[w];
            if ((win->flags & WINDOW_FLAG_BORDERLESS) || win->is_snapped == SNAP_MAXIMIZE)
                continue;
            if (win->anim_state != ANIM_IDLE && win->anim_state != ANIM_FOCUS &&
                g_theme->reduce_motion == 0 && win->anim_current_alpha <= 0.05f)
                continue;

            uint8_t sh_alpha = g_theme->shadow_alpha;
            if (win->anim_state != ANIM_IDLE && g_theme->reduce_motion == 0) {
                sh_alpha = (uint8_t)((float)sh_alpha * win->anim_current_alpha);
            }

            vanilla_rect_t frame_rect;
            wm_get_frame_rect(win, &frame_rect);

            vanilla_rect_t shadow_bounds;
            shadow_bounds.x = frame_rect.x - g_theme->shadow_radius;
            shadow_bounds.y = frame_rect.y - g_theme->shadow_radius;
            shadow_bounds.w = frame_rect.w + 2 * g_theme->shadow_radius;
            shadow_bounds.h = frame_rect.h + 2 * g_theme->shadow_radius;

            vanilla_rect_t dummy;
            if (vanilla_rect_intersect(&shadow_bounds, dirty, &dummy)) {
                blt_draw_shadow_cached(comp->backbuffer, comp->pitch_px, comp->width, comp->height,
                                       &frame_rect, dirty, g_theme->shadow_radius, sh_alpha);
            }
        }

        /* 3. Render windows in Z-order */
        for (int w = 0; w < win_count; w++) {
            vanilla_server_window_t *win = sorted[w];
            if (win->anim_state != ANIM_IDLE && win->anim_state != ANIM_FOCUS &&
                g_theme->reduce_motion == 0 && win->anim_current_alpha <= 0.05f)
                continue;

            vanilla_rect_t frame_rect;
            wm_get_frame_rect(win, &frame_rect);

            vanilla_rect_t vis_frame;
            if (!vanilla_rect_intersect(&frame_rect, dirty, &vis_frame))
                continue;

            /* Window frame decoration if not borderless */
            if (!(win->flags & WINDOW_FLAG_BORDERLESS)) {
                vanilla_rect_t title_rect;
                title_rect.x = frame_rect.x;
                title_rect.y = frame_rect.y;
                title_rect.w = frame_rect.w;
                title_rect.h = g_theme->titlebar_height + g_theme->border_width;

                vanilla_rect_t vis_title;
                if (vanilla_rect_intersect(&title_rect, dirty, &vis_title)) {
                    uint32_t tb_color = win->is_focused ? g_theme->titlebar_active : g_theme->titlebar_inactive;
                    if (win->anim_state == ANIM_FOCUS && g_theme->reduce_motion == 0 && win->anim_current_alpha > 1.0f) {
                        float boost = win->anim_current_alpha;
                        uint32_t a = (tb_color >> 24) & 0xFF;
                        uint32_t r = (uint32_t)(((tb_color >> 16) & 0xFF) * boost);
                        uint32_t g = (uint32_t)(((tb_color >> 8) & 0xFF) * boost);
                        uint32_t b = (uint32_t)((tb_color & 0xFF) * boost);
                        if (r > 255) r = 255;
                        if (g > 255) g = 255;
                        if (b > 255) b = 255;
                        tb_color = (a << 24) | (r << 16) | (g << 8) | b;
                    } else if (win->anim_state != ANIM_IDLE && g_theme->reduce_motion == 0 && win->anim_current_alpha < 1.0f) {
                        uint32_t a = (uint32_t)(((tb_color >> 24) & 0xFF) * win->anim_current_alpha);
                        tb_color = (a << 24) | (tb_color & 0x00FFFFFF);
                    }
                    blt_rounded_rect_clipped(comp->backbuffer, comp->pitch_px,
                                             title_rect.x, title_rect.y, title_rect.w, title_rect.h,
                                             g_theme->radius_sm, tb_color,
                                             BLT_CORNER_TOP, dirty);

                    /* Titlebar buttons */
                    chrome_btn_rects_t btns = chrome_metrics(&frame_rect);

                    vanilla_rect_t vis_btn;
                    int32_t btn_glyph_pad = THEME_PX(2);
                    int32_t btn_glyph_end = btns.close_btn.w - 1 - btn_glyph_pad;

                    if (vanilla_rect_intersect(&btns.close_btn, dirty, &vis_btn)) {
                        blt_fill_rect(comp->backbuffer, comp->pitch_px, &vis_btn, g_theme->danger);
                        for (int k = btn_glyph_pad; k <= btn_glyph_end; k++) {
                            int32_t px1 = btns.close_btn.x + k;
                            int32_t py1 = btns.close_btn.y + k;
                            int32_t px2 = btns.close_btn.x + (btns.close_btn.w - 1 - k);
                            int32_t py2 = btns.close_btn.y + k;
                            if (px1 >= dirty->x && px1 < dirty->x + dirty->w &&
                                py1 >= dirty->y && py1 < dirty->y + dirty->h)
                                comp->backbuffer[py1 * comp->pitch_px + px1] = g_theme->titlebar_btn_icon;
                            if (px2 >= dirty->x && px2 < dirty->x + dirty->w &&
                                py2 >= dirty->y && py2 < dirty->y + dirty->h)
                                comp->backbuffer[py2 * comp->pitch_px + px2] = g_theme->titlebar_btn_icon;
                        }
                    }

                    /* Maximize button [] */
                    if (vanilla_rect_intersect(&btns.max_btn, dirty, &vis_btn)) {
                        uint32_t btn_bg = g_theme->titlebar_btn_bg;
                        blt_fill_rect(comp->backbuffer, comp->pitch_px, &vis_btn, btn_bg);
                        int32_t top_y = btns.max_btn.y + btn_glyph_pad;
                        int32_t bot_y = btns.max_btn.y + btn_glyph_end;
                        int32_t left_x = btns.max_btn.x + btn_glyph_pad;
                        int32_t right_x = btns.max_btn.x + btn_glyph_end;
                        for (int k = btn_glyph_pad; k <= btn_glyph_end; k++) {
                            int32_t px = btns.max_btn.x + k;
                            if (px >= dirty->x && px < dirty->x + dirty->w) {
                                if (top_y >= dirty->y && top_y < dirty->y + dirty->h)
                                    comp->backbuffer[top_y * comp->pitch_px + px] = g_theme->titlebar_btn_icon;
                                if (bot_y >= dirty->y && bot_y < dirty->y + dirty->h)
                                    comp->backbuffer[bot_y * comp->pitch_px + px] = g_theme->titlebar_btn_icon;
                            }
                            int32_t py = btns.max_btn.y + k;
                            if (py >= dirty->y && py < dirty->y + dirty->h) {
                                if (left_x >= dirty->x && left_x < dirty->x + dirty->w)
                                    comp->backbuffer[py * comp->pitch_px + left_x] = g_theme->titlebar_btn_icon;
                                if (right_x >= dirty->x && right_x < dirty->x + dirty->w)
                                    comp->backbuffer[py * comp->pitch_px + right_x] = g_theme->titlebar_btn_icon;
                            }
                        }
                    }

                    /* Minimize button [_] */
                    if (vanilla_rect_intersect(&btns.min_btn, dirty, &vis_btn)) {
                        uint32_t btn_bg = g_theme->titlebar_btn_bg;
                        blt_fill_rect(comp->backbuffer, comp->pitch_px, &vis_btn, btn_bg);
                        int32_t py = btns.min_btn.y + btn_glyph_end;
                        if (py >= dirty->y && py < dirty->y + dirty->h) {
                            for (int k = btn_glyph_pad; k <= btn_glyph_end; k++) {
                                int32_t px = btns.min_btn.x + k;
                                if (px >= dirty->x && px < dirty->x + dirty->w)
                                    comp->backbuffer[py * comp->pitch_px + px] = g_theme->titlebar_btn_icon;
                            }
                        }
                    }

                    /* Vector text title rendering */
                    if (win->title[0] != '\0' && comp->font.info) {
                        vanilla_rect_t text_clip;
                        text_clip.x = frame_rect.x + (g_theme->titlebar_height - g_theme->btn_size) / 2;
                        text_clip.y = frame_rect.y;
                        text_clip.w = btns.min_btn.x - text_clip.x - g_theme->space_1;
                        text_clip.h = g_theme->titlebar_height;

                        vanilla_rect_t vis_text;
                        if (vanilla_rect_intersect(&text_clip, dirty, &vis_text) && text_clip.w > 0) {
                            uint32_t text_color = win->is_focused ? g_theme->titlebar_text_active : g_theme->titlebar_text_inactive;
                            if (win->anim_state != ANIM_IDLE && win->anim_state != ANIM_FOCUS &&
                                g_theme->reduce_motion == 0 && win->anim_current_alpha < 1.0f) {
                                uint32_t a = (uint32_t)(((text_color >> 24) & 0xFF) * win->anim_current_alpha);
                                text_color = (a << 24) | (text_color & 0x00FFFFFF);
                            }
                            int32_t text_y = frame_rect.y + (g_theme->titlebar_height - (int32_t)g_theme->title_font_size) / 2;
                            font_draw_text(comp->backbuffer, comp->pitch_px, &vis_text,
                                           &comp->font, win->title, text_clip.x, text_y,
                                           g_theme->title_font_size, text_color);
                        }
                    }
                }

                uint32_t border_color = win->is_focused ? g_theme->border_focus : g_theme->border;
                int32_t content_y = frame_rect.y + g_theme->titlebar_height + g_theme->border_width;
                int32_t content_h = frame_rect.h - (g_theme->titlebar_height + g_theme->border_width);

                vanilla_rect_t b_left = { frame_rect.x, content_y, g_theme->border_width, content_h };
                vanilla_rect_t vis_b;
                if (vanilla_rect_intersect(&b_left, dirty, &vis_b))
                    blt_fill_rect(comp->backbuffer, comp->pitch_px, &vis_b, border_color);

                vanilla_rect_t b_right = { frame_rect.x + frame_rect.w - g_theme->border_width, content_y, g_theme->border_width, content_h };
                if (vanilla_rect_intersect(&b_right, dirty, &vis_b))
                    blt_fill_rect(comp->backbuffer, comp->pitch_px, &vis_b, border_color);

                vanilla_rect_t b_bottom = { frame_rect.x, frame_rect.y + frame_rect.h - g_theme->border_width, frame_rect.w, g_theme->border_width };
                if (vanilla_rect_intersect(&b_bottom, dirty, &vis_b))
                    blt_fill_rect(comp->backbuffer, comp->pitch_px, &vis_b, border_color);
                if (win->has_keyboard_focus && g_theme->reduce_motion == 0) {
                    int32_t ring_w = THEME_PX(2);
                    vanilla_rect_t r_top   = { frame_rect.x, frame_rect.y, frame_rect.w, ring_w };
                    vanilla_rect_t r_bot   = { frame_rect.x, frame_rect.y + frame_rect.h - ring_w, frame_rect.w, ring_w };
                    vanilla_rect_t r_left  = { frame_rect.x, frame_rect.y, ring_w, frame_rect.h };
                    vanilla_rect_t r_right = { frame_rect.x + frame_rect.w - ring_w, frame_rect.y, ring_w, frame_rect.h };
                    vanilla_rect_t vis_r;
                    if (vanilla_rect_intersect(&r_top, dirty, &vis_r))
                        blt_fill_rect(comp->backbuffer, comp->pitch_px, &vis_r, g_theme->accent);
                    if (vanilla_rect_intersect(&r_bot, dirty, &vis_r))
                        blt_fill_rect(comp->backbuffer, comp->pitch_px, &vis_r, g_theme->accent);
                    if (vanilla_rect_intersect(&r_left, dirty, &vis_r))
                        blt_fill_rect(comp->backbuffer, comp->pitch_px, &vis_r, g_theme->accent);
                    if (vanilla_rect_intersect(&r_right, dirty, &vis_r))
                        blt_fill_rect(comp->backbuffer, comp->pitch_px, &vis_r, g_theme->accent);
                }
            }

            /* Render client SHM surface */
            if (win->surface.pixels && win->width > 0 && win->height > 0) {
                if (win->anim_state != ANIM_IDLE && win->anim_state != ANIM_FOCUS && g_theme->reduce_motion == 0) {
                    compositor_paint_animated_window(comp, win, dirty);
                } else {
                    vanilla_rect_t client_rect;
                    client_rect.x = win->x;
                    client_rect.y = win->y;
                    client_rect.w = (int32_t)win->width;
                    client_rect.h = (int32_t)win->height;

                    vanilla_rect_t vis_client;
                    if (vanilla_rect_intersect(&client_rect, dirty, &vis_client)) {
                        /* Fill client background for areas where surface is smaller than window container */
                        blt_fill_rect(comp->backbuffer, comp->pitch_px, &vis_client, g_theme->bg_base);

                        /* Intersect visible client rectangle with actual allocated surface bounds */
                        int32_t surf_w = (int32_t)win->surface.width;
                        int32_t surf_h = (int32_t)win->surface.height;
                        if (surf_w > 0 && surf_h > 0) {
                            vanilla_rect_t surf_rect = { win->x, win->y, surf_w, surf_h };
                            vanilla_rect_t vis_surf;
                            if (vanilla_rect_intersect(&surf_rect, &vis_client, &vis_surf)) {
                                int32_t src_x = vis_surf.x - win->x;
                                int32_t src_y = vis_surf.y - win->y;

                                uint32_t surf_pitch_px = win->surface.pitch / sizeof(uint32_t);
                                if (surf_pitch_px == 0)
                                    surf_pitch_px = (uint32_t)surf_w;

                                blt_blend_subrect(comp->backbuffer, comp->pitch_px,
                                                  vis_surf.x, vis_surf.y,
                                                  win->surface.pixels, surf_pitch_px,
                                                  src_x, src_y,
                                                  vis_surf.w, vis_surf.h, 255);
                            }
                        }
                    }
                }
            }
        }

        /* 6. Render Alt+Tab overlay (if active) */
        if (srv->alttab_visible)
            alttab_render(srv, dirty);

        /* 6.5 Render Snap preview overlay (if active and not reduce_motion) */
        if (comp->snap_preview_visible && g_theme->reduce_motion == 0 && comp->snap_preview_alpha > 0.0f) {
            vanilla_rect_t vis_preview;
            if (vanilla_rect_intersect(&comp->snap_preview_rect, dirty, &vis_preview)) {
                uint8_t alpha = (uint8_t)(comp->snap_preview_alpha * 0.40f * 255.0f);
                if (alpha > 0) {
                    uint32_t fill_buf[64];
                    uint32_t fill_color = 0xFF000000 | (g_theme->accent & 0x00FFFFFF);
                    for (int k = 0; k < 64; k++)
                        fill_buf[k] = fill_color;

                    int32_t rem_w = vis_preview.w;
                    int32_t cur_x = vis_preview.x;
                    while (rem_w > 0) {
                        int32_t chunk_w = rem_w > 64 ? 64 : rem_w;
                        blt_blend_subrect(comp->backbuffer, comp->pitch_px,
                                          cur_x, vis_preview.y,
                                          fill_buf, 0, 0, 0,
                                          chunk_w, vis_preview.h, alpha);
                        cur_x += chunk_w;
                        rem_w -= chunk_w;
                    }
                }
            }
        }

        /* 6.6 Render Server-Managed Popup Context Menus (if active) */
        if (srv->context_menu_depth > 0)
            compositor_paint_context_menus(srv, dirty);

        /* 7. Render Hardware Cursor Overlay */
        cursor_render(comp->backbuffer, comp->pitch_px, comp->width, comp->height,
                      srv->cursor_x, srv->cursor_y, dirty);

        /* 8. Copy composited region to mapped framebuffer */
        if (comp->fb_mem && !comp->is_offscreen) {
            blt_copy_subrect((uint32_t *)comp->fb_mem, comp->pitch_px,
                             dirty->x, dirty->y,
                             comp->backbuffer, comp->pitch_px,
                             dirty->x, dirty->y,
                             dirty->w, dirty->h);
        }
    }

    vanilla_server_release_buffers(srv);
    comp->dirty_count = 0;

    /* If animations are still running, invalidate so the next frame is queued */
    if (compositor_has_active_animations(srv)) {
        for (int i = 0; i < VANILLA_MAX_WINDOWS; i++) {
            if (srv->windows[i].in_use && srv->windows[i].anim_state != ANIM_IDLE)
                wm_invalidate_window(srv, &srv->windows[i]);
        }
        if (srv->compositor.snap_preview_visible && srv->compositor.snap_preview_alpha < 1.0f)
            compositor_add_damage(&srv->compositor, &srv->compositor.snap_preview_rect);
        for (int d = 0; d < srv->context_menu_depth; d++) {
            if (srv->context_menu_stack[d].active && srv->context_menu_stack[d].anim_alpha < 1.0f) {
                int32_t sr = (g_theme && g_theme->shadow_radius > 0) ? g_theme->shadow_radius : 16;
                vanilla_rect_t mr = {
                    srv->context_menu_stack[d].x - sr,
                    srv->context_menu_stack[d].y - sr,
                    srv->context_menu_stack[d].width + sr * 2,
                    srv->context_menu_stack[d].height + sr * 2
                };
                compositor_add_damage(&srv->compositor, &mr);
            }
        }
    }
}

void alttab_get_rect(vanilla_server_t *srv, vanilla_rect_t *out_rect)
{
    if (!srv || !out_rect)
        return;
    int32_t pw = THEME_PX(360);
    int32_t item_h = THEME_PX(32);
    int32_t ph = THEME_PX(24) + srv->alttab_window_count * item_h;
    if (ph < THEME_PX(64))
        ph = THEME_PX(64);
    if (srv->compositor.height > 0) {
        int32_t max_h = (int32_t)srv->compositor.height - THEME_PX(40);
        if (max_h >= THEME_PX(64) && ph > max_h)
            ph = max_h;
    }
    int32_t px = ((int32_t)srv->compositor.width - pw) / 2;
    int32_t py = ((int32_t)srv->compositor.height - ph) / 2;
    out_rect->x = px;
    out_rect->y = py;
    out_rect->w = pw;
    out_rect->h = ph;
}

void alttab_render(vanilla_server_t *srv, const vanilla_rect_t *dirty)
{
    if (!srv || !srv->alttab_visible || !dirty)
        return;

    vanilla_compositor_t *comp = &srv->compositor;
    vanilla_rect_t overlay;
    alttab_get_rect(srv, &overlay);

    vanilla_rect_t vis_overlay;
    if (!vanilla_rect_intersect(&overlay, dirty, &vis_overlay))
        return;

    /* Background panel with rounded corners */
    blt_rounded_rect_clipped(comp->backbuffer, comp->pitch_px,
                             overlay.x, overlay.y, overlay.w, overlay.h,
                             g_theme->radius_md, g_theme->bg_elevated,
                             BLT_CORNER_ALL, dirty);

    /* 1px border */
    int32_t bw = g_theme->border_width;
    vanilla_rect_t b_top = { overlay.x, overlay.y, overlay.w, bw };
    vanilla_rect_t b_bot = { overlay.x, overlay.y + overlay.h - bw, overlay.w, bw };
    vanilla_rect_t b_l   = { overlay.x, overlay.y, bw, overlay.h };
    vanilla_rect_t b_r   = { overlay.x + overlay.w - bw, overlay.y, bw, overlay.h };
    vanilla_rect_t vis;
    if (vanilla_rect_intersect(&b_top, dirty, &vis))
        blt_fill_rect(comp->backbuffer, comp->pitch_px, &vis, g_theme->border);
    if (vanilla_rect_intersect(&b_bot, dirty, &vis))
        blt_fill_rect(comp->backbuffer, comp->pitch_px, &vis, g_theme->border);
    if (vanilla_rect_intersect(&b_l, dirty, &vis))
        blt_fill_rect(comp->backbuffer, comp->pitch_px, &vis, g_theme->border);
    if (vanilla_rect_intersect(&b_r, dirty, &vis))
        blt_fill_rect(comp->backbuffer, comp->pitch_px, &vis, g_theme->border);

    /* Window list */
    int32_t item_h = THEME_PX(32);
    int32_t start_y = overlay.y + THEME_PX(12);

    for (int i = 0; i < srv->alttab_window_count; i++) {
        uint32_t wid = srv->alttab_window_ids[i];
        vanilla_server_window_t *w = vanilla_server_find_window(srv, wid);
        if (!w || !w->is_mapped)
            continue;

        vanilla_rect_t item_rect = {
            overlay.x + THEME_PX(12),
            start_y + i * item_h,
            overlay.w - THEME_PX(24),
            item_h - THEME_PX(4)
        };

        if (item_rect.y + item_rect.h > overlay.y + overlay.h)
            break;

        vanilla_rect_t vis_item;
        if (!vanilla_rect_intersect(&item_rect, dirty, &vis_item))
            continue;

        int selected = (i == srv->alttab_selection);
        if (selected) {
            blt_fill_rect(comp->backbuffer, comp->pitch_px, &vis_item, g_theme->accent);
        } else {
            blt_fill_rect(comp->backbuffer, comp->pitch_px, &vis_item, g_theme->bg_base);
        }

        if (comp->font.info) {
            const char *title = (w->title[0] != '\0') ? w->title : "Window";
            uint32_t text_color = selected ? g_theme->titlebar_btn_icon : g_theme->fg_primary;
            font_draw_text(comp->backbuffer, comp->pitch_px, &vis_item, &comp->font,
                           title, item_rect.x + THEME_PX(10), item_rect.y + THEME_PX(6),
                           THEME_F(13.0f), text_color);
        }
    }
}

static inline uint32_t color_modulate_alpha(uint32_t color, float alpha)
{
    if (alpha >= 1.0f)
        return color;
    if (alpha <= 0.0f)
        return color & 0x00FFFFFF;
    uint32_t a = (uint32_t)(((color >> 24) & 0xFF) * alpha);
    return (a << 24) | (color & 0x00FFFFFF);
}

void compositor_paint_context_menus(vanilla_server_t *srv, const vanilla_rect_t *dirty)
{
    if (!srv || srv->context_menu_depth <= 0 || !dirty)
        return;

    vanilla_compositor_t *comp = &srv->compositor;

    for (int d = 0; d < srv->context_menu_depth; d++) {
        vanilla_context_menu_t *m = &srv->context_menu_stack[d];
        if (!m->active)
            continue;

        vanilla_rect_t menu_rect = { m->x, m->y, m->width, m->height };

        /* Update animation alpha */
        if (g_theme->reduce_motion != 0) {
            m->anim_alpha = 1.0f;
        } else {
            uint64_t now = pit_ticks();
            uint64_t freq = pit_frequency();
            uint64_t dur_ticks = (freq * 80) / 1000;
            if (dur_ticks == 0)
                dur_ticks = 1;
            uint64_t elapsed = (now >= m->open_ticks) ? (now - m->open_ticks) : 0;
            float t = (float)elapsed / (float)dur_ticks;
            if (t >= 1.0f) {
                m->anim_alpha = 1.0f;
            } else {
                m->anim_alpha = anim_ease_out_quad(t);
            }
        }

        if (m->anim_alpha <= 0.01f && g_theme->reduce_motion == 0)
            continue;

        float alpha = m->anim_alpha;
        uint8_t sh_alpha = (uint8_t)((float)g_theme->shadow_alpha * alpha);
        uint32_t bg_col = color_modulate_alpha(g_theme->bg_elevated, alpha);
        uint32_t border_col = color_modulate_alpha(g_theme->border, alpha);
        uint32_t accent_col = color_modulate_alpha(g_theme->accent, alpha);

        vanilla_rect_t vis_menu;
        if (!vanilla_rect_intersect(&menu_rect, dirty, &vis_menu))
            continue;

        /* Ambient soft drop shadow */
        if (sh_alpha > 0) {
            blt_draw_shadow_cached(comp->backbuffer, comp->pitch_px,
                                   comp->width, comp->height,
                                   &menu_rect, dirty,
                                   g_theme->shadow_radius, sh_alpha);
        }

        /* Elevated background panel */
        blt_rounded_rect_clipped(comp->backbuffer, comp->pitch_px,
                                 menu_rect.x, menu_rect.y, menu_rect.w, menu_rect.h,
                                 g_theme->radius_sm, bg_col,
                                 BLT_CORNER_ALL, dirty);

        /* 1px border */
        int32_t bw = g_theme->border_width;
        if (bw < 1) bw = 1;
        vanilla_rect_t b_top = { menu_rect.x, menu_rect.y, menu_rect.w, bw };
        vanilla_rect_t b_bot = { menu_rect.x, menu_rect.y + menu_rect.h - bw, menu_rect.w, bw };
        vanilla_rect_t b_l   = { menu_rect.x, menu_rect.y, bw, menu_rect.h };
        vanilla_rect_t b_r   = { menu_rect.x + menu_rect.w - bw, menu_rect.y, bw, menu_rect.h };
        blt_rounded_rect_clipped(comp->backbuffer, comp->pitch_px, b_top.x, b_top.y, b_top.w, b_top.h, 0, border_col, 0, dirty);
        blt_rounded_rect_clipped(comp->backbuffer, comp->pitch_px, b_bot.x, b_bot.y, b_bot.w, b_bot.h, 0, border_col, 0, dirty);
        blt_rounded_rect_clipped(comp->backbuffer, comp->pitch_px, b_l.x, b_l.y, b_l.w, b_l.h, 0, border_col, 0, dirty);
        blt_rounded_rect_clipped(comp->backbuffer, comp->pitch_px, b_r.x, b_r.y, b_r.w, b_r.h, 0, border_col, 0, dirty);

        /* Render menu item rows */
        int row_h = 24;
        for (int i = 0; i < m->item_count; i++) {
            vanilla_menu_item_t *it = &m->items[i];
            int32_t iy = menu_rect.y + 2 + i * row_h;
            vanilla_rect_t row_rect = { menu_rect.x + 2, iy, menu_rect.w - 4, row_h };

            if (row_rect.y + row_rect.h > menu_rect.y + menu_rect.h)
                break;

            if (it->flags & MENU_ITEM_SEPARATOR) {
                vanilla_rect_t sep_line = { menu_rect.x + 8, iy + row_h / 2, menu_rect.w - 16, 1 };
                blt_rounded_rect_clipped(comp->backbuffer, comp->pitch_px,
                                         sep_line.x, sep_line.y, sep_line.w, sep_line.h,
                                         0, border_col, 0, dirty);
                continue;
            }

            int is_highlighted = (i == m->highlighted) && (it->flags & MENU_ITEM_ENABLED);
            if (is_highlighted) {
                blt_rounded_rect_clipped(comp->backbuffer, comp->pitch_px,
                                         row_rect.x, row_rect.y, row_rect.w, row_rect.h,
                                         g_theme->radius_sm > 4 ? 4 : g_theme->radius_sm,
                                         accent_col, BLT_CORNER_ALL, dirty);
            }

            if (comp->font.info) {
                uint32_t base_fg = (it->flags & MENU_ITEM_ENABLED) ?
                              (is_highlighted ? g_theme->titlebar_btn_icon : g_theme->fg_primary) :
                              g_theme->fg_muted;
                uint32_t fg = color_modulate_alpha(base_fg, alpha);

                /* Checkmark indicator */
                if (it->flags & MENU_ITEM_CHECKED) {
                    font_draw_text(comp->backbuffer, comp->pitch_px, &vis_menu,
                                   &comp->font, "*", row_rect.x + 4, iy + 4,
                                   THEME_F(13.0f), fg);
                }

                /* Item label */
                font_draw_text(comp->backbuffer, comp->pitch_px, &vis_menu,
                               &comp->font, it->label, row_rect.x + 14, iy + 4,
                               THEME_F(13.0f), fg);

                /* Submenu arrow */
                if (it->submenu_id != 0) {
                    font_draw_text(comp->backbuffer, comp->pitch_px, &vis_menu,
                                   &comp->font, ">", menu_rect.x + menu_rect.w - 16, iy + 4,
                                   THEME_F(13.0f), fg);
                }
            }
        }
    }
}
