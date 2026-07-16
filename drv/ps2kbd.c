/*
 * ps2kbd.c - PS/2 keyboard driver.
 */

#include "ps2.h"
#include "pic.h"
#include "../input/event.h"
#ifdef __x86_64__
#include "../tty/tty.h"
#endif

/** Simple scan code to ASCII (make codes, set 1). High bit = release. */
static const unsigned char scan_to_key[] = {
    0, 0x1B, '1', '2', '3', '4', '5', '6', '7', '8', '9', '0', '-', '=', '\b',
    '\t', 'q', 'w', 'e', 'r', 't', 'y', 'u', 'i', 'o', 'p', '[', ']', '\n', 0,
    'a', 's', 'd', 'f', 'g', 'h', 'j', 'k', 'l', ';', '\'', '`', 0, '\\',
    'z', 'x', 'c', 'v', 'b', 'n', 'm', ',', '.', '/', 0, '*', 0, ' ',
};

static uint8_t g_ext_prefix;

void ps2kbd_handler(void)
{
    unsigned char sc = inb(PS2_DATA);
    unsigned char key = 0;
    int press;
    unsigned int code;
    uint32_t keycode;
    struct input_event e;

    if (sc == 0xE0) {
        g_ext_prefix = 1;
        pic_eoi(1);
        return;
    }

    press = ((sc & 0x80u) == 0u);
    code = sc & 0x7Fu;

    if (g_ext_prefix && code == 0x2A) {
        /* Part of PrintScreen make/release sequence, ignore pseudo-shift. */
        g_ext_prefix = 0;
        pic_eoi(1);
        return;
    }

    if (g_ext_prefix && code == 0x37) {
        keycode = INPUT_KEY_PRINTSCREEN;
    } else {
        if (code < sizeof(scan_to_key) / sizeof(scan_to_key[0]))
            key = scan_to_key[code];
        keycode = key ? (uint32_t)key : (uint32_t)(g_ext_prefix ? (0xE000u | code) : code);
    }

    e.event_id = press ? INPUT_EVENT_KEY : INPUT_EVENT_KEYUP;
    e.type = EVENT_KEY;
    e.subtype = press ? KEY_PRESS : KEY_RELEASE;
    e.keycode = keycode;
    e.x = 0;
    e.y = 0;
    e.wheel_delta = 0;
    e.width = 0;
    e.height = 0;
    e.modifiers = 0;
    e.window_id = -1;

#ifdef __x86_64__
    if (!g_ext_prefix)
        tty_handle_scancode((uint8_t)code, press);
#endif
    event_enqueue(&e);
    g_ext_prefix = 0;
    pic_eoi(1);
}
