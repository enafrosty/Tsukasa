/*
 * pic.h - Programmable Interrupt Controller.
 */

#ifndef PIC_H
#define PIC_H

#include <stdint.h>

#include "../drv/ps2.h"

/**
 * Initialize PIC: remap IRQs to vectors 32-47 and unmask timer/keyboard/cascade.
 */
void pic_init(void);

/**
 * Send End-Of-Interrupt to PIC.
 *
 * @param irq IRQ number (0-15).
 */
void pic_eoi(unsigned int irq);

void pic_mask_irq(uint8_t irq);
void pic_unmask_irq(uint8_t irq);

#endif /* PIC_H */
