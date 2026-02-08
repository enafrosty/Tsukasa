/*
 * event.c - Input event ring buffer.
 */

#include "event.h"

static struct input_event event_buf[EVENT_BUF_SIZE];
static unsigned int event_head;
static unsigned int event_tail;
static unsigned int event_count;

void event_init(void)
{
    event_head = 0;
    event_tail = 0;
    event_count = 0;
}

int event_enqueue(const struct input_event *e)
{
    if (!e || event_count >= EVENT_BUF_SIZE)
        return -1;
    event_buf[event_tail] = *e;
    event_tail = (event_tail + 1) % EVENT_BUF_SIZE;
    event_count++;
    return 0;
}

int event_dequeue(struct input_event *e)
{
    if (!e || event_count == 0)
        return 0;
    *e = event_buf[event_head];
    event_head = (event_head + 1) % EVENT_BUF_SIZE;
    event_count--;
    return 1;
}
