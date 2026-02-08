/*
 * event.h - Input event types and buffer.
 */

#ifndef EVENT_H
#define EVENT_H

#include <stdint.h>

#define EVENT_KEY     0
#define EVENT_MOUSE  1

#define KEY_PRESS    1
#define KEY_RELEASE  0

#define EVENT_BUF_SIZE 64

struct input_event {
    uint8_t type;
    uint8_t subtype;
    uint16_t keycode;
    int16_t x;
    int16_t y;
};

/**
 * Initialize input event buffer.
 */
void event_init(void);

/**
 * Enqueue an event (called from ISR).
 */
int event_enqueue(const struct input_event *e);

/**
 * Dequeue an event (called from window server).
 *
 * @return 1 if event available, 0 if buffer empty.
 */
int event_dequeue(struct input_event *e);

#endif /* EVENT_H */
