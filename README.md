
  

![logo](https://i.imgur.com/PRzEoKE.png)

  
Tsukasa is a freestanding operating system written in C and x86_64 assembly. The system is designed without standard runtime dependencies, implementing its own boot protocol handlers, virtual memory manager, preemptive scheduler, POSIX system call interface, C runtime library, display server, userland coreutils, and port infrastructure.


  
The operating system boots primarily on 64-bit x86 hardware through the Limine bootloader protocol, while retaining a legacy 32-bit Multiboot interface for compatibility testing.

  

---

  

## Current Architecture and Status

  

Tsukasa has expanded from an early kernel prototype into a multi-package operating system environment. The codebase is organized into a modular monolithic kernel, an independent standard C library (`tsukasa-libc`), a complete userland utility suite (`tsukasa-coreutils`), a window server and compositing subsystem (`vanilla`), disk management tooling (`tsukasa-disktools`), and an ecosystem of ported third-party software (`tsukasa-ports`).

  

### Core Subsystems

  

*  **Boot and Hardware Bringup**: Boots in 64-bit Long Mode via Limine v8. Parses Higher-Half Direct Map (HHDM), ACPI tables (RSDP, MADT, FADT), multi-processor enumeration (SMP), I/O APIC routing, 8259 PIC fallback, 8254 Programmable Interval Timer (PIT), and real-time clock (RTC).

*  **Memory Management**: Physical memory manager (PMM) based on frame allocation bitmaps; 4-level x86_64 paging (PML4, PDPT, PD, PT) with fine-grained protection bits; dynamic kernel heap managed by a Two-Level Segregated Fit (TLSF) allocator with an auxiliary SLAB allocator for kernel object caching.

*  **Processes and Scheduling**: Preemptive multi-level priority scheduler supporting 256 priority bands, per-process timeslicing, kernel work queues, wait queues, and thread synchronization primitives including fast user-space mutexes (futex).

*  **Inter-Process Communication**: Unix domain stream and datagram sockets (`AF_UNIX`), page-mapped shared memory (`shm`), and unidirectional VFS pipes.

*  **Virtual File System (VFS)**: POSIX-style file descriptor table with support for standard streams, path resolution, mount table management, and permissions. Filesystem drivers include:

* FAT12 root ramdisk (`initrd.img`)

* MemFS volatile in-memory filesystem mounted at `/tmp`

* FAT32 partition driver for ATA/IDE hard drives mounted at `/disk`

* DevFS device tree mounted at `/dev` (`/dev/fb0`, `/dev/input/event0`, `/dev/null`, `/dev/zero`, `/dev/urandom`)

* Procfs and Sysfs synthetic filesystems for runtime kernel inspection

*  **Project Vanilla (Display Server and Desktop)**: Decoupled client-server display server communicating over Unix domain sockets and shared framebuffer memory. Features a dirty-rectangle compositor, Z-order window management, window chrome with minimize/maximize/close controls, TTF vector typography via stb_truetype, BMP wallpaper scaling, a system taskbar, application launcher, and built-in client applications.

*  **Standard C Library (`tsukasa-libc`)**: Standalone, freestanding C library implementing the standard headers (`<stdio.h>`, `<stdlib.h>`, `<string.h>`, `<math.h>`, `<unistd.h>`, `<fcntl.h>`, `<sys/socket.h>`, `<sys/mman.h>`, `<sys/wait.h>`). Includes a full IEEE 754 math library and `crt0` application startup code.

*  **Tsukasa Coreutils**: Complete set of standalone POSIX command-line utilities including `ls`, `cat`, `cp`, `mv`, `rm`, `mkdir`, `grep`, `head`, `tail`, `wc`, `cut`, `sort`, `uniq`, `uname`, `ps`, `kill`, `free`, `date`, `sysfetch`, `poweroff`, `reboot`, and the interactive `tsh` shell with history and pipeline redirection.

*  **Third-Party Ports**: Integrated build pipelines for Doom (via `doomgeneric`), Lua 5.4.7, and Tiny C Compiler (TCC).

  

---

  

## Directory Structure

  

```text

tsukasa/

|-- arch/x86_64/ Kernel entry points, boot info parser, GDT/IDT/TSS, ISRs, syscall entry

|-- drv/ Device drivers: ACPI, I/O APIC, LAPIC, PIC, PIT, RTC, serial, ATA, FB, PS/2

|-- fs/ VFS core, FAT12, FAT32, MemFS, DevFS, Procfs, Sysfs, TAR parser

|-- gfx/ Kernel-level boot graphics, font tables, legacy drawing primitives

|-- include/ Kernel headers, hardware definitions, errno, POSIX types, socket structures

|-- ipc/ Shared memory (SHM) and Unix domain socket implementations

|-- mm/ Physical frame allocator (PMM), 4-level paging (VMM), TLSF heap, SLAB allocator

|-- net/ Network device abstraction, Intel E1000 and VirtIO drivers, lwIP port

|-- proc/ Process control blocks (PCB), thread scheduler, context switching, signals

|-- sys/ Futex subsystem, wait queues, work queues, kernel console, panic handlers

|-- syscall/ Syscall dispatch table and POSIX system call handlers

|-- tsukasa-coreutils/ Standalone CLI programs and interactive tsh shell

|-- tsukasa-disktools/ Storage partition manager (fdisk), mkfs.fat32, mkfs.ext2, disk installer

|-- tsukasa-libc/ Freestanding C library implementation and application linker scripts

|-- tsukasa-ports/ Third-party ported software (Doom, Lua, TCC)

|-- vanilla/ Project Vanilla client-server display server, compositor, and GUI applications

`-- Makefile Master build system orchestrating kernel and userland packaging

```

  

---

  

## Prerequisites and Toolchain Setup

  

### Linux / Ubuntu / Debian / WSL2

  

The build system requires standard GNU and LLVM bare-metal cross tools:

  

```bash

sudo  apt-get  update

sudo  apt-get  install  -y \

build-essential \

clang \

lld \

llvm \

nasm \

xorriso \

mtools \

dosfstools \

git \

qemu-system-x86

```

  

If you wish to test the host emulator for Project Vanilla on Linux, install the SDL2 development headers:

  

```bash

sudo  apt-get  install  -y  libsdl2-dev

```

  

An automated setup script is provided in the repository root:

  

```bash

chmod  +x  setup_wsl.sh

./setup_wsl.sh

```

  

---

  

## Building the Operating System

  

Tsukasa uses GNU Make. The master Makefile builds the kernel, standard library, user utilities, desktop server, and prepares a bootable ISO image.

  

### Building the 64-Bit Limine ISO (Primary Target)

  

```bash

# 1. Clean previous build artifacts

make  clean

  

# 2. Build the standard library, user applications, and root ramdisk

make  initrd

  

# 3. Build the x86_64 kernel and package the bootable ISO

make  ARCH=x86_64  iso

```

  

The resulting bootable image is generated at `tsukasa.iso`.

  

### Building Individual Subsystems

  

You can build specific userland subsystems independently during development:

  

```bash

# Compile tsukasa-libc

make  -C  tsukasa-libc  all

  

# Compile tsukasa-coreutils

make  -C  tsukasa-coreutils  all

  

# Compile Project Vanilla display server and GUI apps

make  -C  vanilla  all

  

# Compile disk formatting and installation tools

make  -C  tsukasa-disktools  all

  

# Compile ported third-party packages (Doom, Lua)

make  -C  tsukasa-ports  all

```

  

---

  

## Running in QEMU

  

Run scripts and Make targets are configured for QEMU testing.

  

### Standard Execution via Make

  

```bash

make  run

```

  

This target builds the image if necessary and launches QEMU with 256 MiB of RAM, an attached IDE hard drive image (`disk.img`), serial output routed to the terminal, and an emulated Intel E1000 network adapter configured with user-mode NAT networking.

  

### Manual QEMU Command Line

  

For full control over QEMU parameters, run:

  

```bash

qemu-system-x86_64 \

-cdrom tsukasa.iso \

-hda  disk.img \

-boot d \

-m  256 \

-smp 2 \

-vga  std \

-serial stdio \

-netdev  user,id=u1 \

-device e1000,netdev=u1

```

  

### Script Execution

  

*  **Linux / WSL**: Run `./run_qemu.sh` for an interactive boot session with standard display and serial diagnostics.

*  **Windows PowerShell**: Run `.\run_qemu.ps1` to execute directly from a Windows environment where QEMU for Windows is installed.

  

### Parameter Breakdown

  

*  `-cdrom tsukasa.iso`: Mounts the bootable ISO containing Limine, the 64-bit kernel, and the initrd filesystem.

*  `-hda disk.img`: Attaches an ATA/IDE virtual drive used for secondary storage, partitioning, and filesystem formatting tests.

*  `-m 256`: Allocates 256 MiB of physical RAM.

*  `-smp 2`: Exposes 2 CPU cores to test SMP detection and multi-core data structures.

*  `-vga std`: Configures a standard Bochs/VESA compatible linear framebuffer.

*  `-serial stdio`: Directs COM1 serial output to your terminal for real-time kernel logging.

*  `-netdev user,id=u1 -device e1000,netdev=u1`: Attaches an Intel 82540EM Gigabit NIC connected to QEMU's virtual NAT network.

  

---

  

## Project Vanilla Desktop Host Emulator

  

To speed up GUI development and avoid restarting virtual machines when tweaking window manager layouts, typography, or widgets, Project Vanilla includes a host emulator that compiles natively on Linux or Windows using SDL2.

  

To build and run the host emulator:

  

```bash

cd  vanilla

make  host-run

```

  

This starts the Vanilla display server and desktop shell directly in a host window with keyboard and mouse input forwarding.

  

---

  

## Kernel Debugging with GDB

  

To debug kernel initialization or step through assembly routines:

  

1. Launch QEMU with the `-s -S` flags to pause CPU execution at the first instruction:

  

```bash

qemu-system-x86_64 -cdrom tsukasa.iso -s -S -serial stdio

```

  

2. In a separate terminal, launch GDB and connect to the QEMU target:

  

```bash

gdb tsukasa_x64.elf

(gdb) target remote localhost:1234

(gdb) break kernel_main_x64

(gdb) continue

```

  

---

  

## License

  

Project Tsukasa is released under the GNU General Public License v3.0. Refer to the `LICENSE` file for full terms and licensing details.