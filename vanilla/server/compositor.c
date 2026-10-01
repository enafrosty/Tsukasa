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

#include <fcntl.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <errno.h>
#include <sys/ioctl.h>
#include <sys/mman.h>

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

    r.close_btn.x = right - 18;
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

void compositor_render_frame(struct vanilla_server *srv)
{
    if (!srv || srv->compositor.dirty_count <= 0)
        return;

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
                                       &frame_rect, dirty, g_theme->shadow_radius, g_theme->shadow_alpha);
            }
        }

        /* 3. Render windows in Z-order */
        for (int w = 0; w < win_count; w++) {
            vanilla_server_window_t *win = sorted[w];

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
                    blt_rounded_rect_clipped(comp->backbuffer, comp->pitch_px,
                                             title_rect.x, title_rect.y, title_rect.w, title_rect.h,
                                             g_theme->radius_sm, tb_color,
                                             BLT_CORNER_TOP, dirty);

                    /* Titlebar buttons */
                    chrome_btn_rects_t btns = chrome_metrics(&frame_rect);

                    vanilla_rect_t vis_btn;
                    if (vanilla_rect_intersect(&btns.close_btn, dirty, &vis_btn)) {
                        blt_fill_rect(comp->backbuffer, comp->pitch_px, &vis_btn, g_theme->danger);
                        for (int k = 2; k <= 9; k++) {
                            int32_t px1 = btns.close_btn.x + k;
                            int32_t py1 = btns.close_btn.y + k;
                            int32_t px2 = btns.close_btn.x + (11 - k);
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
                        for (int k = 2; k <= 9; k++) {
                            int32_t top_y = btns.max_btn.y + 2;
                            int32_t bot_y = btns.max_btn.y + 9;
                            int32_t px = btns.max_btn.x + k;
                            if (px >= dirty->x && px < dirty->x + dirty->w) {
                                if (top_y >= dirty->y && top_y < dirty->y + dirty->h)
                                    comp->backbuffer[top_y * comp->pitch_px + px] = g_theme->titlebar_btn_icon;
                                if (bot_y >= dirty->y && bot_y < dirty->y + dirty->h)
                                    comp->backbuffer[bot_y * comp->pitch_px + px] = g_theme->titlebar_btn_icon;
                            }
                            int32_t left_x = btns.max_btn.x + 2;
                            int32_t right_x = btns.max_btn.x + 9;
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
                        int32_t py = btns.min_btn.y + 9;
                        if (py >= dirty->y && py < dirty->y + dirty->h) {
                            for (int k = 2; k <= 9; k++) {
                                int32_t px = btns.min_btn.x + k;
                                if (px >= dirty->x && px < dirty->x + dirty->w)
                                    comp->backbuffer[py * comp->pitch_px + px] = g_theme->titlebar_btn_icon;
                            }
                        }
                    }

                    /* Vector text title rendering */
                    if (win->title[0] != '\0' && comp->font.info) {
                        vanilla_rect_t text_clip;
                        text_clip.x = frame_rect.x + 6;
                        text_clip.y = frame_rect.y;
                        text_clip.w = btns.min_btn.x - text_clip.x - 4;
                        text_clip.h = g_theme->titlebar_height;

                        vanilla_rect_t vis_text;
                        if (vanilla_rect_intersect(&text_clip, dirty, &vis_text) && text_clip.w > 0) {
                            uint32_t text_color = win->is_focused ? g_theme->titlebar_text_active : g_theme->titlebar_text_inactive;
                            int32_t text_y = frame_rect.y + (g_theme->titlebar_height - 13) / 2;
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
            }

            /* Render client SHM surface */
            if (win->surface.pixels && win->width > 0 && win->height > 0) {
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

        /* 4. Render Desktop Shell Taskbar */
        shell_render(srv, dirty);

        /* 5. Render Quick Launcher modal (if active) */
        if (srv->launcher.visible)
            launcher_render(srv, dirty);

        /* 6. Render Hardware Cursor Overlay */
        wm_render_cursor(srv, dirty);

        /* 7. Copy composited region to mapped framebuffer */
        if (comp->fb_mem && !comp->is_offscreen) {
            blt_copy_subrect((uint32_t *)comp->fb_mem, comp->pitch_px,
                             dirty->x, dirty->y,
                             comp->backbuffer, comp->pitch_px,
                             dirty->x, dirty->y,
                             dirty->w, dirty->h);
        }
    }

    comp->dirty_count = 0;
}
