/*
 * ps2mouse.h - PS/2 mouse driver.
 */

#ifndef PS2MOUSE_H
#define PS2MOUSE_H

/**
 * Initialize the PS/2 mouse (enable second port, set sample rate, enable
 * data reporting). Must be called after pic_init().
 */
void ps2mouse_init(void);

/**
 * IRQ 12 handler. Called from irq_handler when vector == 44.
 */
void ps2mouse_handler(void);

#endif /* PS2MOUSE_H */
