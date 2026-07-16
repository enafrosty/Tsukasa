/*
 * wm.c - Window manager with dirty-region tracking and normalized routing.
 */

#include "wm.h"
#include "ui.h"
#include "../drv/fb.h"
#include "../mm/heap.h"

#include <stddef.h>

#define WM_RESIZE_GRIP 12
#define WM_DIRTY_MAX   128
#define WM_MIN_W       140
#define WM_MIN_H        90

/* Pool of window structs (avoid per-alloc overhead). */
static wm_window_t win_pool[WM_MAX_WINDOWS];
static int win_pool_used[WM_MAX_WINDOWS];

/* Z-order doubly-linked list: head = bottom, tail = top. */
static wm_window_t *zlist_head;
static wm_window_t *zlist_tail;

/* Interaction state. */
static wm_window_t *drag_win;
static wm_window_t *resize_win;
static int drag_offset_x, drag_offset_y;
static int resize_start_w, resize_start_h;
static int resize_start_mx, resize_start_my;
static int prev_buttons;

/* Dirty region queue. */
static wm_dirty_rect_t dirty_regions[WM_DIRTY_MAX];
static int dirty_count;

/* ---- String helpers --------------------------------------------------- */

static void kstrcpy(char *dst, const char *src, int max)
{
    int i = 0;
    while (src && src[i] && i < max - 1) {
        dst[i] = src[i];
        i++;
    }
    dst[i] = '\0';
}

/* ---- Rect helpers ----------------------------------------------------- */

static int rect_intersects(int ax, int ay, int aw, int ah,
                           int bx, int by, int bw, int bh)
{
    int ax1 = ax + aw;
    int ay1 = ay + ah;
    int bx1 = bx + bw;
    int by1 = by + bh;
    return !(ax1 <= bx || bx1 <= ax || ay1 <= by || by1 <= ay);
}

static int clampi(int v, int lo, int hi)
{
    if (v < lo)
        return lo;
    if (v > hi)
        return hi;
    return v;
}

static void wm_mark_window_dirty(const wm_window_t *w)
{
    int margin;
    if (!w)
        return;
    /*
     * Include shadow spill so moving/resizing windows does not leave ghost
     * silhouettes from rounded drop-shadows.
     */
    margin = UI_SHADOW_R + 2;
    wm_mark_dirty_rect(w->x - margin,
                       w->y - margin,
                       w->w + margin * 2,
                       w->h + margin * 2);
}

/* ---- Dirty queue ------------------------------------------------------ */

void wm_mark_dirty_rect(int x, int y, int w, int h)
{
    if (w <= 0 || h <= 0)
        return;

    if (fb_info.width && fb_info.height) {
        int x1 = x + w;
        int y1 = y + h;
        x = clampi(x, 0, (int)fb_info.width);
        y = clampi(y, 0, (int)fb_info.height);
        x1 = clampi(x1, 0, (int)fb_info.width);
        y1 = clampi(y1, 0, (int)fb_info.height);
        w = x1 - x;
        h = y1 - y;
    }
    if (w <= 0 || h <= 0)
        return;

    if (dirty_count >= WM_DIRTY_MAX) {
        /* Collapse on overflow to one full-screen repaint region. */
        dirty_regions[0].x = 0;
        dirty_regions[0].y = 0;
        dirty_regions[0].w = (int)fb_info.width;
        dirty_regions[0].h = (int)fb_info.height;
        dirty_count = 1;
        return;
    }

    dirty_regions[dirty_count].x = x;
    dirty_regions[dirty_count].y = y;
    dirty_regions[dirty_count].w = w;
    dirty_regions[dirty_count].h = h;
    dirty_count++;
}

int wm_collect_dirty_regions(wm_dirty_rect_t *out, int max)
{
    int n = 0;
    if (!out || max <= 0)
        return 0;
    while (n < dirty_count && n < max) {
        out[n] = dirty_regions[n];
        n++;
    }
    dirty_count = 0;
    return n;
}

