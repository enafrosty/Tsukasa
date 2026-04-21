#ifndef TSUKASA_X64_IDT_H
#define TSUKASA_X64_IDT_H

#include <stdint.h>

void idt_init_x64(void);
void idt_exception_handler_x64(uint64_t vector, uint64_t error_code, uint64_t rip);

#endif