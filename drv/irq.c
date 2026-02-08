/*
 * irq.c - IRQ handler dispatch.
 */

#include "ps2kbd.h"

void irq_handler(unsigned int vector)
{
    if (vector == 33)
        ps2kbd_handler();
}
