/*
 * Project Tsukasa — x86_64 Kernel Main Entry & Initialization
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
#include "drv/ps2mouse.h"

#include "dev/pci.h"
#include "input/event.h"
#include "fs/vfs.h"
#include "ipc/unix_socket.h"
#include "net/network.h"

#include "loader/exec.h"
#include "loader/elf64.h"
#include "proc/process.h"
#include "tty/tty.h"
#include "user/apps/registry.h"
#include "include/io.h"
#include "include/kutils.h"

#include "vga.h"

static void halt_forever(void)
{
    for (;;)
        __asm__ volatile ("hlt");
}

/*
 * Run network stack initialization after scheduler start.
 * Pre-scheduler boot keeps IRQs disabled, which can stall lwIP timeout-based
 * waits; deferring avoids blocking system bring-up.
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

extern void syscall_table_run_selftests(void);

static int is_selftest_requested(const struct tsukasa_boot_info *boot_info)
{
    if (boot_info && boot_info->cmdline) {
        if (k_strstr(boot_info->cmdline, "tsukasa.selftest=1"))
            return 1;
    }

    io_outw(0x510, 0x0000);
    char sig[4];
    for (int i = 0; i < 4; i++)
        sig[i] = (char)io_inb(0x511);

    if (sig[0] == 'Q' && sig[1] == 'E' && sig[2] == 'M' && sig[3] == 'U') {
        io_outw(0x510, 0x0019);
        uint32_t count = 0;
        count |= ((uint32_t)io_inb(0x511)) << 24;
        count |= ((uint32_t)io_inb(0x511)) << 16;
        count |= ((uint32_t)io_inb(0x511)) << 8;
        count |= (uint32_t)io_inb(0x511);

        for (uint32_t i = 0; i < count; i++) {
            (void)io_inb(0x511); (void)io_inb(0x511);
            (void)io_inb(0x511); (void)io_inb(0x511);

            uint16_t select = 0;
            select |= ((uint16_t)io_inb(0x511)) << 8;
            select |= (uint16_t)io_inb(0x511);

            (void)io_inb(0x511); (void)io_inb(0x511);

            char name[56];
            for (int j = 0; j < 56; j++)
                name[j] = (char)io_inb(0x511);

            if (k_strcmp(name, "opt/org.tsukasa.selftest") == 0 ||
                k_strcmp(name, "opt/tsukasa.selftest") == 0) {
                io_outw(0x510, select);
                char val = (char)io_inb(0x511);
                if (val == '1')
                    return 1;
            }
        }
    }

    return 0;
}

static int is_test_requested(const struct tsukasa_boot_info *boot_info)
{
    if (boot_info && boot_info->cmdline) {
        if (k_strstr(boot_info->cmdline, "tsukasa.test=1"))
            return 1;
    }

    io_outw(0x510, 0x0000);
    char sig[4];
    for (int i = 0; i < 4; i++)
        sig[i] = (char)io_inb(0x511);

    if (sig[0] == 'Q' && sig[1] == 'E' && sig[2] == 'M' && sig[3] == 'U') {
        io_outw(0x510, 0x0019);
        uint32_t count = 0;
        count |= ((uint32_t)io_inb(0x511)) << 24;
        count |= ((uint32_t)io_inb(0x511)) << 16;
        count |= ((uint32_t)io_inb(0x511)) << 8;
        count |= (uint32_t)io_inb(0x511);

        for (uint32_t i = 0; i < count; i++) {
            (void)io_inb(0x511); (void)io_inb(0x511);
            (void)io_inb(0x511); (void)io_inb(0x511);

            uint16_t select = 0;
            select |= ((uint16_t)io_inb(0x511)) << 8;
            select |= (uint16_t)io_inb(0x511);

            (void)io_inb(0x511); (void)io_inb(0x511);

            char name[56];
            for (int j = 0; j < 56; j++)
                name[j] = (char)io_inb(0x511);

            if (k_strcmp(name, "opt/org.tsukasa.test") == 0 ||
                k_strcmp(name, "opt/tsukasa.test") == 0) {
                io_outw(0x510, select);
                char val = (char)io_inb(0x511);
                if (val == '1')
                    return 1;
            }
        }
    }

    return 0;
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
    unix_socket_run_selftests();
    ps2mouse_init();

    kprintf("[boot:x64] process init...\n");
    process_init();
    kprintf("[boot:x64] tty init...\n");
    tty_init();
    user_apps_register_all();
    kprintf("[boot:x64] userspace app registry ready\n");

    int init_pid = -1;
    vfs_stat_t st;
    if (is_test_requested(boot_info)) {
        kprintf("[boot:x64] automated test runner mode enabled\n");
        if (vfs_stat("/fat12/TESTDRV.ELF", &st) == 0 && st.type != VFS_TYPE_DIR)
            init_pid = elf64_spawn("/fat12/TESTDRV.ELF", "testdrv");
        else if (vfs_stat("/bin/TESTDRV.ELF", &st) == 0 && st.type != VFS_TYPE_DIR)
            init_pid = elf64_spawn("/bin/TESTDRV.ELF", "testdrv");
        else if (vfs_stat("/fat12/testdrv.elf", &st) == 0 && st.type != VFS_TYPE_DIR)
            init_pid = elf64_spawn("/fat12/testdrv.elf", "testdrv");
        else if (vfs_stat("/bin/testdrv.elf", &st) == 0 && st.type != VFS_TYPE_DIR)
            init_pid = elf64_spawn("/bin/testdrv.elf", "testdrv");

        if (init_pid >= 0) {
            kprintf("[boot:x64] /fat12/TESTDRV.ELF running (pid=%d)\n", init_pid);
        } else {
            kprintf("[boot:x64] WARN: failed to spawn TESTDRV.ELF\n");
        }
    } else {
        kprintf("[boot:x64] handoff to /bin/init (PID 1)...\n");
        if (vfs_stat("/bin/init", &st) == 0 && st.type != VFS_TYPE_DIR) {
            init_pid = elf64_spawn("/bin/init", "/bin/init");
        } else if (vfs_stat("/init", &st) == 0 && st.type != VFS_TYPE_DIR) {
            init_pid = elf64_spawn("/init", "/init");
        } else if (vfs_stat("/sbin/init", &st) == 0 && st.type != VFS_TYPE_DIR) {
            init_pid = elf64_spawn("/sbin/init", "/sbin/init");
        }

        if (init_pid < 0) {
            exec_entry_t init_entry = NULL;
            if (exec_resolve_builtin("/bin/init", &init_entry) == 0 && init_entry) {
                process_t *init_proc = process_spawn_kernel("init", init_entry);
                if (init_proc) {
                    init_pid = (int)init_proc->pid;
                    process_set_cmdline(init_pid, "/bin/init");
                }
            }
        }

        if (init_pid >= 0) {
            kprintf("[boot:x64] /bin/init running (pid=%d)\n", init_pid);
        } else {
            kprintf("[boot:x64] WARN: failed to spawn /bin/init\n");
        }
    }

    {
        process_t *net_proc = process_spawn_kernel("net-bootstrap", network_bootstrap_entry);
        if (!net_proc)
            kprintf("[boot:x64] WARN: failed to spawn network bootstrap process\n");
    }

    if (is_selftest_requested(boot_info)) {
        kprintf("[boot:x64] automated selftest mode enabled\n");
        syscall_table_run_selftests();
    }
    /*
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
    */

    __asm__ volatile ("sti");
    kprintf("[boot:x64] interrupts enabled, preemptive scheduler active\n");
    process_start_scheduler();
}
