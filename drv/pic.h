/*
 * pic.h - Programmable Interrupt Controller.
 */

#ifndef PIC_H
#define PIC_H

#include "../drv/ps2.h"

/**
 * Initialize PIC: remap IRQs to vectors 32-47, mask all except keyboard.
 */
void pic_init(void);

/**
 * Send End-Of-Interrupt to PIC.
 *
 * @param irq IRQ number (0-15).
 */
void pic_eoi(unsigned int irq);

#endif /* PIC_H */
