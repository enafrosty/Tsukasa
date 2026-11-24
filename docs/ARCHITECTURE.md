# Project Tsukasa Architecture Overview

Project Tsukasa is a monolithic `x86_64` kernel paired with a modular userspace runtime, UNIX utility suite, and a decoupled client-server graphical desktop environment (**Project Vanilla**).

---

## High-Level System Diagram

```text
+-----------------------------------------------------------------------------+
|                            Userspace Applications                           |
|  Vanilla Desktop: [shell] [terminal] [filemgr] [notepad] [calc] [taskmgr]   |
|                   [settings] [devtools] [supervisord] [registryd/clip/notif]|
|  Coreutils/Tools: [ls] [cat] [grep] [ps] [tsh] [fdisk] [mkfs.*] [installer] |
|  Ports:           [doom] [lua/luac 5.4.7] [tcc]                             |
+-----------------------------------------------------------------------------+
                                      |
                                      | tsukasa-libc (x86_64 SYSCALL/SYSRET)
                                      v
+-----------------------------------------------------------------------------+
|                           Kernel Syscall Gateway                            |
|  Flat POSIX Table (syscall/syscall_table.c) + Domain Dispatch (syscall.c)   |
|  Mandatory vmm_validate_user_ptr() check, interruptible sti dispatch        |
+-----------------------------------------------------------------------------+
           |                          |                          |
           v                          v                          v
+--------------------+    +------------------------+    +---------------------+
| Process & Sched    |    | VFS & Page Cache       |    | IPC & Subsystems    |
| - 256-band Sched   |    | - Locked VFS + dcache  |    | - AF_UNIX Sockets   |
| - int 0x40 Yield   |    | - Bounded LRU Cache    |    | - Shared Mem (SHM)  |
| - Wait/Work Queues |    | - tmpfs, ext4, FAT32   |    | - VFS Pipes & TTY   |
| - Signals & Futex  |    | - DevFS, ProcFS, SysFS |    | - Net (lwIP 2.x)    |
+--------------------+    +------------------------+    +---------------------+
           |                          |                          |
           +--------------------------+--------------------------+
                                      v
+-----------------------------------------------------------------------------+
|                      Memory & Hardware Drivers (x86_64)                     |
|  PMM Bitmap | 4-Level VMM + HHDM + PAT WC | TLSF Heap + SLAB | GPT + MBR    |
|  ACPI/MADT/MCFG | IOAPIC/LAPIC/PIC | PCIe ECAM + MSI-X | PIT/RTC            |
|  AHCI SATA | VirtIO-blk | NVMe | ATA PIO | E1000 | VirtIO-net | PS/2 | UART |
+-----------------------------------------------------------------------------+
                                      |
                                      v
+-----------------------------------------------------------------------------+
|                    Bootloader: Limine v8 (BIOS & UEFI)                      |
|               Loads tsukasa_x64.elf + initrd.tar.lz4 in Long Mode           |
+-----------------------------------------------------------------------------+
```

---

## Kernel Initialization Sequence (`arch/x86_64/kernel_main.c`)

