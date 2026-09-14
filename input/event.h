/*
 * Project Tsukasa — Normalized input/gui event contract and queue API
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

struct gui_event {
    uint16_t event_id;

    /* Legacy compatibility fields: type/subtype retain the old EVENT_KEY/EVENT_MOUSE model. */
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

typedef struct gui_event gui_event_t;

void event_init(void);

/* Enqueue an event (called from IRQ handlers and desktop internals). */
int event_enqueue(const struct gui_event *e);

/* Dequeue one event. @return 1 if event available, 0 if buffer empty. */
int event_dequeue(struct gui_event *e);

#endif /* EVENT_H */