/* ---- Z-list manipulation ---------------------------------------------- */

static void zlist_remove(wm_window_t *w)
{
    if (!w)
        return;
    if (w->prev) w->prev->next = w->next; else zlist_head = w->next;
    if (w->next) w->next->prev = w->prev; else zlist_tail = w->prev;
    w->prev = w->next = NULL;
}

static void zlist_push_top(wm_window_t *w)
{
    if (!w)
        return;
    w->prev = zlist_tail;
    w->next = NULL;
    if (zlist_tail) zlist_tail->next = w; else zlist_head = w;
    zlist_tail = w;
}

/* ---- Event dispatch --------------------------------------------------- */

static void dispatch_to_window(wm_window_t *w,
                               const struct input_event *src,
                               uint16_t event_id,
                               uint8_t subtype,
                               uint32_t keycode,
                               int32_t x,
                               int32_t y)
{
    struct input_event ev;
    if (!w || !w->handle_event)
        return;
    if (src)
        ev = *src;
    else {
        ev.event_id = INPUT_EVENT_NONE;
        ev.type = EVENT_MOUSE;
        ev.subtype = 0;
        ev.keycode = 0;
        ev.x = 0;
        ev.y = 0;
        ev.wheel_delta = 0;
        ev.width = 0;
        ev.height = 0;
        ev.modifiers = 0;
        ev.window_id = -1;
    }
    ev.event_id = event_id;
    ev.subtype = subtype;
    ev.keycode = keycode;
    ev.x = x;
    ev.y = y;
    w->handle_event(w, &ev);
    wm_mark_window_dirty(w);
}

/* ---- Public API ------------------------------------------------------- */

void wm_init(void)
{
    for (int i = 0; i < WM_MAX_WINDOWS; i++)
        win_pool_used[i] = 0;
    zlist_head = zlist_tail = NULL;
    drag_win = NULL;
    resize_win = NULL;
    drag_offset_x = drag_offset_y = 0;
    resize_start_w = resize_start_h = 0;
    resize_start_mx = resize_start_my = 0;
    prev_buttons = 0;
    dirty_count = 0;
}

wm_window_t *wm_create_window(int x, int y, int w, int h, const char *title,
                              wm_draw_fn draw_fn, wm_event_fn event_fn,
                              void *app_data)
{
    int slot = -1;
    wm_window_t *win;

    for (int i = 0; i < WM_MAX_WINDOWS; i++) {
        if (!win_pool_used[i]) {
            slot = i;
            break;
        }
    }
    if (slot < 0)
        return NULL;

    if (w < WM_MIN_W) w = WM_MIN_W;
    if (h < WM_MIN_H) h = WM_MIN_H;

    win = &win_pool[slot];
    win_pool_used[slot] = 1;

    win->x = x;
    win->y = y;
    win->w = w;
    win->h = h;
    win->min_w = WM_MIN_W;
    win->min_h = WM_MIN_H;
    kstrcpy(win->title, title ? title : "", WM_TITLE_MAX);
    win->flags = WM_FLAG_VISIBLE | WM_FLAG_CLOSABLE | WM_FLAG_RESIZABLE | WM_FLAG_AUTOCLOSE;
    win->opacity = 245;
    win->accent = 0;
    win->draw_content = draw_fn;
    win->handle_event = event_fn;
    win->app_data = app_data;
    win->prev = NULL;
    win->next = NULL;

    if (zlist_tail)
        zlist_tail->flags &= ~WM_FLAG_ACTIVE;

    zlist_push_top(win);
    win->flags |= WM_FLAG_ACTIVE;
    wm_mark_window_dirty(win);
    return win;
}

