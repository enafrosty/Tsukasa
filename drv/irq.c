/*
 * irq.c - IRQ handler dispatch.
 */

#include "ps2kbd.h"
#include "ps2mouse.h"

void irq_handler(unsigned int vector)
{
    if (vector == 33)
        ps2kbd_handler();
    else if (vector == 44)
        ps2mouse_handler();
}