1. `serial_init()`: Early COM1 diagnostic UART (`0x3F8`, `115200 8N1`) and kernel cmdline log flags.
2. `vmm_x64_init(hhdm_offset)`: Registers the Limine Higher-Half Direct Map (HHDM).
3. `pmm_init(boot_info)`: Parses the Limine memory map, builds the 4 KiB frame bitmap, and reserves low memory, kernel sections, and boot modules.
4. `heap_init()`: Initializes the TLSF kernel heap and SLAB caches.
5. `gdt_init_x64()`, `idt_init_x64()`, `gdbstub_init()`, `lapic_init()`, `smp_init()`: Configures Ring 0/3 GDT descriptors, per-CPU TSS (`RSP0` and `IST1` for `#DF`/`#NMI`), 256-vector IDT, optional COM2 GDB stub, Local APIC, and AP startup (`work_queue_drain_loop()`).
6. `acpi_init(boot_info)`: Parses RSDP, `RSDT`/`XSDT`, `MADT`, `FADT` (`PM1a` shutdown), and `MCFG`.
7. `fb_init(boot_info)` + `vmm_map_wc_region()`: Maps the 32bpp linear framebuffer with PAT Write-Combining (`MMU_FLAG_WC`).
8. `rtc_init()`, `pic_init()`, `ioapic_init()`, `pit_init(100)`, `pci_init()`, `network_init()`: Sets up CMOS clock, multi-IOAPIC routing + PIC masking, 100 Hz PIT tick, PCI/PCIe ECAM enumeration, and NIC driver probing (Intel E1000, VirtIO-net).
9. `vfs_init(boot_info)`: Initializes LRU page cache, dcache, `flock`, synthetic filesystems (`devfs`, `procfs`, `sysfs`, `bootfs`), probes AHCI/VirtIO-blk/NVMe (with ATA fallback) and GPT/MBR partitions, mounts either persistent `ext4` root (`root=/dev/*` or `root=PARTUUID=*`) + `/boot` ESP or live `tmpfs` unpacked from `initrd.tar.lz4`, plus `/tmp`, `/var/run`, `/mnt/*`, `/disk`, `/dev`, `/proc`, `/sys`.
10. `ps2mouse_init()`, `process_init()`, `tty_init()`, `user_apps_register_all()`: Initializes unified input event queue (`/dev/input/events`), PCBs/scheduler, and built-in fallbacks.
11. Spawns PID 1 (`/bin/init`, launching `/bin/vsrv`, `/bin/supervisord`, `shell`, apps, or `/bin/TESTDRV.ELF` under `opt/org.tsukasa.test=1`), spawns `net-bootstrap`, optionally runs `syscall_table_run_selftests()` (`opt/org.tsukasa.selftest=1`), enables interrupts, and starts the preemptive scheduler.

---

## Virtual Address Space Layout (`x86_64`)

| Virtual Range | Region | Access |
| :--- | :--- | :--- |
| `0x0000_0000_0040_0000`+ | Default Ring-3 ELF load base, data, heap, mmap/SHM | User (R/W/X as mapped) |
| `0x0000_7FFF_F000_0000` (top) | Default Ring-3 user stack (8 MiB) | User (RW-) |
| `0xFFFF_8000_0000_0000`+ | Higher-Half Direct Map (HHDM) | Supervisor (RW-) |
| `0xFFFF_FFFF_8000_0000`+ | Kernel `.text`, `.rodata`, `.data`, `.bss` | Supervisor (R-X / RW-) |
| `0xFFFF_FFFF_9000_0000`+ | Kernel dynamic heap / virtual pools | Supervisor (RW-) |
| `0xFFFF_FFFF_F000_0000`+ | MMIO & PAT Write-Combining framebuffer mappings | Supervisor (Device / WC) |

---

## Project Vanilla Display & Service Architecture

```text
+-------------------------------------------------------------------+
| /bin/init (PID 1)                                                 |
|  |-- /bin/vsrv         -> owns /dev/fb0 (PAT WC) & /dev/input/*   |
|  |                        serves /tmp/vanilla.sock + SHM surfaces |
|  |-- /bin/supervisord  -> reads /etc/services.conf:               |
|  |     |-- /bin/registryd.elf                                     |
|  |     |-- /bin/clipboardd.elf                                    |
|  |     |-- /bin/notifyd.elf                                       |
|  |     `-- /bin/shell.elf (statusbar, dock, launcher, menus)      |
|  `-- User Apps: terminal (with tsh), filemgr, notepad, calc,      |
|                 taskmgr, settings, devtools                       |
+-------------------------------------------------------------------+
```

For deeper subsystem documentation, see the [Project Tsukasa GitHub Wiki](https://github.com/enafrosty/Tsukasa/wiki) and [docs/AUTOMATED_TESTING.md](AUTOMATED_TESTING.md).
