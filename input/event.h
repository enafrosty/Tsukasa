/*
 * event.h - Normalized input/gui event contract and queue API.
 *
 * Event IDs intentionally mirror SYS_GUI GUI_EVENT_* values.
 */

#ifndef EVENT_H
#define EVENT_H

#include <stdint.h>

/* Normalized event IDs (stable payload routing contract). */
#define INPUT_EVENT_NONE        0
#define INPUT_EVENT_PAINT       1
#define INPUT_EVENT_CLICK       2
#define INPUT_EVENT_RIGHT_CLICK 3
#define INPUT_EVENT_CLOSE       4
#define INPUT_EVENT_KEY         5
#define INPUT_EVENT_MOUSE_DOWN  6
#define INPUT_EVENT_MOUSE_UP    7
#define INPUT_EVENT_MOUSE_MOVE  8
#define INPUT_EVENT_MOUSE_WHEEL 9
#define INPUT_EVENT_KEYUP       10
#define INPUT_EVENT_RESIZE      11

/* Backward-compatible aliases used by existing in-kernel apps. */
#define EVENT_KEY   0
#define EVENT_MOUSE 1

#define KEY_PRESS   1
#define KEY_RELEASE 0

#define MOUSE_MOVE     2
#define MOUSE_BTN_DOWN 3
#define MOUSE_BTN_UP   4

#define MOUSE_BUTTON_LEFT   1
#define MOUSE_BUTTON_RIGHT  2
#define MOUSE_BUTTON_MIDDLE 4

/* Extended keycode for PrintScreen (set-1 extended make code). */
#define INPUT_KEY_PRINTSCREEN 0xE037u

#define EVENT_BUF_SIZE 128

struct input_event {
    /* Normalized event ID used by SYS_GUI and new desktop routing. */
    uint16_t event_id;

    /*
     * Legacy compatibility fields:
     *   - type/subtype retain the old EVENT_KEY/EVENT_MOUSE model.
     *   - keycode carries ASCII or scan/virtual key payload as before.
     */
    uint8_t type;
    uint8_t subtype;

    uint32_t keycode;
    int32_t x;
    int32_t y;
    int32_t wheel_delta;
    int32_t width;
    int32_t height;
    uint32_t modifiers;
    int32_t window_id;
};

/**
 * Initialize input event buffer.
 */
void event_init(void);

/**
 * Enqueue an event (called from IRQ handlers and desktop internals).
 *
 * Ordering guarantee:
 *   FIFO order is preserved for retained events.
 * Starvation control:
 *   queue pressure coalesces low-value motion/paint events first.
 */
int event_enqueue(const struct input_event *e);

/**
 * Dequeue one event.
 *
 * @return 1 if event available, 0 if buffer empty.
 */
int event_dequeue(struct input_event *e);

#endif /* EVENT_H */
