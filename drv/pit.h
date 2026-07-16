/*
 * pit.h - PIT timer driver for IRQ0 preemption ticks.
 */

#ifndef TSUKASA_PIT_H
#define TSUKASA_PIT_H

#include <stdint.h>

void pit_init(uint32_t hz);
void pit_irq_tick(void);
uint64_t pit_ticks(void);
uint32_t pit_frequency(void);

#endif /* TSUKASA_PIT_H */

