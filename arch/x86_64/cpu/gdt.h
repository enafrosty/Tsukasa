#ifndef TSUKASA_X64_GDT_H
#define TSUKASA_X64_GDT_H

#include <stdint.h>

#define X64_GDT_KERNEL_CS 0x08
#define X64_GDT_KERNEL_DS 0x10
#define X64_GDT_USER_DS   0x1B
#define X64_GDT_USER_CS   0x23
#define X64_GDT_TSS       0x28

void gdt_init_x64(void);
void tss_set_rsp0_x64(uint64_t rsp0);

#endif