void wm_destroy_window(wm_window_t *win)
{
    int idx;
    if (!win)
        return;
    wm_mark_window_dirty(win);
    if (drag_win == win)
        drag_win = NULL;
    if (resize_win == win)
        resize_win = NULL;
    zlist_remove(win);
    if (win->app_data) {
        kfree(win->app_data);
        win->app_data = NULL;
    }
    idx = (int)(win - win_pool);
    if (idx >= 0 && idx < WM_MAX_WINDOWS)
        win_pool_used[idx] = 0;
    if (zlist_tail) {
        zlist_tail->flags |= WM_FLAG_ACTIVE;
        wm_mark_window_dirty(zlist_tail);
    }
}

void wm_bring_to_front(wm_window_t *win)
{
    if (!win || win == zlist_tail)
        return;
    if (zlist_tail) {
        zlist_tail->flags &= ~WM_FLAG_ACTIVE;
        wm_mark_window_dirty(zlist_tail);
    }
    zlist_remove(win);
    zlist_push_top(win);
    win->flags |= WM_FLAG_ACTIVE;
    wm_mark_window_dirty(win);
}

wm_window_t *wm_find_window_at(int px, int py)
{
    for (wm_window_t *w = zlist_tail; w; w = w->prev) {
        if (!(w->flags & WM_FLAG_VISIBLE))
            continue;
        if (px >= w->x && px < w->x + w->w &&
            py >= w->y && py < w->y + w->h)
            return w;
    }
    return NULL;
}

void wm_client_rect(const wm_window_t *win, int *cx, int *cy, int *cw, int *ch)
{
    if (!win || !cx || !cy || !cw || !ch)
        return;
    *cx = win->x + WM_BORDER_PX;
    *cy = win->y + WM_BORDER_PX + WM_TITLE_BAR_H;
    *cw = win->w - 2 * WM_BORDER_PX;
    *ch = win->h - 2 * WM_BORDER_PX - WM_TITLE_BAR_H;
}

static int hit_close_btn(const wm_window_t *w, int px, int py)
{
    int bx = w->x + WM_BORDER_PX + 14;
    int by = w->y + WM_BORDER_PX + UI_TBTN_Y_OFF;
    int dx = px - bx;
    int dy = py - by;
    int r = UI_TBTN_R + 4;
    return (dx * dx + dy * dy) <= (r * r);
}

static int hit_title_bar(const wm_window_t *w, int px, int py)
{
    return (px >= w->x && px < w->x + w->w &&
            py >= w->y && py < w->y + WM_BORDER_PX + WM_TITLE_BAR_H);
}

static int hit_resize_grip(const wm_window_t *w, int px, int py)
{
    return (px >= w->x + w->w - WM_RESIZE_GRIP &&
            px < w->x + w->w &&
            py >= w->y + w->h - WM_RESIZE_GRIP &&
            py < w->y + w->h);
}

