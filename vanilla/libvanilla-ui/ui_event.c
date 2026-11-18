/*
 * Project Tsukasa — Display Server Widget Event Dispatcher & Focus Chain
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
#include <sys/input.h>

static int scroll_view_has_sb(const ui_widget_t *w, int32_t *out_eff_ch)
{
    if (!w || w->type != UI_WIDGET_SCROLL_VIEW || !w->layout_elem)
        return 0;
    int32_t eff_ch = ui_scroll_view_effective_content_h(w);
    if (out_eff_ch)
        *out_eff_ch = eff_ch;
    int32_t rw = w->layout_elem->computed_w;
    int32_t rh = w->layout_elem->computed_h;
    return (eff_ch > rh && rh > 16 && rw > UI_SCROLLBAR_W + 4);
}

static int list_has_sb(const ui_widget_t *w, int32_t *out_total_h, int32_t *out_inner_h)
{
    if (!w || w->type != UI_WIDGET_LIST || !w->layout_elem)
        return 0;
    int32_t rw = w->layout_elem->computed_w;
    int32_t rh = w->layout_elem->computed_h;
    int32_t inner_h = rh > 2 ? rh - 2 : 0;
    int32_t total_h = w->list.count * UI_LIST_ROW_H;
    if (out_total_h) *out_total_h = total_h;
    if (out_inner_h) *out_inner_h = inner_h;
    return (total_h > inner_h && inner_h > 20 && rw > UI_SCROLLBAR_W + 8);
}

static void sb_press_action(int32_t vx, int32_t vy, int32_t vw, int32_t vh,
                            int32_t content_h, int32_t my,
                            int32_t *scroll_y, int32_t *sb_dragging,
                            int32_t *sb_drag_off)
{
    int32_t tx = 0, ty = 0, tw = 0, th = 0, by = 0, bh = 0, ms = 0;
    ui_scrollbar_metrics(vx, vy, vw, vh, content_h, *scroll_y,
                         &tx, &ty, &tw, &th, &by, &bh, &ms);
    if (my >= by && my < by + bh) {
        *sb_dragging = 1;
        *sb_drag_off = my - by;
    } else {
        int32_t step = (vh > 40) ? (vh - 24) : vh;
        if (my < by)
            *scroll_y -= step;
        else
            *scroll_y += step;
        if (*scroll_y < 0)
            *scroll_y = 0;
        if (*scroll_y > ms)
            *scroll_y = ms;
    }
}

static void sb_drag_action(int32_t vx, int32_t vy, int32_t vw, int32_t vh,
                           int32_t content_h, int32_t my, int32_t drag_off,
                           int32_t *scroll_y)
{
    int32_t tx = 0, ty = 0, tw = 0, th = 0, by = 0, bh = 0, ms = 0;
    ui_scrollbar_metrics(vx, vy, vw, vh, content_h, *scroll_y,
                         &tx, &ty, &tw, &th, &by, &bh, &ms);
    int32_t travel = th - bh;
    if (travel > 0 && ms > 0) {
        int32_t new_by = my - drag_off;
        if (new_by < ty)
            new_by = ty;
        if (new_by > ty + travel)
            new_by = ty + travel;
        *scroll_y = ((new_by - ty) * ms) / travel;
        if (*scroll_y < 0)
            *scroll_y = 0;
        if (*scroll_y > ms)
            *scroll_y = ms;
    }
}

static int32_t modal_hit_button(const ui_widget_t *w, int32_t mx, int32_t my)
{
    if (!w || w->type != UI_WIDGET_MODAL_DIALOG)
        return 0;

    int32_t cx = 0, cy = 0, cw = 0, ch = 0;
    ui_modal_compute_layout(w, &cx, &cy, &cw, &ch);
    int32_t btn_y = cy + ch - 40;
    int32_t ok_x = cx + cw - 96;
    if (mx >= ok_x && mx < ok_x + 80 && my >= btn_y && my < btn_y + 28)
        return 1;

    if (w->modal.kind == UI_DIALOG_CONFIRM || w->modal.kind == UI_DIALOG_INPUT) {
        int32_t cancel_x = cx + cw - 186;
        if (mx >= cancel_x && mx < cancel_x + 80 && my >= btn_y && my < btn_y + 28)
            return 2;
    }
    return 0;
}

static int widget_is_descendant(const ui_widget_t *anc, const ui_widget_t *w)
{
    for (const ui_widget_t *cur = w; cur; cur = cur->parent) {
        if (cur == anc)
            return 1;
    }
    return 0;
}

ui_widget_t *ui_hit_test(ui_widget_t *root, int32_t x, int32_t y)
{
    if (!root || root->hidden || !root->layout_elem)
        return NULL;

    int32_t rx = root->layout_elem->computed_x;
    int32_t ry = root->layout_elem->computed_y;
    int32_t rw = root->layout_elem->computed_w;
    int32_t rh = root->layout_elem->computed_h;

    if (x < rx || x >= rx + rw || y < ry || y >= ry + rh)
        return NULL;

    if (root->type == UI_WIDGET_SCROLL_VIEW && scroll_view_has_sb(root, NULL)) {
        if (x >= rx + rw - UI_SCROLLBAR_W - 2)
            return root;
    }

    int can_descend = 1;
    if (root->layout_elem->clip_children) {
        int32_t ix = rx + root->layout_elem->pad_left;
        int32_t iy = ry + root->layout_elem->pad_top;
        int32_t iw = rw - (root->layout_elem->pad_left + root->layout_elem->pad_right);
        int32_t ih = rh - (root->layout_elem->pad_top + root->layout_elem->pad_bottom);
        if (x < ix || x >= ix + iw || y < iy || y >= iy + ih)
            can_descend = 0;
    }

    ui_widget_t *hit = NULL;
    if (can_descend) {
        for (ui_widget_t *c = root->first_child; c; c = c->next_sibling) {
            ui_widget_t *child_hit = ui_hit_test(c, x, y);
            if (child_hit)
                hit = child_hit;
        }
    }

    if (hit)
        return hit;

    return root;
}

void ui_widget_set_focus(ui_ctx_t *ctx, ui_widget_t *w)
{
    if (!ctx || ctx->focused_widget == w)
        return;

    if (ctx->focused_widget) {
        ui_widget_t *old = ctx->focused_widget;
        old->focused = 0;
        ui_widget_invalidate(old);
        ui_event_t ev;
        memset(&ev, 0, sizeof(ev));
        ev.type = UI_EVENT_FOCUS_LOST;
        ev.source = old;
        if (old->on_event)
            old->on_event(old, &ev, old->userdata);
    }

    ctx->focused_widget = w;

    if (w) {
        w->focused = 1;
        ui_widget_invalidate(w);
        ui_event_t ev;
        memset(&ev, 0, sizeof(ev));
        ev.type = UI_EVENT_FOCUS_GAINED;
        ev.source = w;
        if (w->on_event)
            w->on_event(w, &ev, w->userdata);
    }
}

ui_widget_t *ui_get_focused(ui_ctx_t *ctx)
{
    return ctx ? ctx->focused_widget : NULL;
}

static void collect_focusable(ui_widget_t *w, ui_widget_t **list, int *count, int max_items)
{
    if (!w || w->hidden || *count >= max_items)
        return;

    if (w->layout_elem && w->layout_elem->clip_children &&
        (w->layout_elem->computed_w <= 0 || w->layout_elem->computed_h <= 0) &&
        (w->layout_elem->w_mode == VSIZE_FIXED || w->layout_elem->h_mode == VSIZE_FIXED))
        return;

    if (w->focusable)
        list[(*count)++] = w;

    for (ui_widget_t *c = w->first_child; c; c = c->next_sibling)
        collect_focusable(c, list, count, max_items);
}

void ui_focus_next(ui_ctx_t *ctx, ui_widget_t *root)
{
    if (!ctx || !root)
        return;

    ui_widget_t *list[128];
    int count = 0;
    collect_focusable(root, list, &count, 128);
    if (count == 0)
        return;

    if (!ctx->focused_widget) {
        ui_widget_set_focus(ctx, list[0]);
        return;
    }

    for (int i = 0; i < count; i++) {
        if (list[i] == ctx->focused_widget) {
            ui_widget_set_focus(ctx, list[(i + 1) % count]);
            return;
        }
    }

    ui_widget_set_focus(ctx, list[0]);
}

void ui_focus_prev(ui_ctx_t *ctx, ui_widget_t *root)
{
    if (!ctx || !root)
        return;

    ui_widget_t *list[128];
    int count = 0;
    collect_focusable(root, list, &count, 128);
    if (count == 0)
        return;

    if (!ctx->focused_widget) {
        ui_widget_set_focus(ctx, list[count - 1]);
        return;
    }

    for (int i = 0; i < count; i++) {
        if (list[i] == ctx->focused_widget) {
            ui_widget_set_focus(ctx, list[(i - 1 + count) % count]);
            return;
        }
    }

    ui_widget_set_focus(ctx, list[count - 1]);
}

static void uncheck_radio_group(ui_widget_t *node, int32_t group_id, ui_widget_t *except)
{
    if (!node)
        return;

    if (node->type == UI_WIDGET_RADIO && node != except && node->radio.group_id == group_id) {
        if (node->radio.checked != 0) {
            node->radio.checked = 0;
            ui_widget_invalidate(node);
        }
    }

    for (ui_widget_t *c = node->first_child; c; c = c->next_sibling)
        uncheck_radio_group(c, group_id, except);
}

static void clear_hover_state(ui_widget_t *w)
{
    if (!w)
        return;

    int changed = 0;
    switch (w->type) {
    case UI_WIDGET_BUTTON:
        if (w->button.hovered) {
            w->button.hovered = 0;
            changed = 1;
        }
        break;
    case UI_WIDGET_TAB_BAR:
        if (w->tab_bar.hovered_tab != -1) {
            w->tab_bar.hovered_tab = -1;
            changed = 1;
        }
        break;
    case UI_WIDGET_LIST:
        if (w->list.hovered != -1) {
            w->list.hovered = -1;
            changed = 1;
        }
        break;
    case UI_WIDGET_MENU:
        if (w->menu.hovered != -1) {
            w->menu.hovered = -1;
            changed = 1;
        }
        break;
    case UI_WIDGET_MODAL_DIALOG:
        if (w->modal.hovered_btn != 0) {
            w->modal.hovered_btn = 0;
            changed = 1;
        }
        break;
    default:
        break;
    }
    if (changed)
        ui_widget_invalidate(w);
}

static void update_hover_state(ui_ctx_t *ctx, ui_widget_t *root, int32_t mx, int32_t my)
{
    ui_widget_t *hit = NULL;
    if (ctx->active_modal && !ctx->active_modal->hidden) {
        hit = ui_hit_test(ctx->active_modal, mx, my);
    } else if (ctx->active_popup && !ctx->active_popup->hidden) {
        hit = ui_hit_test(ctx->active_popup, mx, my);
        if (!hit)
            hit = ui_hit_test(root, mx, my);
    } else {
        hit = ui_hit_test(root, mx, my);
    }

    if (ctx->hovered_widget && ctx->hovered_widget != hit)
        clear_hover_state(ctx->hovered_widget);
    ctx->hovered_widget = hit;
    if (!hit || !hit->layout_elem)
        return;

    int32_t bx = hit->layout_elem->computed_x;
    int32_t bw = hit->layout_elem->computed_w;
    int32_t by = hit->layout_elem->computed_y;

    switch (hit->type) {
    case UI_WIDGET_BUTTON:
        if (!hit->button.hovered) {
            hit->button.hovered = 1;
            ui_widget_invalidate(hit);
        }
        break;

    case UI_WIDGET_TAB_BAR: {
        int32_t count = hit->tab_bar.tab_count > 0 ? hit->tab_bar.tab_count : 1;
        int32_t tab_w = bw / count;
        int32_t htab = (tab_w > 0) ? ((mx - bx) / tab_w) : -1;
        if (htab < 0 || htab >= hit->tab_bar.tab_count)
            htab = -1;
        if (hit->tab_bar.hovered_tab != htab) {
            hit->tab_bar.hovered_tab = htab;
            ui_widget_invalidate(hit);
        }
        break;
    }

    case UI_WIDGET_LIST: {
        int32_t hrow = -1;
        if (!(list_has_sb(hit, NULL, NULL) && mx >= bx + bw - UI_SCROLLBAR_W - 2)) {
            hrow = (my - (by + 1) + hit->list.scroll_top) / UI_LIST_ROW_H;
            if (hrow < 0 || hrow >= hit->list.count)
                hrow = -1;
        }
        if (hit->list.hovered != hrow) {
            hit->list.hovered = hrow;
            ui_widget_invalidate(hit);
        }
        break;
    }

    case UI_WIDGET_MENU: {
        int32_t idx = ui_menu_hit_item(hit, mx, my);
        if (!hit->menu.is_bar && idx >= 0 && ui_menu_is_disabled(hit, idx))
            idx = -1;
        if (hit->menu.hovered != idx) {
            hit->menu.hovered = idx;
            ui_widget_invalidate(hit);
        }
        break;
    }

    case UI_WIDGET_MODAL_DIALOG: {
        int32_t hbtn = modal_hit_button(hit, mx, my);
        if (hit->modal.hovered_btn != hbtn) {
            hit->modal.hovered_btn = hbtn;
            ui_widget_invalidate(hit);
        }
        break;
    }

    default:
        break;
    }
}

static void dispatch_modal_result(ui_ctx_t *ctx, ui_widget_t *m, ui_dialog_result_t res,
                                  int32_t mx, int32_t my)
{
    m->modal.result = res;
    ui_widget_invalidate(m);
    if (m->on_event) {
        ui_event_t out_ev;
        memset(&out_ev, 0, sizeof(out_ev));
        out_ev.type = UI_EVENT_CLICK;
        out_ev.source = m;
        out_ev.click.x = mx;
        out_ev.click.y = my;
        m->on_event(m, &out_ev, m->userdata);
    }
    if (ctx->active_modal == m && m->modal.result != UI_DIALOG_RES_NONE)
        ui_ctx_hide_modal(ctx);
}

static void menu_step_hover(ui_widget_t *m, int32_t dir)
{
    if (!m || m->type != UI_WIDGET_MENU || m->menu.count <= 0)
        return;

    int32_t idx = m->menu.hovered;
    if (idx < 0 || idx >= m->menu.count)
        idx = (dir > 0) ? -1 : m->menu.count;

    for (int32_t tries = 0; tries < m->menu.count; tries++) {
        idx = (idx + dir + m->menu.count) % m->menu.count;
        if (!ui_menu_is_disabled(m, idx)) {
            m->menu.hovered = idx;
            ui_widget_invalidate(m);
            return;
        }
    }
}

static void handle_wheel_event(ui_ctx_t *ctx, ui_widget_t *root,
                               const struct input_event *ev)
{
    int32_t wx = (ev->pad1 != 0 || ev->pad2 != 0) ? (int32_t)ev->pad1 : ctx->mouse_x;
    int32_t wy = (ev->pad1 != 0 || ev->pad2 != 0) ? (int32_t)ev->pad2 : ctx->mouse_y;
    if (ev->pad1 != 0 || ev->pad2 != 0) {
        ctx->mouse_x = wx;
        ctx->mouse_y = wy;
    }

    ui_widget_t *hit = NULL;
    if (ctx->active_modal && !ctx->active_modal->hidden)
        hit = ui_hit_test(ctx->active_modal, wx, wy);
    else if (ctx->active_popup && !ctx->active_popup->hidden)
        hit = ui_hit_test(ctx->active_popup, wx, wy);
    if (!hit)
        hit = ui_hit_test(root, wx, wy);

    for (ui_widget_t *cur = hit; cur; cur = cur->parent) {
        if (cur->type == UI_WIDGET_TEXT_FIELD && cur->text_field.multiline) {
            ui_text_field_handle_wheel(cur, ev->value);
            return;
        }
        if (cur->type == UI_WIDGET_LIST) {
            int32_t total_h = 0, inner_h = 0;
            if (list_has_sb(cur, &total_h, &inner_h)) {
                int32_t ms = total_h - inner_h;
                cur->list.scroll_top -= ev->value * (UI_LIST_ROW_H * 2);
                if (cur->list.scroll_top < 0)
                    cur->list.scroll_top = 0;
                if (cur->list.scroll_top > ms)
                    cur->list.scroll_top = ms;
                ui_widget_invalidate(cur);
                return;
            }
        }
        if (cur->type == UI_WIDGET_SCROLL_VIEW) {
            ui_scroll_view_scroll_to(cur, cur->scroll_view.scroll_y - ev->value * 40);
            return;
        }
    }
}

void ui_handle_event(ui_ctx_t *ctx, ui_widget_t *root, const struct input_event *ev)
{
    if (!ctx || !root || !ev)
        return;

    if (ev->type == EV_REL && ev->code == REL_WHEEL) {
        if (ev->value != 0)
            handle_wheel_event(ctx, root, ev);
        return;
    }

    if (ev->type == EV_REL || ev->type == EV_ABS) {
        ctx->mouse_x = (int32_t)ev->pad1;
        ctx->mouse_y = (int32_t)ev->pad2;

        if (ctx->mouse_down && ctx->pressed_widget) {
            ui_widget_t *pw = ctx->pressed_widget;
            if (pw->type == UI_WIDGET_SLIDER) {
                int32_t bx = pw->layout_elem ? pw->layout_elem->computed_x : 0;
                int32_t bw = pw->layout_elem ? pw->layout_elem->computed_w : 1;
                if (bw <= 0)
                    bw = 1;
                float frac = (float)(ctx->mouse_x - bx) / (float)bw;
                if (frac < 0.0f) frac = 0.0f;
                if (frac > 1.0f) frac = 1.0f;
                pw->slider.value = pw->slider.min_val +
                                   frac * (pw->slider.max_val - pw->slider.min_val);
                ui_widget_invalidate(pw);
                ui_event_t out_ev;
                memset(&out_ev, 0, sizeof(out_ev));
                out_ev.type = UI_EVENT_VALUE_CHANGED;
                out_ev.source = pw;
                out_ev.slider.value = pw->slider.value;
                if (pw->on_event)
                    pw->on_event(pw, &out_ev, pw->userdata);
            } else if (pw->type == UI_WIDGET_TEXT_FIELD) {
                ui_text_field_handle_drag(pw, ctx->mouse_x, ctx->mouse_y);
            } else if (pw->type == UI_WIDGET_SCROLL_VIEW && pw->scroll_view.sb_dragging) {
                int32_t eff_ch = 0;
                if (scroll_view_has_sb(pw, &eff_ch)) {
                    int32_t sy = pw->scroll_view.scroll_y;
                    sb_drag_action(pw->layout_elem->computed_x,
                                   pw->layout_elem->computed_y,
                                   pw->layout_elem->computed_w,
                                   pw->layout_elem->computed_h,
                                   eff_ch, ctx->mouse_y,
                                   pw->scroll_view.sb_drag_off, &sy);
                    ui_scroll_view_scroll_to(pw, sy);
                }
            } else if (pw->type == UI_WIDGET_LIST && pw->list.sb_dragging) {
                int32_t total_h = 0, inner_h = 0;
                if (list_has_sb(pw, &total_h, &inner_h)) {
                    sb_drag_action(pw->layout_elem->computed_x + 1,
                                   pw->layout_elem->computed_y + 1,
                                   pw->layout_elem->computed_w - 2,
                                   inner_h,
                                   total_h, ctx->mouse_y,
                                   pw->list.sb_drag_off, &pw->list.scroll_top);
                    ui_widget_invalidate(pw);
                }
            }
        }

        update_hover_state(ctx, root, ctx->mouse_x, ctx->mouse_y);
        return;
    }

    if (ev->type != EV_KEY)
        return;

    if (ev->code == KEY_LEFTSHIFT || ev->code == KEY_RIGHTSHIFT) {
        ctx->shift_down = (ev->value != 0);
        return;
    }
    if (ev->code == KEY_LEFTCTRL || ev->code == 97) {
        ctx->ctrl_down = (ev->value != 0);
        return;
    }
    if (ev->code == KEY_LEFTALT || ev->code == 100) {
        ctx->alt_down = (ev->value != 0);
        return;
    }

    if (ev->code == BTN_RIGHT && ev->value >= 1) {
        int32_t mx = (int32_t)ev->pad1;
        int32_t my = (int32_t)ev->pad2;
        if (ctx->active_popup && !ui_hit_test(ctx->active_popup, mx, my))
            ui_ctx_hide_popup(ctx);
        return;
    }

    if (ev->code == BTN_LEFT) {
        int32_t mx = (int32_t)ev->pad1;
        int32_t my = (int32_t)ev->pad2;
        ctx->mouse_x = mx;
        ctx->mouse_y = my;

        if (ev->value >= 1) {
            ctx->mouse_down = 1;
            ui_widget_t *target = NULL;
            if (ctx->active_modal && !ctx->active_modal->hidden) {
                target = ui_hit_test(ctx->active_modal, mx, my);
            } else if (ctx->active_popup && !ctx->active_popup->hidden) {
                target = ui_hit_test(ctx->active_popup, mx, my);
                if (!target)
                    ui_ctx_hide_popup(ctx);
            }
            if (!target && (!ctx->active_modal || ctx->active_modal->hidden))
                target = ui_hit_test(root, mx, my);

            if (target) {
                if (target->focusable)
                    ui_widget_set_focus(ctx, target);
                ctx->pressed_widget = target;

                switch (target->type) {
                case UI_WIDGET_BUTTON:
                    target->button.pressed = 1;
                    ui_widget_invalidate(target);
                    break;

                case UI_WIDGET_TEXT_FIELD:
                    ui_text_field_handle_click(target, mx, my, ctx->shift_down);
                    break;

                case UI_WIDGET_CHECKBOX: {
                    target->checkbox.checked ^= 1;
                    ui_widget_invalidate(target);
                    ui_event_t out_ev;
                    memset(&out_ev, 0, sizeof(out_ev));
                    out_ev.type = UI_EVENT_VALUE_CHANGED;
                    out_ev.source = target;
                    out_ev.toggle.state = target->checkbox.checked;
                    if (target->on_event)
                        target->on_event(target, &out_ev, target->userdata);
                    break;
                }

                case UI_WIDGET_RADIO: {
                    target->radio.checked = 1;
                    uncheck_radio_group(root, target->radio.group_id, target);
                    ui_widget_invalidate(target);
                    ui_event_t out_ev;
                    memset(&out_ev, 0, sizeof(out_ev));
                    out_ev.type = UI_EVENT_VALUE_CHANGED;
                    out_ev.source = target;
                    out_ev.toggle.state = 1;
                    if (target->on_event)
                        target->on_event(target, &out_ev, target->userdata);
                    break;
                }

                case UI_WIDGET_SLIDER: {
                    int32_t bx = target->layout_elem ? target->layout_elem->computed_x : 0;
                    int32_t bw = target->layout_elem ? target->layout_elem->computed_w : 1;
                    if (bw <= 0)
                        bw = 1;
                    float frac = (float)(mx - bx) / (float)bw;
                    if (frac < 0.0f) frac = 0.0f;
                    if (frac > 1.0f) frac = 1.0f;
                    target->slider.value = target->slider.min_val +
                                           frac * (target->slider.max_val - target->slider.min_val);
                    ui_widget_invalidate(target);
                    ui_event_t out_ev;
                    memset(&out_ev, 0, sizeof(out_ev));
                    out_ev.type = UI_EVENT_VALUE_CHANGED;
                    out_ev.source = target;
                    out_ev.slider.value = target->slider.value;
                    if (target->on_event)
                        target->on_event(target, &out_ev, target->userdata);
                    break;
                }

                case UI_WIDGET_TAB_BAR: {
                    int32_t bx = target->layout_elem ? target->layout_elem->computed_x : 0;
                    int32_t bw = target->layout_elem ? target->layout_elem->computed_w : 1;
                    int32_t count = target->tab_bar.tab_count > 0 ? target->tab_bar.tab_count : 1;
                    int32_t tab_w = bw / count;
                    if (tab_w <= 0)
                        tab_w = 1;
                    int32_t clicked_tab = (mx - bx) / tab_w;
                    if (clicked_tab >= 0 && clicked_tab < count) {
                        target->tab_bar.active_tab = clicked_tab;
                        ui_widget_invalidate(target);
                        ui_event_t out_ev;
                        memset(&out_ev, 0, sizeof(out_ev));
                        out_ev.type = UI_EVENT_VALUE_CHANGED;
                        out_ev.source = target;
                        out_ev.toggle.state = clicked_tab;
                        if (target->on_event)
                            target->on_event(target, &out_ev, target->userdata);
                    }
                    break;
                }

                case UI_WIDGET_LIST: {
                    int32_t bx = target->layout_elem ? target->layout_elem->computed_x : 0;
                    int32_t by = target->layout_elem ? target->layout_elem->computed_y : 0;
                    int32_t bw = target->layout_elem ? target->layout_elem->computed_w : 0;
                    int32_t total_h = 0, inner_h = 0;
                    if (list_has_sb(target, &total_h, &inner_h) &&
                        mx >= bx + bw - UI_SCROLLBAR_W - 2) {
                        sb_press_action(bx + 1, by + 1, bw - 2, inner_h,
                                        total_h, my, &target->list.scroll_top,
                                        &target->list.sb_dragging, &target->list.sb_drag_off);
                        ui_widget_invalidate(target);
                        break;
                    }
                    int32_t item_idx = (my - by + target->list.scroll_top) / UI_LIST_ROW_H;
                    if (item_idx >= 0 && item_idx < target->list.count) {
                        target->list.selected = item_idx;
                        ui_list_scroll_to_selected(target);
                        ui_widget_invalidate(target);
                        ui_event_t out_ev;
                        memset(&out_ev, 0, sizeof(out_ev));
                        out_ev.type = UI_EVENT_VALUE_CHANGED;
                        out_ev.source = target;
                        out_ev.toggle.state = item_idx;
                        if (target->on_event)
                            target->on_event(target, &out_ev, target->userdata);
                    }
                    break;
                }

                case UI_WIDGET_SCROLL_VIEW: {
                    int32_t eff_ch = 0;
                    if (scroll_view_has_sb(target, &eff_ch)) {
                        int32_t vx = target->layout_elem->computed_x;
                        int32_t vy = target->layout_elem->computed_y;
                        int32_t vw = target->layout_elem->computed_w;
                        int32_t vh = target->layout_elem->computed_h;
                        if (mx >= vx + vw - UI_SCROLLBAR_W - 2) {
                            int32_t sy = target->scroll_view.scroll_y;
                            sb_press_action(vx, vy, vw, vh, eff_ch, my, &sy,
                                            &target->scroll_view.sb_dragging,
                                            &target->scroll_view.sb_drag_off);
                            ui_scroll_view_scroll_to(target, sy);
                        }
                    }
                    break;
                }

                case UI_WIDGET_MODAL_DIALOG:
                    target->modal.pressed_btn = modal_hit_button(target, mx, my);
                    if (target->modal.pressed_btn != 0)
                        ui_widget_invalidate(target);
                    break;

                case UI_WIDGET_MENU: {
                    int32_t item_idx = ui_menu_hit_item(target, mx, my);
                    if (item_idx >= 0 && (target->menu.is_bar || !ui_menu_is_disabled(target, item_idx))) {
                        target->menu.selected = item_idx;
                        ui_widget_invalidate(target);
                        ui_event_t out_ev;
                        memset(&out_ev, 0, sizeof(out_ev));
                        out_ev.type = UI_EVENT_VALUE_CHANGED;
                        out_ev.source = target;
                        out_ev.toggle.state = item_idx;
                        if (target->on_event)
                            target->on_event(target, &out_ev, target->userdata);
                        if (ctx->active_popup == target)
                            ui_ctx_hide_popup(ctx);
                    }
                    break;
                }

                default:
                    break;
                }
            }
        } else {
            ctx->mouse_down = 0;
            if (ctx->pressed_widget) {
                ui_widget_t *pw = ctx->pressed_widget;
                ctx->pressed_widget = NULL;

                if (pw->type == UI_WIDGET_BUTTON) {
                    pw->button.pressed = 0;
                    ui_widget_invalidate(pw);
                } else if (pw->type == UI_WIDGET_TEXT_FIELD) {
                    ui_text_field_handle_release(pw);
                } else if (pw->type == UI_WIDGET_SCROLL_VIEW && pw->scroll_view.sb_dragging) {
                    pw->scroll_view.sb_dragging = 0;
                    ui_widget_invalidate(pw);
                } else if (pw->type == UI_WIDGET_LIST && pw->list.sb_dragging) {
                    pw->list.sb_dragging = 0;
                    ui_widget_invalidate(pw);
                }

                ui_widget_t *up_target = NULL;
                if (ctx->active_modal && !ctx->active_modal->hidden)
                    up_target = ui_hit_test(ctx->active_modal, mx, my);
                else if (ctx->active_popup && !ctx->active_popup->hidden)
                    up_target = ui_hit_test(ctx->active_popup, mx, my);
                if (!up_target)
                    up_target = ui_hit_test(root, mx, my);

                if (pw->type == UI_WIDGET_MODAL_DIALOG) {
                    int32_t prev_pressed = pw->modal.pressed_btn;
                    int32_t up_btn = modal_hit_button(pw, mx, my);
                    pw->modal.pressed_btn = 0;
                    ui_widget_invalidate(pw);
                    if (up_target == pw && up_btn != 0 &&
                        (prev_pressed == 0 || prev_pressed == up_btn)) {
                        ui_dialog_result_t res = (up_btn == 1)
                                                 ? UI_DIALOG_RES_OK : UI_DIALOG_RES_CANCEL;
                        dispatch_modal_result(ctx, pw, res, mx, my);
                    }
                } else if (up_target == pw) {
                    if (pw->type == UI_WIDGET_BUTTON || pw->type == UI_WIDGET_BOX) {
                        ui_event_t out_ev;
                        memset(&out_ev, 0, sizeof(out_ev));
                        out_ev.type = UI_EVENT_CLICK;
                        out_ev.source = pw;
                        out_ev.click.x = mx;
                        out_ev.click.y = my;
                        if (pw->on_event)
                            pw->on_event(pw, &out_ev, pw->userdata);
                    }
                }
            }
        }
        return;
    }

    if (ctx->active_modal && !ctx->active_modal->hidden &&
        ctx->active_modal->type == UI_WIDGET_MODAL_DIALOG && ev->value >= 1) {
        ui_widget_t *m = ctx->active_modal;
        int32_t bx = m->layout_elem ? m->layout_elem->computed_x : 0;
        int32_t by = m->layout_elem ? m->layout_elem->computed_y : 0;
        if (ev->code == KEY_ESC) {
            dispatch_modal_result(ctx, m, UI_DIALOG_RES_CANCEL, bx, by);
            return;
        }
        if (ev->code == KEY_ENTER || ev->code == KEY_KPENTER) {
            dispatch_modal_result(ctx, m, UI_DIALOG_RES_OK, bx, by);
            return;
        }
        if (ev->code == KEY_TAB) {
            if (m->modal.input_field)
                ui_widget_set_focus(ctx, m->modal.input_field);
            return;
        }
        if (ctx->focused_widget && !widget_is_descendant(m, ctx->focused_widget))
            return;
    }

    if (ctx->active_popup && !ctx->active_popup->hidden &&
        ctx->active_popup->type == UI_WIDGET_MENU && ev->value >= 1) {
        ui_widget_t *p = ctx->active_popup;
        if (ev->code == KEY_ESC) {
            ui_ctx_hide_popup(ctx);
            return;
        }
        if (ev->code == KEY_UP) {
            menu_step_hover(p, -1);
            return;
        }
        if (ev->code == KEY_DOWN) {
            menu_step_hover(p, 1);
            return;
        }
        if (ev->code == KEY_ENTER || ev->code == KEY_KPENTER) {
            int32_t idx = p->menu.hovered;
            if (idx >= 0 && !ui_menu_is_disabled(p, idx)) {
                p->menu.selected = idx;
                ui_widget_invalidate(p);
                ui_event_t out_ev;
                memset(&out_ev, 0, sizeof(out_ev));
                out_ev.type = UI_EVENT_VALUE_CHANGED;
                out_ev.source = p;
                out_ev.toggle.state = idx;
                if (p->on_event)
                    p->on_event(p, &out_ev, p->userdata);
                if (ctx->active_popup == p)
                    ui_ctx_hide_popup(ctx);
            }
            return;
        }
    }

    if (ev->code == KEY_TAB && ev->value >= 1) {
        int multiline_tab = (ctx->focused_widget &&
                             ctx->focused_widget->type == UI_WIDGET_TEXT_FIELD &&
                             ctx->focused_widget->text_field.multiline &&
                             !ctx->focused_widget->text_field.read_only &&
                             !ctx->shift_down && !ctx->ctrl_down);
        if (!multiline_tab) {
            if (ctx->shift_down)
                ui_focus_prev(ctx, root);
            else
                ui_focus_next(ctx, root);
            return;
        }
    }

    if (!ctx->focused_widget)
        return;

    ui_widget_t *fw = ctx->focused_widget;
    int consumed = 0;

    if (fw->type == UI_WIDGET_BUTTON) {
        if (ev->code == KEY_SPACE || ev->code == KEY_ENTER || ev->code == KEY_KPENTER) {
            consumed = 1;
            if (ev->value >= 1) {
                fw->button.pressed = 1;
                ui_widget_invalidate(fw);
                ui_event_t out_ev;
                memset(&out_ev, 0, sizeof(out_ev));
                out_ev.type = UI_EVENT_CLICK;
                out_ev.source = fw;
                out_ev.click.x = fw->layout_elem ? fw->layout_elem->computed_x : 0;
                out_ev.click.y = fw->layout_elem ? fw->layout_elem->computed_y : 0;
                if (fw->on_event)
                    fw->on_event(fw, &out_ev, fw->userdata);
            } else if (ev->value == 0) {
                fw->button.pressed = 0;
                ui_widget_invalidate(fw);
            }
        }
    } else if (fw->type == UI_WIDGET_CHECKBOX) {
        if ((ev->code == KEY_SPACE || ev->code == KEY_ENTER) && ev->value >= 1) {
            consumed = 1;
            fw->checkbox.checked ^= 1;
            ui_widget_invalidate(fw);
            ui_event_t out_ev;
            memset(&out_ev, 0, sizeof(out_ev));
            out_ev.type = UI_EVENT_VALUE_CHANGED;
            out_ev.source = fw;
            out_ev.toggle.state = fw->checkbox.checked;
            if (fw->on_event)
                fw->on_event(fw, &out_ev, fw->userdata);
        }
    } else if (fw->type == UI_WIDGET_RADIO) {
        if ((ev->code == KEY_SPACE || ev->code == KEY_ENTER) && ev->value >= 1) {
            consumed = 1;
            fw->radio.checked = 1;
            uncheck_radio_group(root, fw->radio.group_id, fw);
            ui_widget_invalidate(fw);
            ui_event_t out_ev;
            memset(&out_ev, 0, sizeof(out_ev));
            out_ev.type = UI_EVENT_VALUE_CHANGED;
            out_ev.source = fw;
            out_ev.toggle.state = 1;
            if (fw->on_event)
                fw->on_event(fw, &out_ev, fw->userdata);
        }
    } else if (fw->type == UI_WIDGET_TAB_BAR) {
        if (ev->value >= 1 && (ev->code == KEY_LEFT || ev->code == KEY_RIGHT)) {
            consumed = 1;
            int count = fw->tab_bar.tab_count > 0 ? fw->tab_bar.tab_count : 1;
            if (ev->code == KEY_LEFT)
                fw->tab_bar.active_tab = (fw->tab_bar.active_tab - 1 + count) % count;
            else
                fw->tab_bar.active_tab = (fw->tab_bar.active_tab + 1) % count;
            ui_widget_invalidate(fw);
            ui_event_t out_ev;
            memset(&out_ev, 0, sizeof(out_ev));
            out_ev.type = UI_EVENT_VALUE_CHANGED;
            out_ev.source = fw;
            out_ev.toggle.state = fw->tab_bar.active_tab;
            if (fw->on_event)
                fw->on_event(fw, &out_ev, fw->userdata);
        }
    } else if (fw->type == UI_WIDGET_SLIDER) {
        if (ev->value >= 1 && (ev->code == KEY_LEFT || ev->code == KEY_RIGHT ||
                               ev->code == KEY_UP || ev->code == KEY_DOWN)) {
            consumed = 1;
            float step = (fw->slider.max_val - fw->slider.min_val) * 0.05f;
            if (step <= 0.0f) step = 1.0f;
            if (ev->code == KEY_LEFT || ev->code == KEY_DOWN)
                fw->slider.value -= step;
            else
                fw->slider.value += step;
            if (fw->slider.value < fw->slider.min_val) fw->slider.value = fw->slider.min_val;
            if (fw->slider.value > fw->slider.max_val) fw->slider.value = fw->slider.max_val;
            ui_widget_invalidate(fw);
            ui_event_t out_ev;
            memset(&out_ev, 0, sizeof(out_ev));
            out_ev.type = UI_EVENT_VALUE_CHANGED;
            out_ev.source = fw;
            out_ev.slider.value = fw->slider.value;
            if (fw->on_event)
                fw->on_event(fw, &out_ev, fw->userdata);
        }
    } else if (fw->type == UI_WIDGET_LIST) {
        if (ev->value >= 1 && fw->list.count > 0 &&
            (ev->code == KEY_UP || ev->code == KEY_DOWN ||
             ev->code == KEY_PAGEUP || ev->code == KEY_PAGEDOWN ||
             ev->code == KEY_HOME || ev->code == KEY_END)) {
            consumed = 1;
            int32_t sel = fw->list.selected;
            if (sel < 0) sel = 0;
            if (ev->code == KEY_UP && sel > 0)
                sel--;
            else if (ev->code == KEY_DOWN && sel < fw->list.count - 1)
                sel++;
            else if (ev->code == KEY_HOME)
                sel = 0;
            else if (ev->code == KEY_END)
                sel = fw->list.count - 1;
            else if (ev->code == KEY_PAGEUP || ev->code == KEY_PAGEDOWN) {
                int32_t vis = (fw->layout_elem && fw->layout_elem->computed_h > UI_LIST_ROW_H)
                              ? (fw->layout_elem->computed_h / UI_LIST_ROW_H) : 5;
                sel += (ev->code == KEY_PAGEUP) ? -vis : vis;
                if (sel < 0) sel = 0;
                if (sel >= fw->list.count) sel = fw->list.count - 1;
            }
            fw->list.selected = sel;
            ui_list_scroll_to_selected(fw);
            ui_widget_invalidate(fw);
            ui_event_t out_ev;
            memset(&out_ev, 0, sizeof(out_ev));
            out_ev.type = UI_EVENT_VALUE_CHANGED;
            out_ev.source = fw;
            out_ev.toggle.state = fw->list.selected;
            if (fw->on_event)
                fw->on_event(fw, &out_ev, fw->userdata);
        }
    } else if (fw->type == UI_WIDGET_TEXT_FIELD) {
        consumed = 1;
        ui_text_field_handle_key_ctx(ctx, fw, ev, ctx->shift_down, ctx->ctrl_down);
    }

    if (!consumed && ev->value >= 1) {
        ui_event_t kev;
        memset(&kev, 0, sizeof(kev));
        kev.type = UI_EVENT_KEY_DOWN;
        kev.source = fw;
        kev.key.code = ev->code;
        kev.key.shift = ctx->shift_down;
        kev.key.ctrl = ctx->ctrl_down;
        if (fw->on_event)
            fw->on_event(fw, &kev, fw->userdata);
    }
}
