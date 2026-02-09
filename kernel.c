/*
 * kernel.c - Minimal kernel: prints "Hello from Windows 10!" to VGA text buffer.
 * VGA text mode buffer is at physical address 0xB8000.
 * Each character: 2 bytes = [ASCII] [attribute]. Attribute 0x07 = light grey on black.
 */

#include "idt.h"
#include "vga.h"
#include "mm/pmm.h"
#include "include/paging.h"
#include "mm/heap.h"
#include "proc/task.h"
#include "proc/scheduler.h"
#include "include/gdt.h"
#include "include/multiboot.h"
#include "drv/fb.h"
#include "drv/pic.h"
#include "gfx/blit.h"
#include "gfx/font.h"
#include "ipc/shm.h"
#include "input/event.h"
#include "fs/vfs.h"
#include <stddef.h>
#include <stdint.h>

extern char _user_stub_start[];
extern char _user_stack_top[];

/** Entry point for the main kernel task (runs in scheduler). */
static void run_task(void)
{
    vga_puts_row(0, "Hello from Windows 10!");

    if (fb_info.addr && fb_info.bpp == 32) {
        fb_fill_rect(0, 0, fb_info.width, fb_info.height, rgb(32, 32, 48));
        fb_fill_rect(100, 100, 200, 150, rgb(64, 64, 128));
        fb_fill_rect(110, 110, 180, 130, rgb(200, 200, 220));
        fb_draw_string(120, 140, "Hello World", rgb(0, 0, 0), rgb(200, 200, 220));
    }

    for (;;) {
        __asm__ volatile ("hlt");
        task_yield();
    }
}

/* Multiboot magic and info pointer from GRUB (passed in eax/ebx; cdecl: first arg = magic, second = info). */
void kernel_main(uint32_t magic, uint32_t info)
{
    (void)magic;
    /* Switch to our page tables first. GRUB (BIOS or UEFI) may leave paging on with
     * its own tables; any kernel memory access (idt_init, multiboot info) must happen
     * after we have identity map for 0-4 MiB. */
    paging_init();
    idt_init();

    if (pmm_init((const void *)(uintptr_t)info) != 0) {
        vga_puts_row(1, "PMM: no memory map");
        vga_puts_row(0, "Hello from Windows 10!");
        for (;;)
            __asm__ volatile ("hlt");
    }
    heap_init();
    gdt_init();
    fb_init((const void *)(uintptr_t)info);
    event_init();
    pic_init();
    vfs_init((const void *)(uintptr_t)info);
    task_init();

    /* Mark user stub and stack pages as user-accessible. */
    paging_map((uintptr_t)_user_stub_start, (uintptr_t)_user_stub_start,
               PTE_PRESENT | PTE_USER);
    paging_map((uintptr_t)_user_stack_top - 4096, (uintptr_t)_user_stack_top - 4096,
               PTE_PRESENT | PTE_WRITABLE | PTE_USER);

    task_t *main_task = task_create(run_task);
    if (main_task)
        task_ready(main_task);

    task_t *user_task = task_create_user((uint32_t)(uintptr_t)_user_stub_start,
                                         (uint32_t)(uintptr_t)_user_stack_top);
    if (user_task)
        task_ready(user_task);

    __asm__ volatile ("sti");
    scheduler_run();
}
