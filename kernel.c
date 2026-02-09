/*
 * kernel.c - temporary minimal kernel to debug boot.
 * Just writes a message to VGA text mode and halts.
 */

#include "vga.h"
#include "idt.h"
#include "mm/pmm.h"
#include "mm/heap.h"
#include "include/gdt.h"
#include "drv/fb.h"
#include "drv/pic.h"
#include "gfx/blit.h"
#include "gfx/font.h"
#include "input/event.h"
#include "proc/task.h"
#include "proc/scheduler.h"
#include "include/paging.h"
#include <stdint.h>

#define ENABLE_USER_TASK 0

extern char _user_stub_start[];
extern char _user_stack_top[];

/* Multiboot entry point from GRUB. */
void kernel_main(uint32_t magic, uint32_t info)
{
    (void)magic;
    (void)info;

    /* Stage 2: enable our page tables early. */
    paging_init();

    /* Stage 3: install IDT so exceptions are visible via idt_handler. */
    idt_init();

    /* Stage 4: initialize physical memory manager and heap. On failure,
       print an error and halt instead of continuing. */
    if (pmm_init((const void *)(uintptr_t)info) != 0) {
        vga_puts_row(0, "Tsukasa: PMM init failed");
        for (;;)
            __asm__ volatile ("hlt");
    }
    heap_init();

    /* Stage 5: set up our own GDT/TSS (uses stack_top from boot.s). */
    gdt_init();

    /* Stage 6: initialize framebuffer and draw a simple rectangle if available. */
    fb_init((const void *)(uintptr_t)info);
    if (fb_info.addr && fb_info.bpp == 32) {
        fb_fill_rect(0, 0, fb_info.width, fb_info.height, rgb(32, 32, 48));
        fb_fill_rect(100, 100, 200, 150, rgb(64, 64, 128));
    }

    /* Initialize input/events and remap the PIC so hardware IRQs (keyboard)
       use vectors 32+ instead of clobbering CPU exception vectors like 0x08. */
    event_init();
    pic_init();

    /* Stage 7: basic tasking and scheduler with a single kernel task. */
    task_init();

    extern void main_kernel_task(void);
    task_t *main_task = task_create(main_kernel_task);
    if (main_task)
        task_ready(main_task);

    /* Stage 8: (optionally) create a user-mode task using the user stub. */
#if ENABLE_USER_TASK
    /* Mark user stub and stack pages as user-accessible. */
    paging_map((uintptr_t)_user_stub_start, (uintptr_t)_user_stub_start,
               PTE_PRESENT | PTE_USER);
    paging_map((uintptr_t)_user_stack_top - 4096, (uintptr_t)_user_stack_top - 4096,
               PTE_PRESENT | PTE_WRITABLE | PTE_USER);

    task_t *user_task = task_create_user((uint32_t)(uintptr_t)_user_stub_start,
                                         (uint32_t)(uintptr_t)_user_stack_top);
    if (user_task)
        task_ready(user_task);
#endif

    __asm__ volatile ("sti");
    scheduler_run();

    vga_puts_row(0, "Tsukasa kernel reached!");

    for (;;) {
        __asm__ volatile ("hlt");
    }
}
