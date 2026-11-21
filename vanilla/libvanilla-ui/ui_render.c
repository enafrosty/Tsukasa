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

static void apply_scroll_and_overlay_layout(ui_widget_t *w, int32_t scroll_y_offset)
{
    if (!w || w->hidden)
        return;

    if (w->layout_elem)
        w->layout_elem->computed_y -= scroll_y_offset;

    if (w->type == UI_WIDGET_MODAL_DIALOG &&
        w->modal.input_field &&
        w->modal.input_field->layout_elem) {
        int32_t cx = 0, cy = 0, cw = 0, ch = 0;
        ui_modal_compute_layout(w, &cx, &cy, &cw, &ch);
        vanilla_elem_t *ie = w->modal.input_field->layout_elem;
        ie->computed_x = cx + 16;
        ie->computed_y = cy + 88;
        ie->computed_w = cw - 32;
        ie->computed_h = 28;
    }

    int32_t child_scroll = scroll_y_offset;
    if (w->type == UI_WIDGET_SCROLL_VIEW && w->layout_elem) {
        int32_t eff_ch = ui_scroll_view_effective_content_h(w);
        int32_t vh = w->layout_elem->computed_h;
        int32_t max_scroll = (eff_ch > vh) ? (eff_ch - vh) : 0;
        if (w->scroll_view.scroll_y < 0)
            w->scroll_view.scroll_y = 0;
        if (w->scroll_view.scroll_y > max_scroll)
            w->scroll_view.scroll_y = max_scroll;
        child_scroll += w->scroll_view.scroll_y;
    }

    for (ui_widget_t *c = w->first_child; c; c = c->next_sibling) {
        if (w->type == UI_WIDGET_MODAL_DIALOG && c == w->modal.input_field)
            continue;
        apply_scroll_and_overlay_layout(c, child_scroll);
    }
}

static void emit_tree(ui_widget_t *w, vanilla_draw_cmd_array_t *out)
{
    if (!w || !out || w->hidden)
        return;

    ui_widget_emit_draw(w, out);

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

    for (ui_widget_t *child = w->first_child; child; child = child->next_sibling)
        emit_tree(child, out);

    if (clip_pushed && out->count < out->capacity) {
        vanilla_draw_cmd_t pop_cmd;
        memset(&pop_cmd, 0, sizeof(pop_cmd));
        pop_cmd.type = VCMD_POP_CLIP;
        out->cmds[out->count++] = pop_cmd;
    }

    if (w->type == UI_WIDGET_SCROLL_VIEW)
        ui_widget_emit_scroll_overlay(w, out);
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

    int popup_dirty = (ctx->active_popup && !ctx->active_popup->hidden && ctx->active_popup->dirty);
    int modal_dirty = (ctx->active_modal && !ctx->active_modal->hidden && ctx->active_modal->dirty);

    if (!dirty_clip && !root->dirty && !ctx->overlay_dirty && !popup_dirty && !modal_dirty)
        return;

    if (ctx->layout_ctx && root->layout_elem) {
        vlayout_compute(ctx->layout_ctx, root->layout_elem,
                        0, 0, (int32_t)target->width, (int32_t)target->height);
        apply_scroll_and_overlay_layout(root, 0);
    }

    vanilla_draw_cmd_t cmd_storage[MAX_DRAW_COMMANDS];
    vanilla_draw_cmd_array_t cmds;
    cmds.cmds = cmd_storage;
    cmds.capacity = MAX_DRAW_COMMANDS;
    cmds.count = 0;

    emit_tree(root, &cmds);

    if (ctx->active_popup && !ctx->active_popup->hidden && ctx->active_popup->layout_elem) {
        ui_widget_t *p = ctx->active_popup;
        int32_t pw = (p->layout_elem->w_mode == VSIZE_FIXED) ? p->layout_elem->w_px : 160;
        int32_t ph = (p->layout_elem->h_mode == VSIZE_FIXED) ? p->layout_elem->h_px : 120;
        int32_t px = (p->type == UI_WIDGET_MENU) ? p->menu.float_x : 0;
        int32_t py = (p->type == UI_WIDGET_MENU) ? p->menu.float_y : 0;
        if (px + pw > (int32_t)target->width)
            px = (int32_t)target->width - pw;
        if (py + ph > (int32_t)target->height)
            py = (int32_t)target->height - ph;
        if (px < 0) px = 0;
        if (py < 0) py = 0;
        if (ctx->layout_ctx)
            vlayout_compute(ctx->layout_ctx, p->layout_elem, px, py, pw, ph);
        emit_tree(p, &cmds);
    }

    if (ctx->active_modal && !ctx->active_modal->hidden && ctx->active_modal->layout_elem) {
        ui_widget_t *m = ctx->active_modal;
        if (ctx->layout_ctx) {
            vlayout_compute(ctx->layout_ctx, m->layout_elem,
                            0, 0, (int32_t)target->width, (int32_t)target->height);
            apply_scroll_and_overlay_layout(m, 0);
        }
        emit_tree(m, &cmds);
    }

    vanilla_execute_draw_commands(target, &cmds, dirty_clip);

    clear_dirty(root);
    if (ctx->active_popup)
        clear_dirty(ctx->active_popup);
    if (ctx->active_modal)
        clear_dirty(ctx->active_modal);
    ctx->overlay_dirty = 0;
}
