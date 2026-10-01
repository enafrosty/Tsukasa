/*
 * Project Tsukasa — Display Server Flex Layout Engine
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

#include "../include/layout.h"
#include <string.h>

struct vanilla_layout_t {
    uint8_t *arena_base;
    size_t   arena_size;
    size_t   offset;
};

static inline size_t align_up(size_t val, size_t align)
{
    return (val + align - 1) & ~(align - 1);
}

vanilla_layout_t *vlayout_init(void *arena, size_t arena_size)
{
    if (!arena || arena_size < sizeof(vanilla_layout_t) + sizeof(vanilla_elem_t))
        return NULL;

    vanilla_layout_t *ctx = (vanilla_layout_t *)arena;
    ctx->arena_base = (uint8_t *)arena;
    ctx->arena_size = arena_size;
    ctx->offset = align_up(sizeof(vanilla_layout_t), 8);
    return ctx;
}

void vlayout_reset(vanilla_layout_t *ctx)
{
    if (!ctx)
        return;

    ctx->offset = align_up(sizeof(vanilla_layout_t), 8);
}

vanilla_elem_t *vlayout_alloc_elem(vanilla_layout_t *ctx)
{
    if (!ctx)
        return NULL;

    size_t cur = align_up(ctx->offset, 8);
    size_t next = cur + sizeof(vanilla_elem_t);
    if (next > ctx->arena_size)
        return NULL;

    vanilla_elem_t *elem = (vanilla_elem_t *)(ctx->arena_base + cur);
    ctx->offset = next;
    memset(elem, 0, sizeof(*elem));
    return elem;
}

vanilla_elem_t *vlayout_box(vanilla_layout_t *ctx)
{
    vanilla_elem_t *elem = vlayout_alloc_elem(ctx);
    if (!elem)
        return NULL;

    elem->type = VELEM_BOX;
    elem->w_mode = VSIZE_GROW;
    elem->h_mode = VSIZE_GROW;
    elem->direction = VDIR_COLUMN;
    elem->align_items = VALIGN_START;
    return elem;
}

vanilla_elem_t *vlayout_text(vanilla_layout_t *ctx, const char *text, uint32_t color, float font_size)
{
    vanilla_elem_t *elem = vlayout_alloc_elem(ctx);
    if (!elem)
        return NULL;

    elem->type = VELEM_TEXT;
    elem->w_mode = VSIZE_FIT;
    elem->h_mode = VSIZE_FIT;
    elem->text = text;
    elem->text_color = color;
    elem->font_size = font_size;
    return elem;
}

vanilla_elem_t *vlayout_image(vanilla_layout_t *ctx, vanilla_surface_t *src, vanilla_draw_rect_t src_rect)
{
    vanilla_elem_t *elem = vlayout_alloc_elem(ctx);
    if (!elem)
        return NULL;

    elem->type = VELEM_IMAGE;
    elem->w_mode = VSIZE_FIT;
    elem->h_mode = VSIZE_FIT;
    elem->image_src = src;
    elem->image_src_rect = src_rect;
    return elem;
}

void vlayout_add_child(vanilla_elem_t *parent, vanilla_elem_t *child)
{
    if (!parent || !child)
        return;

    child->next = NULL;
    if (!parent->children) {
        parent->children = child;
    } else {
        vanilla_elem_t *cur = parent->children;
        while (cur->next)
            cur = cur->next;
        cur->next = child;
    }
}

/* Pass 1: Measure intrinsic/fit sizes */
static void vlayout_measure(vanilla_elem_t *elem)
{
    if (!elem)
        return;

    for (vanilla_elem_t *child = elem->children; child; child = child->next)
        vlayout_measure(child);

    switch (elem->type) {
    case VELEM_TEXT: {
        int scale = (int32_t)elem->font_size / 8;
        if (scale < 1)
            scale = 1;
        int char_w = 8 * scale;
        int line_h = 8 * scale;
        int max_len = 0;
        int cur_len = 0;
        int lines = 0;

        if (elem->text && elem->text[0] != '\0') {
            lines = 1;
            const char *p = elem->text;
            while (*p) {
                if (*p == '\n') {
                    lines++;
                    if (cur_len > max_len)
                        max_len = cur_len;
                    cur_len = 0;
                } else {
                    cur_len++;
                }
                p++;
            }
            if (cur_len > max_len)
                max_len = cur_len;
        }

        elem->computed_w = max_len * char_w;
        elem->computed_h = lines * line_h + (lines > 1 ? (lines - 1) * 2 : 0);
        break;
    }

    case VELEM_IMAGE:
        if (elem->image_src_rect.w > 0 && elem->image_src_rect.h > 0) {
            elem->computed_w = elem->image_src_rect.w;
            elem->computed_h = elem->image_src_rect.h;
        } else if (elem->image_src) {
            elem->computed_w = (int32_t)elem->image_src->width;
            elem->computed_h = (int32_t)elem->image_src->height;
        } else {
            elem->computed_w = 0;
            elem->computed_h = 0;
        }
        break;

    case VELEM_SPACER:
        elem->computed_w = 0;
        elem->computed_h = 0;
        break;

    case VELEM_BOX: {
        int32_t content_w = 0;
        int32_t content_h = 0;
        int child_count = 0;

        if (elem->direction == VDIR_ROW) {
            for (vanilla_elem_t *c = elem->children; c; c = c->next) {
                child_count++;
                int32_t cw = (c->w_mode == VSIZE_FIXED) ? c->w_px : c->computed_w;
                int32_t ch = (c->h_mode == VSIZE_FIXED) ? c->h_px : c->computed_h;
                content_w += cw;
                if (ch > content_h)
                    content_h = ch;
            }
            if (child_count > 1)
                content_w += (child_count - 1) * elem->gap;
        } else {
            for (vanilla_elem_t *c = elem->children; c; c = c->next) {
                child_count++;
                int32_t cw = (c->w_mode == VSIZE_FIXED) ? c->w_px : c->computed_w;
                int32_t ch = (c->h_mode == VSIZE_FIXED) ? c->h_px : c->computed_h;
                content_h += ch;
                if (cw > content_w)
                    content_w = cw;
            }
            if (child_count > 1)
                content_h += (child_count - 1) * elem->gap;
        }

        content_w += elem->pad_left + elem->pad_right;
        content_h += elem->pad_top + elem->pad_bottom;
        elem->computed_w = content_w;
        elem->computed_h = content_h;
        break;
    }
    }

    if (elem->w_mode == VSIZE_FIXED)
        elem->computed_w = elem->w_px;
    if (elem->h_mode == VSIZE_FIXED)
        elem->computed_h = elem->h_px;

    if (elem->computed_w < 0)
        elem->computed_w = 0;
    if (elem->computed_h < 0)
        elem->computed_h = 0;
}

