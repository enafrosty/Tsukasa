#include <stddef.h>
#include <stdint.h>

#include "boot/limine.h"
#include "boot/boot_info.h"
#include "cpu/gdt.h"
#include "cpu/idt.h"

#include "include/kprintf.h"
#include "include/smp.h"
#include "include/lapic.h"
#include "mm/pmm.h"
#include "mm/heap.h"
#include "mm/vmm_x64.h"

#include "drv/serial.h"
#include "drv/fb.h"
#include "drv/pic.h"
#include "drv/pit.h"
#include "drv/rtc.h"

#include "dev/pci.h"
#include "input/event.h"
#include "fs/vfs.h"
#include "net/network.h"

#include "gfx/blit.h"
#include "gfx/theme.h"
#include "gfx/desktop.h"

#include "loader/exec.h"
#include "proc/process.h"
#include "tty/tty.h"
#include "user/apps/registry.h"

#include "vga.h"

static void halt_forever(void)
{
    for (;;)
        __asm__ volatile ("hlt");
}

static void desktop_process_entry(void)
{
    if (fb_info.addr && fb_info.bpp == 32)
        desktop_run();
    process_exit(0);
}

/*
 * Run network stack initialization after scheduler start.
 * Pre-scheduler boot keeps IRQs disabled, which can stall lwIP timeout-based
 * waits; deferring avoids blocking desktop bring-up.
 */
static void network_bootstrap_entry(void)
{
    if (network_initialize_stack() == 0) {
        if (network_dhcp_acquire() == 0)
            kprintf("[boot:x64] network dhcp ok\n");
        else
            kprintf("[boot:x64] network dhcp pending/fail\n");
    } else {
        kprintf("[boot:x64] network stack init skipped (no nic)\n");
    }
    process_exit(0);
}

void kernel_main_x64(const struct tsukasa_boot_info *boot_info)
{
    serial_init();
    /* Keep IRQs masked until scheduler/runtime setup is complete. */
    __asm__ volatile ("cli");
    /* Normalize inherited flags state (clear DF/TF/NT). */
    __asm__ volatile (
        "pushfq\n"
        "popq %%rax\n"
        "andq $~((1<<10)|(1<<8)|(1<<14)), %%rax\n"
        "pushq %%rax\n"
        "popfq\n"
        :
        :
        : "rax", "memory");
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
    kprintf("[boot:x64] PMM free pages: %u / %u\n", (uint32_t)pmm_free_page_count(), (uint32_t)pmm_total_page_count());

    heap_init();
    {
        void *heap_probe = kmalloc(64);
        if (heap_probe) {
            kfree(heap_probe);
            kprintf("[boot:x64] heap_init done\n");
        } else {
            kprintf("[boot:x64] WARN: heap_init probe failed, using PMM fallbacks\n");
        }
    }

    gdt_init_x64();
    idt_init_x64();
    lapic_init();
    smp_init_bsp();
    uint32_t online_cpus = smp_init(smp_request.response);
    kprintf("[boot:x64] GDT/IDT ready\n");
    kprintf("[boot:x64] SMP online CPUs=%u\n", online_cpus);

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
    pit_init(100);
    pci_init();
    network_init();
    kprintf("[boot:x64] network stack init deferred until scheduler start\n");

    vfs_init(boot_info);

    kprintf("[boot:x64] process init...\n");
    process_init();
    kprintf("[boot:x64] tty init...\n");
    tty_init();
    user_apps_register_all();
    kprintf("[boot:x64] userspace app registry ready\n");

    kprintf("[boot:x64] spawn desktop...\n");
    {
        process_t *desktop_proc = process_spawn_kernel("desktop", desktop_process_entry);
        if (!desktop_proc) {
            kprintf("[boot:x64] WARN: failed to spawn desktop process\n");
        } else {
            uint64_t sp_phys = vmm_virt_to_phys((uintptr_t)desktop_proc->kernel_stack);
            kprintf("[boot:x64] desktop pid=%d stack_phys=0x%08x%08x\n",
                    (int)desktop_proc->pid,
                    (uint32_t)(sp_phys >> 32),
                    (uint32_t)(sp_phys & 0xFFFFFFFFu));
        }
    }
    {
        process_t *net_proc = process_spawn_kernel("net-bootstrap", network_bootstrap_entry);
        if (!net_proc)
            kprintf("[boot:x64] WARN: failed to spawn network bootstrap process\n");
    }
    {
        exec_entry_t shinit = NULL;
        if (exec_resolve_builtin("/bin/shinit", &shinit) == 0 && shinit) {
            if (!process_spawn_kernel("shell-init", shinit))
                kprintf("[boot:x64] WARN: failed to spawn shell-init\n");
        }
    }
    {
        exec_entry_t abi_test = NULL;
        if (exec_resolve_builtin("/bin/abi-test", &abi_test) == 0 && abi_test) {
            if (!process_spawn_kernel("abi-test", abi_test))
                kprintf("[boot:x64] WARN: failed to spawn abi-test\n");
        }
    }
    kprintf("[boot:x64] phase2 selftests spawn...\n");
    process_run_phase2_selftests();
    kprintf("[boot:x64] phase3 selftests spawn...\n");
    process_run_phase3_selftests();
    kprintf("[boot:x64] phase4 selftests spawn...\n");
    process_run_phase4_selftests();
    kprintf("[boot:x64] phase5 selftests spawn...\n");
    process_run_phase5_selftests();
    kprintf("[boot:x64] phase7 selftests spawn...\n");
    process_run_phase7_selftests();
    kprintf("[boot:x64] phase8 selftests spawn...\n");
    process_run_phase8_selftests();

    __asm__ volatile ("sti");
    kprintf("[boot:x64] interrupts enabled, preemptive scheduler active\n");
    process_start_scheduler();
}
