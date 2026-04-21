# Project Tsukasa - The Operating System

Tsukasa is a freestanding hobby operating system written in C and Assembly, built without the standard C library (`libc`).
It currently supports both a legacy 32-bit boot path and a new 64-bit migration foundation.

![Tsukasa wallpaper](https://w0.peakpx.com/wallpaper/235/811/HD-wallpaper-anime-tonikawa-over-the-moon-for-you-tsukasa-yuzaki.jpg)

## Development Status

Tsukasa is in active development.

Current state:
- Stable legacy `i386` path (GRUB + Multiboot v1)
- New single-core `x86_64` foundation path (Limine)
- Desktop loop, framebuffer, serial diagnostics, and input IRQ flow operational on x64 BSP

## Features

### Desktop & UI

- Custom compositing window manager with:
  - Z-order and focus handling
  - Drag/move window interactions
  - Close controls and desktop shell integration
- Desktop shell with:
  - Taskbar
  - Start menu
  - App icons
- Built-in apps:
  - Notepad
  - File Manager
  - Settings
  - Calculator
  - Terminal
  - About
- 32-bit color framebuffer rendering
- BMP wallpaper loading and scaling

### Filesystems & Storage

- FAT12 ramdisk (`/`) via `initrd.img`
- MemFS (`/tmp`) for writable volatile files
- FAT32 ATA disk mount (`/disk`) when detected

### Platform

- Early COM1 serial logging for boot diagnostics
- x64 descriptor setup (GDT/IDT/TSS)
- Exception handling with usable diagnostics
- PIC-based IRQ routing for keyboard/mouse on BSP

## Boot Paths

### `ARCH=i386` (legacy)

- Bootloader: GRUB
- Protocol: Multiboot v1
- Kernel artifact: `tsukasa.bin`

### `ARCH=x86_64` (new foundation)

- Bootloader: Limine
- Kernel artifact: `tsukasa_x64.elf`
- Includes:
  - x64 boot entry
  - Limine boot info parsing (framebuffer/memory map/modules)
  - x64 CPU descriptor/interrupt setup
  - Higher-half / HHDM memory groundwork

## Build Dependencies (WSL / Linux)

Required:
- `build-essential` (`gcc`, `make`, `ld`)
- `nasm`
- `xorriso`
- `dosfstools` (`mkfs.fat`, `mcopy`)
- `git`
- `qemu-system-x86_64` and/or `qemu-system-i386`
- `grub-mkrescue` (for legacy i386 ISO path)

Setup helper:

```bash
chmod +x setup_wsl.sh
./setup_wsl.sh
```

## Quick Start (Windows PowerShell + WSL)

From PowerShell:

```powershell
cd <path-to-tsukasa>
```

Tip: in WSL, a Windows path like `C:\dev\tsukasa` becomes `/mnt/c/dev/tsukasa`.

Optional tool check:

```powershell
wsl bash -lc "which gcc nasm make xorriso qemu-system-x86_64 qemu-system-i386"
```

### Build + Run `x86_64` (Limine)

```powershell
wsl bash -lc "cd <wsl-path-to-tsukasa> && make clean && make initrd && make ARCH=x86_64 iso"
wsl bash -lc "cd <wsl-path-to-tsukasa> && qemu-system-x86_64 -cdrom tsukasa.iso -hda disk.img -boot d -m 256 -smp 1 -vga std -serial stdio"
```

### Build + Run `i386` (legacy)

```powershell
wsl bash -lc "cd <wsl-path-to-tsukasa> && make clean && make initrd && make ARCH=i386 iso"
wsl bash -lc "cd <wsl-path-to-tsukasa> && qemu-system-i386 -cdrom tsukasa.iso -hda disk.img -boot d -m 64 -vga std -serial stdio"
```

## Build Instructions (Manual)

Always run `make clean` when switching architectures.

Build initrd:

```bash
make initrd
```

Build x64 ISO:

```bash
make clean
make initrd
make ARCH=x86_64 iso
```

Build i386 ISO:

```bash
make clean
make initrd
make ARCH=i386 iso
```

## Runtime Notes

- The x64 path is intentionally single-core in the current foundation stage.
- Interrupt routing is currently PIC-based; APIC/timer preemption work is future work.
- If boot debugging is needed, prioritize serial output (`-serial stdio`) in QEMU.
