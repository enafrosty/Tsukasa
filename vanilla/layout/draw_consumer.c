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

void vanilla_execute_draw_commands(vanilla_surface_t *target,
                                   const vanilla_draw_cmd_array_t *cmds,
                                   const vanilla_rect_t *clip)
{
    if (!target || !cmds || !cmds->cmds || cmds->count <= 0)
        return;

    vanilla_rect_t clip_stack[16];
    int clip_depth = 0;
    if (clip)
        clip_stack[clip_depth++] = *clip;

    for (int i = 0; i < cmds->count; i++) {
        const vanilla_draw_cmd_t *c = &cmds->cmds[i];
        const vanilla_rect_t *active_clip = clip_depth ? &clip_stack[clip_depth - 1] : NULL;

        if (active_clip) {
            int32_t x1 = c->bounds.x > active_clip->x ? c->bounds.x : active_clip->x;
            int32_t y1 = c->bounds.y > active_clip->y ? c->bounds.y : active_clip->y;
            int32_t x2 = (c->bounds.x + c->bounds.w) < (active_clip->x + active_clip->w) ?
                         (c->bounds.x + c->bounds.w) : (active_clip->x + active_clip->w);
            int32_t y2 = (c->bounds.y + c->bounds.h) < (active_clip->y + active_clip->h) ?
                         (c->bounds.y + c->bounds.h) : (active_clip->y + active_clip->h);
            if (c->type != VCMD_PUSH_CLIP && c->type != VCMD_POP_CLIP) {
                if (x2 <= x1 || y2 <= y1)
                    continue;
            }
        }

        switch (c->type) {
        case VCMD_NONE:
            break;

        case VCMD_FILL_RECT:
            app_fill_rect(target, c->bounds.x, c->bounds.y,
                          c->bounds.w, c->bounds.h, c->fill_rect.color);
            break;

        case VCMD_ROUNDED_RECT:
            /* blt_rounded_rect added by P1-3; stub as fill_rect until then */
            app_fill_rect(target, c->bounds.x, c->bounds.y,
                          c->bounds.w, c->bounds.h, c->rounded_rect.color);
            break;

        case VCMD_BORDER:
            app_draw_rect(target, c->bounds.x, c->bounds.y,
                          c->bounds.w, c->bounds.h, c->border.color);
            break;

        case VCMD_TEXT: {
            int scale = (int32_t)c->text.font_size / 8;
            if (scale < 1)
                scale = 1;
            app_draw_text_scale(target, c->bounds.x, c->bounds.y,
                                c->text.text, scale, c->text.color);
            break;
        }

        case VCMD_IMAGE:
            if (c->image.src && c->image.src->pixels) {
                uint32_t dst_pitch = target->pitch ? (target->pitch / sizeof(uint32_t)) : target->width;
                uint32_t src_pitch = c->image.src->pitch ? (c->image.src->pitch / sizeof(uint32_t)) : c->image.src->width;
                blt_copy_subrect(target->pixels, dst_pitch,
                                 c->bounds.x, c->bounds.y,
                                 c->image.src->pixels, src_pitch,
                                 c->image.src_rect.x, c->image.src_rect.y,
                                 c->bounds.w, c->bounds.h);
            }
            break;

        case VCMD_PUSH_CLIP:
            if (clip_depth < 16) {
                vanilla_rect_t new_clip = { c->bounds.x, c->bounds.y, c->bounds.w, c->bounds.h };
                if (clip_depth > 0) {
                    vanilla_rect_t parent_clip = clip_stack[clip_depth - 1];
                    int32_t x1 = new_clip.x > parent_clip.x ? new_clip.x : parent_clip.x;
                    int32_t y1 = new_clip.y > parent_clip.y ? new_clip.y : parent_clip.y;
                    int32_t x2 = (new_clip.x + new_clip.w) < (parent_clip.x + parent_clip.w) ?
                                 (new_clip.x + new_clip.w) : (parent_clip.x + parent_clip.w);
                    int32_t y2 = (new_clip.y + new_clip.h) < (parent_clip.y + parent_clip.h) ?
                                 (new_clip.y + new_clip.h) : (parent_clip.y + parent_clip.h);
                    if (x2 <= x1 || y2 <= y1) {
                        new_clip.x = 0;
                        new_clip.y = 0;
                        new_clip.w = 0;
                        new_clip.h = 0;
                    } else {
                        new_clip.x = x1;
                        new_clip.y = y1;
                        new_clip.w = x2 - x1;
                        new_clip.h = y2 - y1;
                    }
                }
                clip_stack[clip_depth++] = new_clip;
            }
            break;

        case VCMD_POP_CLIP:
            if (clip_depth > (clip ? 1 : 0))
                clip_depth--;
            break;
        }
    }
}
