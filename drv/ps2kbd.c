/*
 * ps2kbd.c - PS/2 keyboard driver.
 */

#include "ps2.h"
#include "pic.h"
#include "../input/event.h"

/** Simple scan code to ASCII (make codes, set 1). High bit = release. */
static const unsigned char scan_to_key[] = {
    0, 0x1B, '1', '2', '3', '4', '5', '6', '7', '8', '9', '0', '-', '=', '\b',
    '\t', 'q', 'w', 'e', 'r', 't', 'y', 'u', 'i', 'o', 'p', '[', ']', '\n', 0,
    'a', 's', 'd', 'f', 'g', 'h', 'j', 'k', 'l', ';', '\'', '`', 0, '\\',
    'z', 'x', 'c', 'v', 'b', 'n', 'm', ',', '.', '/', 0, '*', 0, ' ',
};

void ps2kbd_handler(void)
{
    unsigned char sc = inb(PS2_DATA);
    unsigned char key;
    int press = !(sc & 0x80);
    unsigned int code = sc & 0x7F;

    if (code >= sizeof(scan_to_key) / sizeof(scan_to_key[0]))
        key = 0;
    else
        key = scan_to_key[code];

    struct input_event e = {
        .type = EVENT_KEY,
        .subtype = press ? KEY_PRESS : KEY_RELEASE,
        .keycode = key ? key : code,
        .x = 0, .y = 0,
    };
    event_enqueue(&e);
    pic_eoi(1);
}
