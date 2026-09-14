/*
 * Project Tsukasa — temporary minimal kernel to debug boot
 *
 * Copyright (C) 2025-2026 frosty (@enafrosty) and Project Tsukasa contributors.
 *
 * Project Tsukasa was created and is maintained by frosty (@enafrosty).
 * This program is free software: you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation, either version 3 of the License, or (at your
 * option) any later version. See the top-level LICENSE file.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 */

#include "vga.h"
#include "idt.h"
#include "mm/pmm.h"
#include "mm/heap.h"
#include "include/gdt.h"
#include "drv/fb.h"
#include "drv/pic.h"
#include "drv/serial.h"
#include "drv/rtc.h"
#include "gfx/blit.h"
#include "gfx/theme.h"
#include "input/event.h"
#include "fs/vfs.h"
#include "proc/task.h"
#include "proc/scheduler.h"
#include "include/paging.h"
#include "include/kprintf.h"
#include <stddef.h>
#include <stdint.h>

#define ENABLE_USER_TASK 0

extern char _user_stub_start[];
extern char _user_stack_top[];

/* Multiboot entry point from GRUB. */
void kernel_main(uint32_t magic, uint32_t info)
{
    (void)magic;
    (void)info;

    serial_init();
    kprintf("[boot] Tsukasa OS starting...\n");

    paging_init();
    kprintf("[boot] paging_init done\n");

    idt_init();
    kprintf("[boot] idt_init done\n");

    if (pmm_init((const void *)(uintptr_t)info) != 0) {
        kprintf("[boot] PMM init failed — halting\n");
        vga_puts_row(0, "Tsukasa: PMM init failed");
        for (;;)
            __asm__ volatile ("hlt");
    }
    heap_init();
    kprintf("[heap] TLSF pool ready\n");

    gdt_init();
    kprintf("[boot] gdt_init done\n");

    fb_init((const void *)(uintptr_t)info);
    if (fb_info.addr && fb_info.width && fb_info.height)
        paging_map_framebuffer((uintptr_t)fb_info.addr,
                              (size_t)fb_info.pitch * (size_t)fb_info.height);
    if (fb_info.addr && fb_info.bpp == 32)
        fb_fill_rect(0, 0, fb_info.width, fb_info.height, THEME_BG_TOP);
    kprintf("[boot] framebuffer %ux%u bpp=%u\n",
            fb_info.width, fb_info.height, fb_info.bpp);

    rtc_init();
    {
        rtc_time_t now;
        rtc_read(&now);
        kprintf("[rtc] %04u-%02u-%02u %02u:%02u:%02u UTC\n",
                (unsigned)now.year, (unsigned)now.month, (unsigned)now.day,
                (unsigned)now.hour, (unsigned)now.min,   (unsigned)now.sec);
    }

    /* Initialize input/events and remap the PIC so hardware IRQs (keyboard) use vectors 32+ instead of... */
    event_init();
    pic_init();

    vfs_init((const void *)(uintptr_t)info);

    task_init();

    extern void main_kernel_task(void);
    task_t *main_task = task_create(main_kernel_task);
    if (main_task)
        task_ready(main_task);

#if ENABLE_USER_TASK
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
