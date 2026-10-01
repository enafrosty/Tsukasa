/*
 * Project Tsukasa — Display Server Draw Command Consumer
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

#include "../include/draw_cmd.h"
#include "../apps/app_common.h"
#include "../server/blitter.h"
#include <string.h>

static inline void fill_clipped(vanilla_surface_t *surf, const vanilla_rect_t *clip,
                                int32_t x, int32_t y, int32_t w, int32_t h, uint32_t color)
{
    if (!surf || w <= 0 || h <= 0)
        return;

    int32_t x1 = x > clip->x ? x : clip->x;
    int32_t y1 = y > clip->y ? y : clip->y;
    int32_t x2 = (x + w) < (clip->x + clip->w) ? (x + w) : (clip->x + clip->w);
    int32_t y2 = (y + h) < (clip->y + clip->h) ? (y + h) : (clip->y + clip->h);

    if (x2 > x1 && y2 > y1)
        app_fill_rect(surf, x1, y1, x2 - x1, y2 - y1, color);
}

void vanilla_execute_draw_commands(vanilla_surface_t *target,
                                   const vanilla_draw_cmd_array_t *cmds,
                                   const vanilla_rect_t *clip)
{
    if (!target || !target->pixels || !cmds || !cmds->cmds || cmds->count <= 0)
        return;

    int32_t surf_w = (int32_t)target->width;
    int32_t surf_h = (int32_t)target->height;
    if (surf_w <= 0 || surf_h <= 0)
        return;

    vanilla_rect_t clip_stack[16];
    int clip_depth = 0;
    int clip_overflow = 0;

    vanilla_rect_t base_clip = { 0, 0, surf_w, surf_h };
    if (clip) {
        int32_t x1 = clip->x > 0 ? clip->x : 0;
        int32_t y1 = clip->y > 0 ? clip->y : 0;
        int32_t x2 = (clip->x + clip->w) < surf_w ? (clip->x + clip->w) : surf_w;
        int32_t y2 = (clip->y + clip->h) < surf_h ? (clip->y + clip->h) : surf_h;
        if (x2 > x1 && y2 > y1) {
            base_clip.x = x1;
            base_clip.y = y1;
            base_clip.w = x2 - x1;
            base_clip.h = y2 - y1;
        } else {
            base_clip.x = 0;
            base_clip.y = 0;
            base_clip.w = 0;
            base_clip.h = 0;
        }
    }
    clip_stack[clip_depth++] = base_clip;

    for (int i = 0; i < cmds->count; i++) {
        const vanilla_draw_cmd_t *c = &cmds->cmds[i];
        const vanilla_rect_t *active_clip = &clip_stack[clip_depth - 1];

        if (c->type != VCMD_PUSH_CLIP && c->type != VCMD_POP_CLIP && c->type != VCMD_NONE) {
            int32_t x1 = c->bounds.x > active_clip->x ? c->bounds.x : active_clip->x;
            int32_t y1 = c->bounds.y > active_clip->y ? c->bounds.y : active_clip->y;
            int32_t x2 = (c->bounds.x + c->bounds.w) < (active_clip->x + active_clip->w) ?
                         (c->bounds.x + c->bounds.w) : (active_clip->x + active_clip->w);
            int32_t y2 = (c->bounds.y + c->bounds.h) < (active_clip->y + active_clip->h) ?
                         (c->bounds.y + c->bounds.h) : (active_clip->y + active_clip->h);
            if (x2 <= x1 || y2 <= y1)
                continue;
        }

        switch (c->type) {
        case VCMD_NONE:
            break;

        case VCMD_FILL_RECT:
            fill_clipped(target, active_clip, c->bounds.x, c->bounds.y,
                         c->bounds.w, c->bounds.h, c->fill_rect.color);
            break;

        case VCMD_ROUNDED_RECT:
            blt_rounded_rect_clipped(target->pixels, target->width,
                                     c->bounds.x, c->bounds.y, c->bounds.w, c->bounds.h,
                                     c->rounded_rect.radius, c->rounded_rect.color,
                                     BLT_CORNER_ALL, active_clip);
            break;

        case VCMD_BORDER: {
            int32_t bw = c->border.width > 0 ? c->border.width : 1;
            if (bw * 2 >= c->bounds.w || bw * 2 >= c->bounds.h) {
                fill_clipped(target, active_clip, c->bounds.x, c->bounds.y,
                             c->bounds.w, c->bounds.h, c->border.color);
            } else {
                fill_clipped(target, active_clip, c->bounds.x, c->bounds.y,
                             c->bounds.w, bw, c->border.color);
                fill_clipped(target, active_clip, c->bounds.x, c->bounds.y + c->bounds.h - bw,
                             c->bounds.w, bw, c->border.color);
                fill_clipped(target, active_clip, c->bounds.x, c->bounds.y + bw,
                             bw, c->bounds.h - 2 * bw, c->border.color);
                fill_clipped(target, active_clip, c->bounds.x + c->bounds.w - bw, c->bounds.y + bw,
                             bw, c->bounds.h - 2 * bw, c->border.color);
            }
            break;
        }

        case VCMD_TEXT: {
            if (!c->text.text)
                break;
            int scale = (int32_t)c->text.font_size / 8;
            if (scale < 1)
                scale = 1;
            app_draw_text_scale(target, c->bounds.x, c->bounds.y,
                                c->text.text, scale, c->text.color);
            break;
        }

        case VCMD_IMAGE:
            if (c->image.src && c->image.src->pixels) {
                int32_t sx = c->image.src_rect.w > 0 ? c->image.src_rect.x : 0;
                int32_t sy = c->image.src_rect.h > 0 ? c->image.src_rect.y : 0;
                int32_t src_max_w = (int32_t)c->image.src->width;
                int32_t src_max_h = (int32_t)c->image.src->height;

                int32_t dx1 = c->bounds.x > active_clip->x ? c->bounds.x : active_clip->x;
                int32_t dy1 = c->bounds.y > active_clip->y ? c->bounds.y : active_clip->y;
                int32_t dx2 = (c->bounds.x + c->bounds.w) < (active_clip->x + active_clip->w) ?
                              (c->bounds.x + c->bounds.w) : (active_clip->x + active_clip->w);
                int32_t dy2 = (c->bounds.y + c->bounds.h) < (active_clip->y + active_clip->h) ?
                              (c->bounds.y + c->bounds.h) : (active_clip->y + active_clip->h);

                int32_t off_x = dx1 - c->bounds.x;
                int32_t off_y = dy1 - c->bounds.y;
                int32_t cur_src_x = sx + off_x;
                int32_t cur_src_y = sy + off_y;
                int32_t cur_w = dx2 - dx1;
                int32_t cur_h = dy2 - dy1;

                if (cur_src_x < 0) {
                    cur_w += cur_src_x;
                    dx1 -= cur_src_x;
                    cur_src_x = 0;
                }
                if (cur_src_y < 0) {
                    cur_h += cur_src_y;
                    dy1 -= cur_src_y;
                    cur_src_y = 0;
                }
                int32_t max_src_x = src_max_w;
                if (c->image.src_rect.w > 0 && sx + c->image.src_rect.w < max_src_x)
                    max_src_x = sx + c->image.src_rect.w;

                int32_t max_src_y = src_max_h;
                if (c->image.src_rect.h > 0 && sy + c->image.src_rect.h < max_src_y)
                    max_src_y = sy + c->image.src_rect.h;

                if (cur_src_x + cur_w > max_src_x)
                    cur_w = max_src_x - cur_src_x;
                if (cur_src_y + cur_h > max_src_y)
                    cur_h = max_src_y - cur_src_y;

                if (cur_w > 0 && cur_h > 0) {
                    uint32_t dst_pitch = target->pitch ? (target->pitch / sizeof(uint32_t)) : target->width;
                    uint32_t src_pitch = c->image.src->pitch ? (c->image.src->pitch / sizeof(uint32_t)) : c->image.src->width;
                    blt_copy_subrect(target->pixels, dst_pitch,
                                     dx1, dy1,
                                     c->image.src->pixels, src_pitch,
                                     cur_src_x, cur_src_y,
                                     cur_w, cur_h);
                }
            }
            break;

        case VCMD_PUSH_CLIP:
            if (clip_overflow > 0) {
                clip_overflow++;
            } else if (clip_depth < 16) {
                vanilla_rect_t parent_clip = clip_stack[clip_depth - 1];
                int32_t x1 = c->bounds.x > parent_clip.x ? c->bounds.x : parent_clip.x;
                int32_t y1 = c->bounds.y > parent_clip.y ? c->bounds.y : parent_clip.y;
                int32_t x2 = (c->bounds.x + c->bounds.w) < (parent_clip.x + parent_clip.w) ?
                             (c->bounds.x + c->bounds.w) : (parent_clip.x + parent_clip.w);
                int32_t y2 = (c->bounds.y + c->bounds.h) < (parent_clip.y + parent_clip.h) ?
                             (c->bounds.y + c->bounds.h) : (parent_clip.y + parent_clip.h);
                vanilla_rect_t new_clip;
                if (x2 > x1 && y2 > y1) {
                    new_clip.x = x1;
                    new_clip.y = y1;
                    new_clip.w = x2 - x1;
                    new_clip.h = y2 - y1;
                } else {
                    new_clip.x = 0;
                    new_clip.y = 0;
                    new_clip.w = 0;
                    new_clip.h = 0;
                }
                clip_stack[clip_depth++] = new_clip;
            } else {
                clip_overflow = 1;
            }
            break;

        case VCMD_POP_CLIP:
            if (clip_overflow > 0) {
                clip_overflow--;
            } else if (clip_depth > 1) {
                clip_depth--;
            }
            break;
        }
    }
}
