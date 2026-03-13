/*
 * wm.h  -  Window manager (z-ordered windows with drag/close/focus).
 *          Extended for modern theming: per-window opacity and accent color.
 */

#ifndef WM_H
#define WM_H

#include "blit.h"
#include <stdint.h>
#include <stddef.h>

#define WM_MAX_WINDOWS    16
#define WM_TITLE_MAX      48

#define WM_FLAG_VISIBLE   (1u << 0)
#define WM_FLAG_ACTIVE    (1u << 1)
#define WM_FLAG_DRAGGING  (1u << 2)
#define WM_FLAG_CLOSABLE  (1u << 3)

/* Must match UI_TITLE_H / UI_BORDER in ui.h */
#define WM_TITLE_BAR_H    28
#define WM_BORDER_PX       2
#define WM_BTN_W          18
#define WM_BTN_H          16
#define WM_BTN_GAP         2

/* Forward declaration. */
struct wm_window;
typedef struct wm_window wm_window_t;

/** App callback: draw content into window client area. */
typedef void (*wm_draw_fn)(wm_window_t *win);

/** App callback: handle an input event forwarded to this window. */
typedef void (*wm_event_fn)(wm_window_t *win, const void *event);

struct wm_window {
    int x, y, w, h;
    char title[WM_TITLE_MAX];
    uint32_t flags;

    /* Modern theming. */
    uint8_t  opacity;     /* 0..255, default 245 (near-opaque)            */
    uint32_t accent;      /* per-window accent color (0 = use global)      */

    /* Application callbacks. */
    wm_draw_fn   draw_content;
    wm_event_fn  handle_event;
    void         *app_data;

    /* Z-order doubly-linked list (head = bottom, tail = top). */
    wm_window_t *prev;
    wm_window_t *next;
};

/**
 * Initialize the window manager.
 */
void wm_init(void);

/**
 * Create a new window and add it to the top of the z-order.
 * @return Pointer to the window, or NULL on failure.
 */
wm_window_t *wm_create_window(int x, int y, int w, int h, const char *title,
                               wm_draw_fn draw_fn, wm_event_fn event_fn,
                               void *app_data);

/**
 * Destroy a window and remove it from the z-order.
 */
void wm_destroy_window(wm_window_t *win);

/**
 * Bring a window to the top of the z-order and set it active.
 */
void wm_bring_to_front(wm_window_t *win);

/**
 * Find the topmost window containing point (px, py).
 * @return Window pointer, or NULL if no window is under the point.
 */
wm_window_t *wm_find_window_at(int px, int py);

/**
 * Handle a mouse event at (mx, my) with button state.
 * Returns 1 if a window was affected, 0 otherwise.
 */
int wm_handle_mouse(int mx, int my, int buttons, int btn_changed);

/**
 * Redraw the entire desktop: all windows bottom-to-top.
 */
void wm_redraw_all(void);

/**
 * Get the head (bottom) of the z-order list.
 */
wm_window_t *wm_get_bottom(void);

/**
 * Get the tail (top/active) of the z-order list.
 */
wm_window_t *wm_get_top(void);

/**
 * Get the client area coordinates for a window.
 */
void wm_client_rect(const wm_window_t *win,
                    int *cx, int *cy, int *cw, int *ch);

#endif /* WM_H */
