/*
 * wm.c  -  Window manager implementation.
 * Z-ordered doubly-linked list, hit-testing, drag, close, focus.
 * Chrome rendering delegated to ui.h / ui.c.
 */

#include "wm.h"
#include "ui.h"
#include "../input/event.h"
#include "../mm/heap.h"
#include <stddef.h>

/* Pool of window structs (avoid per-alloc overhead). */
static wm_window_t win_pool[WM_MAX_WINDOWS];
static int         win_pool_used[WM_MAX_WINDOWS];

/* Z-order doubly-linked list: head = bottom, tail = top. */
static wm_window_t *zlist_head;
static wm_window_t *zlist_tail;

/* Drag state. */
static wm_window_t *drag_win;
static int          drag_offset_x, drag_offset_y;

/* Previous mouse button state. */
static int prev_buttons;

/* ---- String helpers --------------------------------------------------- */

static int kstrlen(const char *s)
{
    int n = 0;
    while (s && s[n]) n++;
    return n;
}

static void kstrcpy(char *dst, const char *src, int max)
{
    int i = 0;
    while (src && src[i] && i < max - 1) { dst[i] = src[i]; i++; }
    dst[i] = '\0';
}

/* ---- Z-list manipulation ---------------------------------------------- */

static void zlist_remove(wm_window_t *w)
{
    if (w->prev) w->prev->next = w->next; else zlist_head = w->next;
    if (w->next) w->next->prev = w->prev; else zlist_tail = w->prev;
    w->prev = w->next = NULL;
}

static void zlist_push_top(wm_window_t *w)
{
    w->prev = zlist_tail;
    w->next = NULL;
    if (zlist_tail) zlist_tail->next = w; else zlist_head = w;
    zlist_tail = w;
}

/* ---- Public API ------------------------------------------------------- */

void wm_init(void)
{
    for (int i = 0; i < WM_MAX_WINDOWS; i++)
        win_pool_used[i] = 0;
    zlist_head = zlist_tail = NULL;
    drag_win   = NULL;
    prev_buttons = 0;
}

wm_window_t *wm_create_window(int x, int y, int w, int h, const char *title,
                               wm_draw_fn draw_fn, wm_event_fn event_fn,
                               void *app_data)
{
    /* Find a free slot. */
    int slot = -1;
    for (int i = 0; i < WM_MAX_WINDOWS; i++) {
        if (!win_pool_used[i]) { slot = i; break; }
    }
    if (slot < 0) return NULL;

    wm_window_t *win = &win_pool[slot];
    win_pool_used[slot] = 1;

    win->x = x;  win->y = y;
    win->w = w;  win->h = h;
    kstrcpy(win->title, title ? title : "", WM_TITLE_MAX);
    win->flags        = WM_FLAG_VISIBLE | WM_FLAG_CLOSABLE;
    win->opacity      = 245;
    win->accent       = 0;   /* use global */
    win->draw_content = draw_fn;
    win->handle_event = event_fn;
    win->app_data     = app_data;
    win->prev         = win->next = NULL;

    /* Deactivate current top. */
    if (zlist_tail) zlist_tail->flags &= ~WM_FLAG_ACTIVE;

    zlist_push_top(win);
    win->flags |= WM_FLAG_ACTIVE;
    return win;
}

void wm_destroy_window(wm_window_t *win)
{
    if (!win) return;
    if (drag_win == win) drag_win = NULL;
    zlist_remove(win);
    if (win->app_data) { kfree(win->app_data); win->app_data = NULL; }
    int idx = (int)(win - win_pool);
    if (idx >= 0 && idx < WM_MAX_WINDOWS)
        win_pool_used[idx] = 0;
    if (zlist_tail) zlist_tail->flags |= WM_FLAG_ACTIVE;
}

void wm_bring_to_front(wm_window_t *win)
{
    if (!win || win == zlist_tail) return;
    if (zlist_tail) zlist_tail->flags &= ~WM_FLAG_ACTIVE;
    zlist_remove(win);
    zlist_push_top(win);
    win->flags |= WM_FLAG_ACTIVE;
}

