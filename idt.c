/*
 * idt.c - IDT table and initialization. Exception handlers write vector to VGA and halt.
 */

#include "idt.h"
#include "vga.h"

/* Packed IDT gate descriptor (8 bytes per entry). */
struct idt_entry {
    uint16_t offset_lo;
    uint16_t selector;
    uint8_t  zero;
    uint8_t  type_attr;  /* 0x8E = 32-bit interrupt gate, DPL=0, present */
    uint16_t offset_hi;
} __attribute__((packed));

struct idt_ptr {
    uint16_t limit;
    uint32_t base;
} __attribute__((packed));

static struct idt_entry idt[IDT_ENTRIES];

/* Assembly ISR stubs: isr_0 .. isr_31. */
extern void isr_0(void);
extern void isr_1(void);
extern void isr_2(void);
extern void isr_3(void);
extern void isr_4(void);
extern void isr_5(void);
extern void isr_6(void);
extern void isr_7(void);
extern void isr_8(void);
extern void isr_9(void);
extern void isr_10(void);
extern void isr_11(void);
extern void isr_12(void);
extern void isr_13(void);
extern void isr_14(void);
extern void isr_15(void);
extern void isr_16(void);
extern void isr_17(void);
extern void isr_18(void);
extern void isr_19(void);
extern void isr_20(void);
extern void isr_21(void);
extern void isr_22(void);
extern void isr_23(void);
extern void isr_24(void);
extern void isr_25(void);
extern void isr_26(void);
extern void isr_27(void);
extern void isr_28(void);
extern void isr_29(void);
extern void isr_30(void);
extern void isr_31(void);
extern void isr_128(void);
extern void isr_ignore(void);
extern void isr_33(void);

static void (*const isr_stubs[])(void) = {
    isr_0, isr_1, isr_2, isr_3, isr_4, isr_5, isr_6, isr_7,
    isr_8, isr_9, isr_10, isr_11, isr_12, isr_13, isr_14, isr_15,
    isr_16, isr_17, isr_18, isr_19, isr_20, isr_21, isr_22, isr_23,
    isr_24, isr_25, isr_26, isr_27, isr_28, isr_29, isr_30, isr_31,
};

static void set_gate(unsigned int i, void (*handler)(void))
{
    uint32_t addr = (uint32_t)handler;
    idt[i].offset_lo = (uint16_t)(addr & 0xFFFF);
    idt[i].selector  = 0x08;   /* kernel code segment (GRUB GDT) */
    idt[i].zero      = 0;
    idt[i].type_attr = 0x8E;   /* 32-bit interrupt gate, DPL=0, present */
    idt[i].offset_hi = (uint16_t)((addr >> 16) & 0xFFFF);
}

static void set_gate_user(unsigned int i, void (*handler)(void))
{
    uint32_t addr = (uint32_t)handler;
    idt[i].offset_lo = (uint16_t)(addr & 0xFFFF);
    idt[i].selector  = 0x08;
    idt[i].zero      = 0;
    idt[i].type_attr = 0xEE;   /* DPL=3 so user can call int 0x80 */
    idt[i].offset_hi = (uint16_t)((addr >> 16) & 0xFFFF);
}

void idt_init(void)
{
    struct idt_ptr ptr;
    unsigned int i;

    for (i = 0; i < 32; i++)
        set_gate(i, isr_stubs[i]);
    for (i = 32; i < 33; i++)
        set_gate(i, isr_ignore);
    set_gate(33, isr_33);
    for (i = 34; i < 128; i++)
        set_gate(i, isr_ignore);
    set_gate_user(128, isr_128);
    for (i = 129; i < IDT_ENTRIES; i++)
        set_gate(i, isr_ignore);

    ptr.limit = (uint16_t)(sizeof(idt) - 1);
    ptr.base  = (uint32_t)&idt;

    __asm__ volatile ("lidt %0" : : "m"(ptr));
}

/* Convert nibble to hex character. */
static char hex_char(unsigned int n)
{
    return (char)(n < 10 ? '0' + n : 'A' + n - 10);
}

void idt_handler(uint32_t vector, uint32_t error_code)
{
    /* Disable interrupts and write exception info to VGA row 1. */
    __asm__ volatile ("cli");

    /* "Ex=XX Err=XXXXXXXX" at start of second row. */
    VGA_BUFFER[80 + 0]  = (VGA_ATTR << 8) | 'E';
    VGA_BUFFER[80 + 1]  = (VGA_ATTR << 8) | 'x';
    VGA_BUFFER[80 + 2]  = (VGA_ATTR << 8) | '=';
    VGA_BUFFER[80 + 3]  = (VGA_ATTR << 8) | hex_char((vector >> 4) & 0xF);
    VGA_BUFFER[80 + 4]  = (VGA_ATTR << 8) | hex_char(vector & 0xF);
    VGA_BUFFER[80 + 5]  = (VGA_ATTR << 8) | ' ';
    VGA_BUFFER[80 + 6]  = (VGA_ATTR << 8) | 'E';
    VGA_BUFFER[80 + 7]  = (VGA_ATTR << 8) | 'r';
    VGA_BUFFER[80 + 8]  = (VGA_ATTR << 8) | 'r';
    VGA_BUFFER[80 + 9]  = (VGA_ATTR << 8) | '=';
    VGA_BUFFER[80 + 10] = (VGA_ATTR << 8) | hex_char((error_code >> 28) & 0xF);
    VGA_BUFFER[80 + 11] = (VGA_ATTR << 8) | hex_char((error_code >> 24) & 0xF);
    VGA_BUFFER[80 + 12] = (VGA_ATTR << 8) | hex_char((error_code >> 20) & 0xF);
    VGA_BUFFER[80 + 13] = (VGA_ATTR << 8) | hex_char((error_code >> 16) & 0xF);
    VGA_BUFFER[80 + 14] = (VGA_ATTR << 8) | hex_char((error_code >> 12) & 0xF);
    VGA_BUFFER[80 + 15] = (VGA_ATTR << 8) | hex_char((error_code >> 8) & 0xF);
    VGA_BUFFER[80 + 16] = (VGA_ATTR << 8) | hex_char((error_code >> 4) & 0xF);
    VGA_BUFFER[80 + 17] = (VGA_ATTR << 8) | hex_char(error_code & 0xF);

    for (;;)
        __asm__ volatile ("hlt");
}