int wm_handle_mouse(int mx, int my, int buttons, int btn_changed)
{
    int left_pressed = (buttons & MOUSE_BUTTON_LEFT) != 0;
    int right_pressed = (buttons & MOUSE_BUTTON_RIGHT) != 0;
    int left_down_edge = left_pressed && (btn_changed & MOUSE_BUTTON_LEFT);
    int left_up_edge = (!left_pressed) && (btn_changed & MOUSE_BUTTON_LEFT);
    int right_down_edge = right_pressed && (btn_changed & MOUSE_BUTTON_RIGHT);
    int right_up_edge = (!right_pressed) && (btn_changed & MOUSE_BUTTON_RIGHT);

    /* Resize in progress. */
    if (resize_win) {
        if (left_pressed) {
            int old_x = resize_win->x;
            int old_y = resize_win->y;
            int old_w = resize_win->w;
            int old_h = resize_win->h;
            int new_w = resize_start_w + (mx - resize_start_mx);
            int new_h = resize_start_h + (my - resize_start_my);
            int cx, cy, cw, ch;

            if (new_w < resize_win->min_w) new_w = resize_win->min_w;
            if (new_h < resize_win->min_h) new_h = resize_win->min_h;

            resize_win->w = new_w;
            resize_win->h = new_h;

            wm_mark_dirty_rect(old_x - (UI_SHADOW_R + 2),
                               old_y - (UI_SHADOW_R + 2),
                               old_w + (UI_SHADOW_R + 2) * 2,
                               old_h + (UI_SHADOW_R + 2) * 2);
            wm_mark_window_dirty(resize_win);

            wm_client_rect(resize_win, &cx, &cy, &cw, &ch);
            dispatch_to_window(resize_win, NULL,
                               INPUT_EVENT_RESIZE, MOUSE_MOVE, (uint32_t)buttons, cw, ch);
            prev_buttons = buttons;
            return 1;
        }
        resize_win = NULL;
        prev_buttons = buttons;
        return 1;
    }

    /* Drag in progress. */
    if (drag_win) {
        if (left_pressed) {
            int old_x = drag_win->x;
            int old_y = drag_win->y;
            drag_win->x = mx - drag_offset_x;
            drag_win->y = my - drag_offset_y;
            if (drag_win->y < 0)
                drag_win->y = 0;
            wm_mark_dirty_rect(old_x - (UI_SHADOW_R + 2),
                               old_y - (UI_SHADOW_R + 2),
                               drag_win->w + (UI_SHADOW_R + 2) * 2,
                               drag_win->h + (UI_SHADOW_R + 2) * 2);
            wm_mark_window_dirty(drag_win);
            prev_buttons = buttons;
            return 1;
        }
        drag_win->flags &= ~WM_FLAG_DRAGGING;
        drag_win = NULL;
        prev_buttons = buttons;
        return 1;
    }

    if (left_down_edge || right_down_edge) {
        wm_window_t *hit = wm_find_window_at(mx, my);
        if (hit) {
            wm_bring_to_front(hit);

            if (left_down_edge &&
                (hit->flags & WM_FLAG_CLOSABLE) &&
                hit_close_btn(hit, mx, my)) {
                dispatch_to_window(hit, NULL, INPUT_EVENT_CLOSE, MOUSE_BTN_DOWN, (uint32_t)buttons, mx, my);
                if (hit->flags & WM_FLAG_AUTOCLOSE)
                    wm_destroy_window(hit);
                prev_buttons = buttons;
                return 1;
            }

            if (left_down_edge &&
                (hit->flags & WM_FLAG_RESIZABLE) &&
                hit_resize_grip(hit, mx, my)) {
                resize_win = hit;
                resize_start_w = hit->w;
                resize_start_h = hit->h;
                resize_start_mx = mx;
                resize_start_my = my;
                prev_buttons = buttons;
                return 1;
            }

            if (left_down_edge && hit_title_bar(hit, mx, my)) {
                drag_win = hit;
                drag_offset_x = mx - hit->x;
                drag_offset_y = my - hit->y;
                hit->flags |= WM_FLAG_DRAGGING;
                prev_buttons = buttons;
                return 1;
            }

            if (left_down_edge) {
                dispatch_to_window(hit, NULL, INPUT_EVENT_MOUSE_DOWN, MOUSE_BTN_DOWN, (uint32_t)buttons, mx, my);
                dispatch_to_window(hit, NULL, INPUT_EVENT_CLICK, MOUSE_BTN_DOWN, (uint32_t)buttons, mx, my);
            }
            if (right_down_edge) {
                dispatch_to_window(hit, NULL, INPUT_EVENT_RIGHT_CLICK, MOUSE_BTN_DOWN, (uint32_t)buttons, mx, my);
            }
            prev_buttons = buttons;
            return 1;
        }
    }

    if (left_up_edge || right_up_edge) {
        wm_window_t *hit = wm_find_window_at(mx, my);
        if (hit) {
            dispatch_to_window(hit, NULL, INPUT_EVENT_MOUSE_UP, MOUSE_BTN_UP, (uint32_t)buttons, mx, my);
            prev_buttons = buttons;
            return 1;
        }
    }

    if (!btn_changed) {
        wm_window_t *hit = wm_find_window_at(mx, my);
        if (hit) {
            dispatch_to_window(hit, NULL, INPUT_EVENT_MOUSE_MOVE, MOUSE_MOVE, (uint32_t)buttons, mx, my);
            prev_buttons = buttons;
            return 1;
        }
    }

    prev_buttons = buttons;
    return 0;
}