wm_window_t *wm_find_window_at(int px, int py)
{
    for (wm_window_t *w = zlist_tail; w; w = w->prev) {
        if (!(w->flags & WM_FLAG_VISIBLE)) continue;
        if (px >= w->x && px < w->x + w->w &&
            py >= w->y && py < w->y + w->h)
            return w;
    }
    return NULL;
}

void wm_client_rect(const wm_window_t *win,
                    int *cx, int *cy, int *cw, int *ch)
{
    if (!win) return;
    *cx = win->x + WM_BORDER_PX;
    *cy = win->y + WM_BORDER_PX + WM_TITLE_BAR_H;
    *cw = win->w - 2 * WM_BORDER_PX;
    *ch = win->h - 2 * WM_BORDER_PX - WM_TITLE_BAR_H;
}

/* ----------------------------- hit-testing ----------------------------- */

/*
 * The close button is the first traffic-light circle (leftmost):
 * center_x = win->x + UI_BORDER + 14,
 * center_y = win->y + UI_BORDER + UI_TBTN_Y_OFF,
 * radius    = UI_TBTN_R.
 * We accept a slightly larger hit area (radius + 4).
 */
static int hit_close_btn(const wm_window_t *w, int px, int py)
{
    int bx = w->x + WM_BORDER_PX + 14;
    int by = w->y + WM_BORDER_PX + UI_TBTN_Y_OFF;
    int dx = px - bx, dy = py - by;
    int r  = UI_TBTN_R + 4;
    return (dx * dx + dy * dy) <= (r * r);
}

static int hit_title_bar(const wm_window_t *w, int px, int py)
{
    return (px >= w->x && px < w->x + w->w &&
            py >= w->y && py < w->y + WM_BORDER_PX + WM_TITLE_BAR_H);
}

/* ----------------------------- mouse handling -------------------------- */

int wm_handle_mouse(int mx, int my, int buttons, int btn_changed)
{
    int left_down = (buttons & 1) && (btn_changed & 1);

    /* Drag in progress. */
    if (drag_win) {
        if (buttons & 1) {
            drag_win->x = mx - drag_offset_x;
            drag_win->y = my - drag_offset_y;
            /* Clamp: don't let title bar go off-screen. */
            if (drag_win->y < 0) drag_win->y = 0;
            return 1;
        } else {
            drag_win->flags &= ~WM_FLAG_DRAGGING;
            drag_win = NULL;
            return 1;
        }
    }

    if (left_down) {
        wm_window_t *hit = wm_find_window_at(mx, my);
        if (hit) {
            wm_bring_to_front(hit);

            if ((hit->flags & WM_FLAG_CLOSABLE) &&
                hit_close_btn(hit, mx, my)) {
                wm_destroy_window(hit);
                return 1;
            }

            if (hit_title_bar(hit, mx, my)) {
                drag_win = hit;
                drag_offset_x = mx - hit->x;
                drag_offset_y = my - hit->y;
                hit->flags |= WM_FLAG_DRAGGING;
                return 1;
            }

            /* Forward to app. */
            if (hit->handle_event) {
                struct input_event ev;
                ev.type = EVENT_MOUSE;
                ev.subtype = MOUSE_BTN_DOWN; /* Approximation since wm_handle_mouse only forwards clicks */
                ev.x = mx;
                ev.y = my;
                ev.keycode = buttons;
                hit->handle_event(hit, &ev);
            }
            return 1;
        }
    }

    prev_buttons = buttons;
    return 0;
}

/* ----------------------------- redraw ---------------------------------- */

void wm_redraw_all(void)
{
    for (wm_window_t *w = zlist_head; w; w = w->next) {
        if (!(w->flags & WM_FLAG_VISIBLE)) continue;
        int active = (w->flags & WM_FLAG_ACTIVE) ? 1 : 0;
        ui_draw_window(w->x, w->y, w->w, w->h, w->title, active, w->accent);
        if (w->draw_content)
            w->draw_content(w);
    }
}

wm_window_t *wm_get_bottom(void) { return zlist_head; }
wm_window_t *wm_get_top(void)    { return zlist_tail; }

/* Suppress unused-variable warning for kstrlen if compiler complains. */
static inline int dummy_use_kstrlen(void) { return kstrlen(""); }
