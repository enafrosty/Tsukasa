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
#include <sys/input.h>

ui_widget_t *ui_hit_test(ui_widget_t *root, int32_t x, int32_t y)
{
    if (!root || !root->layout_elem)
        return NULL;

    int32_t rx = root->layout_elem->computed_x;
    int32_t ry = root->layout_elem->computed_y;
    int32_t rw = root->layout_elem->computed_w;
    int32_t rh = root->layout_elem->computed_h;

    if (x < rx || x >= rx + rw || y < ry || y >= ry + rh)
        return NULL;

    ui_widget_t *hit = NULL;
    for (ui_widget_t *c = root->first_child; c; c = c->next_sibling) {
        ui_widget_t *child_hit = ui_hit_test(c, x, y);
        if (child_hit)
            hit = child_hit;
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
    if (!w || *count >= max_items)
        return;

    /* Skip subtrees that are explicitly collapsed to 0 size with clipping enabled */
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

    if (count == 0) {
        if (ctx->focused_widget)
            ui_widget_set_focus(ctx, NULL);
        return;
    }

    int current_idx = -1;
    for (int i = 0; i < count; i++) {
        if (list[i] == ctx->focused_widget) {
            current_idx = i;
            break;
        }
    }

    int next_idx = (current_idx >= 0) ? (current_idx + 1) % count : 0;
    ui_widget_set_focus(ctx, list[next_idx]);
}

void ui_focus_prev(ui_ctx_t *ctx, ui_widget_t *root)
{
    if (!ctx || !root)
        return;

    ui_widget_t *list[128];
    int count = 0;
    collect_focusable(root, list, &count, 128);

    if (count == 0) {
        if (ctx->focused_widget)
            ui_widget_set_focus(ctx, NULL);
        return;
    }

    int current_idx = -1;
    for (int i = 0; i < count; i++) {
        if (list[i] == ctx->focused_widget) {
            current_idx = i;
            break;
        }
    }

    int prev_idx = (current_idx >= 0) ? (current_idx - 1 + count) % count : count - 1;
    ui_widget_set_focus(ctx, list[prev_idx]);
}

static void uncheck_radio_group(ui_widget_t *node, int32_t group_id, ui_widget_t *except)
{
    if (!node)
        return;

    if (node->type == UI_WIDGET_RADIO && node->radio.group_id == group_id && node != except) {
        node->radio.checked = 0;
        ui_widget_invalidate(node);
    }

    for (ui_widget_t *c = node->first_child; c; c = c->next_sibling)
        uncheck_radio_group(c, group_id, except);
}

void ui_handle_event(ui_ctx_t *ctx, ui_widget_t *root, const struct input_event *ev)
{
    if (!ctx || !root || !ev)
        return;

    if (ev->type == EV_REL || ev->type == EV_ABS) {
        ctx->mouse_x = (int32_t)ev->pad1;
        ctx->mouse_y = (int32_t)ev->pad2;

        if (ctx->mouse_down && ctx->pressed_widget && ctx->pressed_widget->type == UI_WIDGET_SLIDER) {
            ui_widget_t *s = ctx->pressed_widget;
            int32_t bx = s->layout_elem ? s->layout_elem->computed_x : 0;
            int32_t bw = s->layout_elem ? s->layout_elem->computed_w : 1;
            if (bw <= 0)
                bw = 1;
            float frac = (float)(ctx->mouse_x - bx) / (float)bw;
            if (frac < 0.0f) frac = 0.0f;
            if (frac > 1.0f) frac = 1.0f;
            s->slider.value = s->slider.min_val + frac * (s->slider.max_val - s->slider.min_val);
            ui_widget_invalidate(s);
            ui_event_t out_ev;
            out_ev.type = UI_EVENT_VALUE_CHANGED;
            out_ev.source = s;
            out_ev.slider.value = s->slider.value;
            if (s->on_event)
                s->on_event(s, &out_ev, s->userdata);
        }
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

    if (ev->code == BTN_LEFT) {
        int32_t mx = (int32_t)ev->pad1;
        int32_t my = (int32_t)ev->pad2;
        ctx->mouse_x = mx;
        ctx->mouse_y = my;

        if (ev->value == 1) {
            ctx->mouse_down = 1;
            ui_widget_t *target = ui_hit_test(root, mx, my);
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
                    target->slider.value = target->slider.min_val + frac * (target->slider.max_val - target->slider.min_val);
                    ui_widget_invalidate(target);
                    ui_event_t out_ev;
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
                        out_ev.type = UI_EVENT_VALUE_CHANGED;
                        out_ev.source = target;
                        out_ev.toggle.state = clicked_tab;
                        if (target->on_event)
                            target->on_event(target, &out_ev, target->userdata);
                    }
                    break;
                }

                case UI_WIDGET_LIST: {
                    int32_t by = target->layout_elem ? target->layout_elem->computed_y : 0;
                    int32_t row_h = 24;
                    int32_t item_idx = (my - by + target->list.scroll_top) / row_h;
                    if (item_idx >= 0 && item_idx < target->list.count) {
                        target->list.selected = item_idx;
                        ui_widget_invalidate(target);
                        ui_event_t out_ev;
                        out_ev.type = UI_EVENT_VALUE_CHANGED;
                        out_ev.source = target;
                        out_ev.toggle.state = item_idx;
                        if (target->on_event)
                            target->on_event(target, &out_ev, target->userdata);
                    }
                    break;
                }

                case UI_WIDGET_MODAL_DIALOG:
                    break;

                case UI_WIDGET_MENU: {
                    int32_t by = target->layout_elem ? target->layout_elem->computed_y : 0;
                    int32_t row_h = 24;
                    int32_t item_idx = (my - by) / row_h;
                    if (item_idx >= 0 && item_idx < target->menu.count) {
                        target->menu.selected = item_idx;
                        ui_widget_invalidate(target);
                        ui_event_t out_ev;
                        out_ev.type = UI_EVENT_VALUE_CHANGED;
                        out_ev.source = target;
                        out_ev.toggle.state = item_idx;
                        if (target->on_event)
                            target->on_event(target, &out_ev, target->userdata);
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
                }

                ui_widget_t *up_target = ui_hit_test(root, mx, my);
                if (up_target == pw) {
                    if (pw->type == UI_WIDGET_BUTTON || pw->type == UI_WIDGET_BOX) {
                        ui_event_t out_ev;
                        out_ev.type = UI_EVENT_CLICK;
                        out_ev.source = pw;
                        out_ev.click.x = mx;
                        out_ev.click.y = my;
                        if (pw->on_event)
                            pw->on_event(pw, &out_ev, pw->userdata);
                    } else if (pw->type == UI_WIDGET_MODAL_DIALOG) {
                        int32_t bx = pw->layout_elem ? pw->layout_elem->computed_x : 0;
                        int32_t by = pw->layout_elem ? pw->layout_elem->computed_y : 0;
                        int32_t bw = pw->layout_elem ? pw->layout_elem->computed_w : 0;
                        int32_t bh = pw->layout_elem ? pw->layout_elem->computed_h : 0;
                        int32_t cw = 360, ch = 180;
                        int32_t cx = bx + (bw - cw) / 2;
                        int32_t cy = by + (bh - ch) / 2;
                        int32_t btn_x = cx + cw - 96;
                        int32_t btn_y = cy + ch - 40;
                        if (mx >= btn_x && mx < btn_x + 80 && my >= btn_y && my < btn_y + 28) {
                            ui_event_t out_ev;
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
        }
        return;
    }

    if (ev->code == KEY_TAB && ev->value == 1) {
        if (ctx->shift_down)
            ui_focus_prev(ctx, root);
        else
            ui_focus_next(ctx, root);
        return;
    }

    if (!ctx->focused_widget)
        return;

    ui_widget_t *fw = ctx->focused_widget;
    int consumed = 0;

    if (fw->type == UI_WIDGET_BUTTON) {
        if (ev->code == KEY_SPACE || ev->code == KEY_ENTER || ev->code == KEY_KPENTER) {
            consumed = 1;
            if (ev->value == 1) {
                fw->button.pressed = 1;
                ui_widget_invalidate(fw);
                ui_event_t out_ev;
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
        if ((ev->code == KEY_SPACE || ev->code == KEY_ENTER) && ev->value == 1) {
            consumed = 1;
            fw->checkbox.checked ^= 1;
            ui_widget_invalidate(fw);
            ui_event_t out_ev;
            out_ev.type = UI_EVENT_VALUE_CHANGED;
            out_ev.source = fw;
            out_ev.toggle.state = fw->checkbox.checked;
            if (fw->on_event)
                fw->on_event(fw, &out_ev, fw->userdata);
        }
    } else if (fw->type == UI_WIDGET_RADIO) {
        if ((ev->code == KEY_SPACE || ev->code == KEY_ENTER) && ev->value == 1) {
            consumed = 1;
            fw->radio.checked = 1;
            uncheck_radio_group(root, fw->radio.group_id, fw);
            ui_widget_invalidate(fw);
            ui_event_t out_ev;
            out_ev.type = UI_EVENT_VALUE_CHANGED;
            out_ev.source = fw;
            out_ev.toggle.state = 1;
            if (fw->on_event)
                fw->on_event(fw, &out_ev, fw->userdata);
        }
    } else if (fw->type == UI_WIDGET_TAB_BAR) {
        if (ev->value == 1 && (ev->code == KEY_LEFT || ev->code == KEY_RIGHT)) {
            consumed = 1;
            int count = fw->tab_bar.tab_count > 0 ? fw->tab_bar.tab_count : 1;
            if (ev->code == KEY_LEFT)
                fw->tab_bar.active_tab = (fw->tab_bar.active_tab - 1 + count) % count;
            else
                fw->tab_bar.active_tab = (fw->tab_bar.active_tab + 1) % count;
            ui_widget_invalidate(fw);
            ui_event_t out_ev;
            out_ev.type = UI_EVENT_VALUE_CHANGED;
            out_ev.source = fw;
            out_ev.toggle.state = fw->tab_bar.active_tab;
            if (fw->on_event)
                fw->on_event(fw, &out_ev, fw->userdata);
        }
    } else if (fw->type == UI_WIDGET_SLIDER) {
        if (ev->value == 1 && (ev->code == KEY_LEFT || ev->code == KEY_RIGHT || ev->code == KEY_UP || ev->code == KEY_DOWN)) {
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
            out_ev.type = UI_EVENT_VALUE_CHANGED;
            out_ev.source = fw;
            out_ev.slider.value = fw->slider.value;
            if (fw->on_event)
                fw->on_event(fw, &out_ev, fw->userdata);
        }
    } else if (fw->type == UI_WIDGET_LIST) {
        if (ev->value == 1 && (ev->code == KEY_UP || ev->code == KEY_DOWN)) {
            consumed = 1;
            if (ev->code == KEY_UP && fw->list.selected > 0)
                fw->list.selected--;
            else if (ev->code == KEY_DOWN && fw->list.selected < fw->list.count - 1)
                fw->list.selected++;
            ui_widget_invalidate(fw);
            ui_event_t out_ev;
            out_ev.type = UI_EVENT_VALUE_CHANGED;
            out_ev.source = fw;
            out_ev.toggle.state = fw->list.selected;
            if (fw->on_event)
                fw->on_event(fw, &out_ev, fw->userdata);
        }
    } else if (fw->type == UI_WIDGET_TEXT_FIELD) {
        consumed = 1;
        ui_text_field_handle_key(fw, ev, ctx->shift_down, ctx->ctrl_down);
    }

    if (!consumed && ev->value == 1) {
        ui_event_t kev;
        kev.type = UI_EVENT_KEY_DOWN;
        kev.source = fw;
        kev.key.code = ev->code;
        kev.key.shift = ctx->shift_down;
        kev.key.ctrl = ctx->ctrl_down;
        if (fw->on_event)
            fw->on_event(fw, &kev, fw->userdata);
    }
}
