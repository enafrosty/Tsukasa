#include <stdint.h>
#include <stddef.h>

#include "idt.h"
#include "include/kprintf.h"
#include "drv/fb.h"
#include "drv/serial.h"
#include "gfx/blit.h"
#include "gfx/font.h"
#include "vga.h"

#define IDT_X64_ENTRIES 256

struct idt_entry {
    uint16_t offset_low;
    uint16_t selector;
    uint8_t ist;
    uint8_t type_attr;
    uint16_t offset_mid;
    uint32_t offset_high;
    uint32_t reserved;
} __attribute__((packed));

struct idt_ptr {
    uint16_t limit;
    uint64_t base;
} __attribute__((packed));

static struct idt_entry idt[IDT_X64_ENTRIES];

extern void isr_x64_0(void);
extern void isr_x64_1(void);
extern void isr_x64_2(void);
extern void isr_x64_3(void);
extern void isr_x64_4(void);
extern void isr_x64_5(void);
extern void isr_x64_6(void);
extern void isr_x64_7(void);
extern void isr_x64_8(void);
extern void isr_x64_9(void);
extern void isr_x64_10(void);
extern void isr_x64_11(void);
extern void isr_x64_12(void);
extern void isr_x64_13(void);
extern void isr_x64_14(void);
extern void isr_x64_15(void);
extern void isr_x64_16(void);
extern void isr_x64_17(void);
extern void isr_x64_18(void);
extern void isr_x64_19(void);
extern void isr_x64_20(void);
extern void isr_x64_21(void);
extern void isr_x64_22(void);
extern void isr_x64_23(void);
extern void isr_x64_24(void);
extern void isr_x64_25(void);
extern void isr_x64_26(void);
extern void isr_x64_27(void);
extern void isr_x64_28(void);
extern void isr_x64_29(void);
extern void isr_x64_30(void);
extern void isr_x64_31(void);
extern void isr_x64_33(void);
extern void isr_x64_44(void);
extern void isr_x64_ignore(void);

static void (*const exception_stubs[32])(void) = {
    isr_x64_0, isr_x64_1, isr_x64_2, isr_x64_3,
    isr_x64_4, isr_x64_5, isr_x64_6, isr_x64_7,
    isr_x64_8, isr_x64_9, isr_x64_10, isr_x64_11,
    isr_x64_12, isr_x64_13, isr_x64_14, isr_x64_15,
    isr_x64_16, isr_x64_17, isr_x64_18, isr_x64_19,
    isr_x64_20, isr_x64_21, isr_x64_22, isr_x64_23,
    isr_x64_24, isr_x64_25, isr_x64_26, isr_x64_27,
    isr_x64_28, isr_x64_29, isr_x64_30, isr_x64_31,
};

static void set_gate(uint8_t vec, void (*handler)(void), uint8_t flags)
{
    uint64_t addr = (uint64_t)(uintptr_t)handler;
    idt[vec].offset_low = (uint16_t)(addr & 0xFFFFu);
    idt[vec].selector = 0x08u;
    idt[vec].ist = 0u;
    idt[vec].type_attr = flags;
    idt[vec].offset_mid = (uint16_t)((addr >> 16) & 0xFFFFu);
    idt[vec].offset_high = (uint32_t)((addr >> 32) & 0xFFFFFFFFu);
    idt[vec].reserved = 0u;
}

static void draw_exception_banner(uint64_t vector, uint64_t error_code, uint64_t rip, uint64_t cr2)
{
    char line1[96];
    char line2[96];

    ksprintf(line1, sizeof(line1),
             "KEX vec=%u err=%08x%08x",
             (uint32_t)vector,
             (uint32_t)(error_code >> 32),
             (uint32_t)(error_code & 0xFFFFFFFFu));

    ksprintf(line2, sizeof(line2),
             "rip=%08x%08x cr2=%08x%08x",
             (uint32_t)(rip >> 32),
             (uint32_t)(rip & 0xFFFFFFFFu),
             (uint32_t)(cr2 >> 32),
             (uint32_t)(cr2 & 0xFFFFFFFFu));

    if (fb_info.addr && fb_info.bpp == 32) {
        fb_fill_rect(0, 0, (int)fb_info.width, 40, rgb(120, 0, 0));
        fb_draw_string(8, 8, line1, rgb(255, 255, 255), rgb(120, 0, 0));
        fb_draw_string(8, 20, line2, rgb(255, 255, 255), rgb(120, 0, 0));
    }

    vga_puts_row(0, "Tsukasa x64 exception (see serial)");
}

void idt_exception_handler_x64(uint64_t vector, uint64_t error_code, uint64_t rip)
{
    uint64_t cr2 = 0;

    __asm__ volatile ("mov %%cr2, %0" : "=r"(cr2));
    __asm__ volatile ("cli");

    kprintf("[x64][exc] vec=%u err=0x%08x%08x rip=0x%08x%08x cr2=0x%08x%08x\n",
            (uint32_t)vector,
            (uint32_t)(error_code >> 32),
            (uint32_t)(error_code & 0xFFFFFFFFu),
            (uint32_t)(rip >> 32),
            (uint32_t)(rip & 0xFFFFFFFFu),
            (uint32_t)(cr2 >> 32),
            (uint32_t)(cr2 & 0xFFFFFFFFu));

    draw_exception_banner(vector, error_code, rip, cr2);

    for (;;) {
        __asm__ volatile ("hlt");
    }
}

void idt_init_x64(void)
{
    struct idt_ptr ptr;

    for (uint32_t i = 0; i < 32; i++)
        set_gate((uint8_t)i, exception_stubs[i], 0x8Eu);

    for (uint32_t i = 32; i < IDT_X64_ENTRIES; i++)
        set_gate((uint8_t)i, isr_x64_ignore, 0x8Eu);

    set_gate(33, isr_x64_33, 0x8Eu);
    set_gate(44, isr_x64_44, 0x8Eu);

    ptr.limit = (uint16_t)(sizeof(idt) - 1);
    ptr.base = (uint64_t)(uintptr_t)&idt;

    __asm__ volatile ("lidt %0" : : "m"(ptr));
}