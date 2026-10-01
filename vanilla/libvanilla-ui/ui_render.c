/*
 * Project Tsukasa — Display Server Widget Tree Renderer
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

#include "ui_internal.h"
#include <string.h>

#define MAX_DRAW_COMMANDS 1024

static void emit_tree(ui_widget_t *w, vanilla_draw_cmd_array_t *out)
{
    if (!w || !out)
        return;

    int clip_pushed = 0;
    if (w->layout_elem && w->layout_elem->clip_children && out->count + 2 < out->capacity) {
        vanilla_draw_cmd_t ccmd;
        memset(&ccmd, 0, sizeof(ccmd));
        ccmd.type = VCMD_PUSH_CLIP;
        ccmd.bounds.x = w->layout_elem->computed_x + w->layout_elem->pad_left;
        ccmd.bounds.y = w->layout_elem->computed_y + w->layout_elem->pad_top;
        ccmd.bounds.w = w->layout_elem->computed_w - (w->layout_elem->pad_left + w->layout_elem->pad_right);
        ccmd.bounds.h = w->layout_elem->computed_h - (w->layout_elem->pad_top + w->layout_elem->pad_bottom);
        if (ccmd.bounds.w < 0) ccmd.bounds.w = 0;
        if (ccmd.bounds.h < 0) ccmd.bounds.h = 0;
        out->cmds[out->count++] = ccmd;
        clip_pushed = 1;
    }

    ui_widget_emit_draw(w, out);

    for (ui_widget_t *child = w->first_child; child; child = child->next_sibling)
        emit_tree(child, out);

    if (clip_pushed && out->count < out->capacity) {
        vanilla_draw_cmd_t pop_cmd;
        memset(&pop_cmd, 0, sizeof(pop_cmd));
        pop_cmd.type = VCMD_POP_CLIP;
        out->cmds[out->count++] = pop_cmd;
    }
}

static void clear_dirty(ui_widget_t *w)
{
    if (!w)
        return;

    w->dirty = 0;
    memset(&w->dirty_rect, 0, sizeof(w->dirty_rect));

    for (ui_widget_t *c = w->first_child; c; c = c->next_sibling)
        clear_dirty(c);
}

void ui_render(ui_ctx_t *ctx, ui_widget_t *root,
               vanilla_surface_t *target,
               const vanilla_rect_t *dirty_clip)
{
    if (!ctx || !root || !target || !target->pixels)
        return;

    if (target->width == 0 || target->height == 0)
        return;

    if (ctx->focused_widget && ctx->focused_widget->type == UI_WIDGET_TEXT_FIELD)
        ui_text_field_update_blink(ctx->focused_widget);

    if (!dirty_clip && !root->dirty)
        return;

    if (ctx->layout_ctx && root->layout_elem) {
        vlayout_compute(ctx->layout_ctx, root->layout_elem,
                        0, 0, (int32_t)target->width, (int32_t)target->height);
    }

    vanilla_draw_cmd_t cmd_storage[MAX_DRAW_COMMANDS];
    vanilla_draw_cmd_array_t cmds;
    cmds.cmds = cmd_storage;
    cmds.capacity = MAX_DRAW_COMMANDS;
    cmds.count = 0;

    emit_tree(root, &cmds);

    vanilla_execute_draw_commands(target, &cmds, dirty_clip);

    clear_dirty(root);
}