/* Pass 2: Distribute space and position elements */
static void vlayout_distribute_and_position(vanilla_elem_t *elem)
{
    if (!elem || !elem->children)
        return;

    int32_t inner_x = elem->computed_x + elem->pad_left;
    int32_t inner_y = elem->computed_y + elem->pad_top;
    int32_t inner_w = elem->computed_w - (elem->pad_left + elem->pad_right);
    int32_t inner_h = elem->computed_h - (elem->pad_top + elem->pad_bottom);
    if (inner_w < 0)
        inner_w = 0;
    if (inner_h < 0)
        inner_h = 0;

    int num_children = 0;
    int grow_count = 0;

    if (elem->direction == VDIR_ROW) {
        int32_t non_grow_w = 0;
        for (vanilla_elem_t *c = elem->children; c; c = c->next) {
            num_children++;
            if (c->w_mode == VSIZE_GROW) {
                grow_count++;
            } else if (c->w_mode == VSIZE_FIXED) {
                non_grow_w += c->w_px;
            } else {
                non_grow_w += c->computed_w;
            }
        }

        int32_t total_gaps = (num_children > 1) ? (num_children - 1) * elem->gap : 0;
        int32_t avail = inner_w - (non_grow_w + total_gaps);
        if (avail < 0)
            avail = 0;

        int32_t base_grow = (grow_count > 0) ? (avail / grow_count) : 0;
        int32_t extra_px = (grow_count > 0) ? (avail % grow_count) : 0;

        int32_t cur_x = inner_x;
        int grow_idx = 0;

        for (vanilla_elem_t *c = elem->children; c; c = c->next) {
            int32_t cw = 0;
            if (c->w_mode == VSIZE_GROW) {
                cw = base_grow + (grow_idx < extra_px ? 1 : 0);
                grow_idx++;
            } else if (c->w_mode == VSIZE_FIXED) {
                cw = c->w_px;
            } else {
                cw = c->computed_w;
            }
            if (cw < 0)
                cw = 0;

            int32_t ch = 0;
            if (c->h_mode == VSIZE_GROW) {
                ch = inner_h;
            } else if (c->h_mode == VSIZE_FIXED) {
                ch = c->h_px;
            } else {
                ch = c->computed_h;
            }
            if (ch < 0)
                ch = 0;

            int32_t cy = inner_y;
            if (c->h_mode != VSIZE_GROW) {
                switch (elem->align_items) {
                case VALIGN_START:
                    cy = inner_y;
                    break;
                case VALIGN_CENTER:
                    cy = inner_y + (inner_h - ch) / 2;
                    break;
                case VALIGN_END:
                    cy = inner_y + (inner_h - ch);
                    break;
                case VALIGN_STRETCH:
                    cy = inner_y;
                    ch = inner_h;
                    break;
                }
            }

            c->computed_x = cur_x;
            c->computed_y = cy;
            c->computed_w = cw;
            c->computed_h = ch;

            cur_x += cw + elem->gap;
            vlayout_distribute_and_position(c);
        }
    } else {
        int32_t non_grow_h = 0;
        for (vanilla_elem_t *c = elem->children; c; c = c->next) {
            num_children++;
            if (c->h_mode == VSIZE_GROW) {
                grow_count++;
            } else if (c->h_mode == VSIZE_FIXED) {
                non_grow_h += c->h_px;
            } else {
                non_grow_h += c->computed_h;
            }
        }

        int32_t total_gaps = (num_children > 1) ? (num_children - 1) * elem->gap : 0;
        int32_t avail = inner_h - (non_grow_h + total_gaps);
        if (avail < 0)
            avail = 0;

        int32_t base_grow = (grow_count > 0) ? (avail / grow_count) : 0;
        int32_t extra_px = (grow_count > 0) ? (avail % grow_count) : 0;

        int32_t cur_y = inner_y;
        int grow_idx = 0;

        for (vanilla_elem_t *c = elem->children; c; c = c->next) {
            int32_t ch = 0;
            if (c->h_mode == VSIZE_GROW) {
                ch = base_grow + (grow_idx < extra_px ? 1 : 0);
                grow_idx++;
            } else if (c->h_mode == VSIZE_FIXED) {
                ch = c->h_px;
            } else {
                ch = c->computed_h;
            }
            if (ch < 0)
                ch = 0;

            int32_t cw = 0;
            if (c->w_mode == VSIZE_GROW) {
                cw = inner_w;
            } else if (c->w_mode == VSIZE_FIXED) {
                cw = c->w_px;
            } else {
                cw = c->computed_w;
            }
            if (cw < 0)
                cw = 0;

            int32_t cx = inner_x;
            if (c->w_mode != VSIZE_GROW) {
                switch (elem->align_items) {
                case VALIGN_START:
                    cx = inner_x;
                    break;
                case VALIGN_CENTER:
                    cx = inner_x + (inner_w - cw) / 2;
                    break;
                case VALIGN_END:
                    cx = inner_x + (inner_w - cw);
                    break;
                case VALIGN_STRETCH:
                    cx = inner_x;
                    cw = inner_w;
                    break;
                }
            }

            c->computed_x = cx;
            c->computed_y = cur_y;
            c->computed_w = cw;
            c->computed_h = ch;

            cur_y += ch + elem->gap;
            vlayout_distribute_and_position(c);
        }
    }
}