int wm_handle_input(const struct input_event *ev)
{
    if (!ev)
        return 0;

    if (ev->event_id == INPUT_EVENT_KEY || ev->event_id == INPUT_EVENT_KEYUP) {
        wm_window_t *top = wm_get_top();
        if (!top || !top->handle_event)
            return 0;
        dispatch_to_window(top,
                           ev,
                           ev->event_id,
                           (ev->event_id == INPUT_EVENT_KEY) ? KEY_PRESS : KEY_RELEASE,
                           ev->keycode,
                           ev->x,
                           ev->y);
        return 1;
    }

    if (ev->event_id == INPUT_EVENT_MOUSE_MOVE ||
        ev->event_id == INPUT_EVENT_MOUSE_DOWN ||
        ev->event_id == INPUT_EVENT_MOUSE_UP ||
        ev->event_id == INPUT_EVENT_RIGHT_CLICK ||
        ev->event_id == INPUT_EVENT_CLICK) {
        int buttons = (int)(ev->keycode & 0x07u);
        int changed = buttons ^ prev_buttons;
        return wm_handle_mouse(ev->x, ev->y, buttons, changed);
    }

    if (ev->event_id == INPUT_EVENT_MOUSE_WHEEL) {
        wm_window_t *target = wm_find_window_at(ev->x, ev->y);
        if (!target)
            target = wm_get_top();
        if (!target)
            return 0;
        dispatch_to_window(target,
                           ev,
                           INPUT_EVENT_MOUSE_WHEEL,
                           MOUSE_MOVE,
                           ev->keycode,
                           ev->x,
                           ev->y);
        return 1;
    }

    return 0;
}

void wm_redraw_all(void)
{
    for (wm_window_t *w = zlist_head; w; w = w->next) {
        int active;
        if (!(w->flags & WM_FLAG_VISIBLE))
            continue;
        active = (w->flags & WM_FLAG_ACTIVE) ? 1 : 0;
        ui_draw_window(w->x, w->y, w->w, w->h, w->title, active, w->accent);
        if (w->draw_content)
            w->draw_content(w);
    }
}

void wm_redraw_region(int x, int y, int w, int h)
{
    int shadow_margin = UI_SHADOW_R + 2;
    for (wm_window_t *win = zlist_head; win; win = win->next) {
        int active;
        if (!(win->flags & WM_FLAG_VISIBLE))
            continue;
        if (!rect_intersects(x, y, w, h,
                             win->x - shadow_margin,
                             win->y - shadow_margin,
                             win->w + shadow_margin * 2,
                             win->h + shadow_margin * 2))
            continue;
        active = (win->flags & WM_FLAG_ACTIVE) ? 1 : 0;
        ui_draw_window(win->x, win->y, win->w, win->h, win->title, active, win->accent);
        if (win->draw_content)
            win->draw_content(win);
    }
}

void wm_set_resizable(wm_window_t *win, int resizable)
{
    if (!win)
        return;
    if (resizable)
        win->flags |= WM_FLAG_RESIZABLE;
    else
        win->flags &= ~WM_FLAG_RESIZABLE;
    wm_mark_window_dirty(win);
}

void wm_set_title(wm_window_t *win, const char *title)
{
    if (!win)
        return;
    kstrcpy(win->title, title ? title : "", WM_TITLE_MAX);
    wm_mark_window_dirty(win);
}

void wm_set_autoclose(wm_window_t *win, int autoclose)
{
    if (!win)
        return;
    if (autoclose)
        win->flags |= WM_FLAG_AUTOCLOSE;
    else
        win->flags &= ~WM_FLAG_AUTOCLOSE;
}

wm_window_t *wm_get_bottom(void) { return zlist_head; }
wm_window_t *wm_get_top(void)    { return zlist_tail; }
