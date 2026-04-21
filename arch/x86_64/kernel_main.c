#include <stddef.h>
#include <stdint.h>

#include "boot/boot_info.h"
#include "cpu/gdt.h"
#include "cpu/idt.h"

#include "include/kprintf.h"
#include "mm/pmm.h"
#include "mm/heap.h"
#include "mm/vmm_x64.h"

#include "drv/serial.h"
#include "drv/fb.h"
#include "drv/pic.h"
#include "drv/rtc.h"

#include "input/event.h"
#include "fs/vfs.h"

#include "gfx/blit.h"
#include "gfx/theme.h"
#include "gfx/desktop.h"

#include "vga.h"

static void halt_forever(void)
{
    for (;;) {
        __asm__ volatile ("hlt");
    }
}

void kernel_main_x64(const struct tsukasa_boot_info *boot_info)
{
    serial_init();
    kprintf("[boot:x64] Tsukasa x86_64 kernel starting\n");

    vmm_x64_init(boot_info ? boot_info->hhdm_offset : 0);
    kprintf("[boot:x64] hhdm=0x%08x%08x\n",
            (uint32_t)(vmm_x64_hhdm_offset() >> 32),
            (uint32_t)(vmm_x64_hhdm_offset() & 0xFFFFFFFFu));

    if (pmm_init(boot_info) != 0) {
        kprintf("[boot:x64] PMM init failed\n");
        vga_puts_row(0, "Tsukasa x64: PMM init failed");
        halt_forever();
    }

    heap_init();
    kprintf("[boot:x64] heap_init done\n");

    gdt_init_x64();
    idt_init_x64();
    kprintf("[boot:x64] GDT/IDT ready\n");

    if (fb_init(boot_info) == 0 && fb_info.addr && fb_info.bpp == 32) {
        uintptr_t fb_mapped = 0;
        size_t fb_size = (size_t)fb_info.pitch * (size_t)fb_info.height;

        if (boot_info)
            vmm_map_io_region(boot_info->framebuffer_addr, fb_size, &fb_mapped);
        if (fb_mapped)
            fb_info.addr = (void *)fb_mapped;

        fb_fill_gradient_v(0, 0,
                           (int)fb_info.width,
                           (int)fb_info.height,
                           THEME_BG_TOP,
                           THEME_BG_BOT);

        kprintf("[boot:x64] framebuffer %ux%u bpp=%u\n",
                fb_info.width,
                fb_info.height,
                fb_info.bpp);
    } else {
        kprintf("[boot:x64] framebuffer unavailable\n");
    }

    rtc_init();

    event_init();
    pic_init();

    vfs_init(boot_info);

    __asm__ volatile ("sti");
    kprintf("[boot:x64] interrupts enabled\n");

    if (fb_info.addr && fb_info.bpp == 32) {
        kprintf("[boot:x64] entering desktop loop\n");
        desktop_run();
    }

    kprintf("[boot:x64] no desktop path, halting\n");
    halt_forever();
}