void vlayout_compute(vanilla_layout_t *ctx, vanilla_elem_t *root,
                     int32_t bound_x, int32_t bound_y,
                     int32_t bound_w, int32_t bound_h)
{
    (void)ctx;
    if (!root)
        return;

    if (bound_w < 0)
        bound_w = 0;
    if (bound_h < 0)
        bound_h = 0;

    vlayout_measure(root);

    root->computed_x = bound_x;
    root->computed_y = bound_y;
    root->computed_w = (root->w_mode == VSIZE_FIXED) ? root->w_px :
                       (root->w_mode == VSIZE_FIT)   ? root->computed_w : bound_w;
    root->computed_h = (root->h_mode == VSIZE_FIXED) ? root->h_px :
                       (root->h_mode == VSIZE_FIT)   ? root->computed_h : bound_h;

    if (root->computed_w < 0)
        root->computed_w = 0;
    if (root->computed_h < 0)
        root->computed_h = 0;

    vlayout_distribute_and_position(root);
}

static void vlayout_emit_elem(vanilla_elem_t *elem, vanilla_draw_cmd_array_t *out)
{
    if (!elem || !out)
        return;

    switch (elem->type) {
    case VELEM_BOX:
        if (elem->bg_color != 0) {
            if (out->count < out->capacity) {
                vanilla_draw_cmd_t cmd;
                memset(&cmd, 0, sizeof(cmd));
                cmd.bounds.x = elem->computed_x;
                cmd.bounds.y = elem->computed_y;
                cmd.bounds.w = elem->computed_w;
                cmd.bounds.h = elem->computed_h;
                if (elem->corner_radius > 0) {
                    cmd.type = VCMD_ROUNDED_RECT;
                    cmd.rounded_rect.color = elem->bg_color;
                    cmd.rounded_rect.radius = elem->corner_radius;
                } else {
                    cmd.type = VCMD_FILL_RECT;
                    cmd.fill_rect.color = elem->bg_color;
                }
                out->cmds[out->count++] = cmd;
            }
        }

        if (elem->border_width > 0 && elem->border_color != 0) {
            if (out->count < out->capacity) {
                vanilla_draw_cmd_t cmd;
                memset(&cmd, 0, sizeof(cmd));
                cmd.type = VCMD_BORDER;
                cmd.bounds.x = elem->computed_x;
                cmd.bounds.y = elem->computed_y;
                cmd.bounds.w = elem->computed_w;
                cmd.bounds.h = elem->computed_h;
                cmd.border.color = elem->border_color;
                cmd.border.width = elem->border_width;
                out->cmds[out->count++] = cmd;
            }
        }

        for (vanilla_elem_t *c = elem->children; c; c = c->next)
            vlayout_emit_elem(c, out);
        break;

    case VELEM_TEXT:
        if (elem->text && elem->text[0] != '\0') {
            if (out->count < out->capacity) {
                vanilla_draw_cmd_t cmd;
                memset(&cmd, 0, sizeof(cmd));
                cmd.type = VCMD_TEXT;
                cmd.bounds.x = elem->computed_x;
                cmd.bounds.y = elem->computed_y;
                cmd.bounds.w = elem->computed_w;
                cmd.bounds.h = elem->computed_h;
                cmd.text.text = elem->text;
                cmd.text.color = elem->text_color;
                cmd.text.font_size = elem->font_size;
                cmd.text.font_id = 0;
                out->cmds[out->count++] = cmd;
            }
        }
        break;

    case VELEM_IMAGE:
        if (elem->image_src) {
            if (out->count < out->capacity) {
                vanilla_draw_cmd_t cmd;
                memset(&cmd, 0, sizeof(cmd));
                cmd.type = VCMD_IMAGE;
                cmd.bounds.x = elem->computed_x;
                cmd.bounds.y = elem->computed_y;
                cmd.bounds.w = elem->computed_w;
                cmd.bounds.h = elem->computed_h;
                cmd.image.src = elem->image_src;
                cmd.image.src_rect = elem->image_src_rect;
                out->cmds[out->count++] = cmd;
            }
        }
        break;

    case VELEM_SPACER:
        break;
    }
}

void vlayout_emit(vanilla_layout_t *ctx, vanilla_elem_t *root,
                  vanilla_draw_cmd_array_t *out)
{
    if (!root || !out)
        return;

    if (!out->cmds && out->capacity == 0 && ctx) {
        size_t cur = align_up(ctx->offset, 8);
        size_t rem = (cur < ctx->arena_size) ? (ctx->arena_size - cur) : 0;
        int32_t cap = (int32_t)(rem / sizeof(vanilla_draw_cmd_t));
        if (cap > 0) {
            out->cmds = (vanilla_draw_cmd_t *)(ctx->arena_base + cur);
            out->capacity = cap;
            out->count = 0;
            ctx->offset = cur + (size_t)cap * sizeof(vanilla_draw_cmd_t);
        }
    }

    vlayout_emit_elem(root, out);
}
