# Project Tsukasa - Operating System

Tsukasa is a freestanding hobby operating system written in C and Assembly. It now supports two boot/build paths during migration:

- Legacy `i386` path (GRUB + Multiboot v1)
- New `x86_64` foundation path (Limine)

The project is currently in a phased migration toward a BoredOS-aligned x64 architecture.

![Tsukasa wallpaper](https://w0.peakpx.com/wallpaper/235/811/HD-wallpaper-anime-tonikawa-over-the-moon-for-you-tsukasa-yuzaki.jpg)

## Migration Status

- Phase 0 (baseline audit/documentation): complete
- Phase 1 (x86_64 platform migration foundation): complete
- Phase 2+: planned

Reference docs:

- `docs/baseline/PHASE_0_EXECUTION_LOG.md`
- `docs/baseline/COMPATIBILITY_MAP.md`
- `phase_plans/PHASE_1_X64_FOUNDATION.md`
- `docs/migration/PHASE_1_EXECUTION_LOG.md`
- `TSUKASA_BOREDOS_GAP_AND_IMPLEMENTATION_PLAN.md`

## Current Capabilities

### Desktop and UI

- Compositing window manager with Z-order, focus, drag, close controls
- Desktop shell with taskbar, start menu, icons
- Built-in apps: Notepad, File Manager, Settings, Calculator, Terminal, About
- 32bpp framebuffer rendering and BMP wallpaper loading

### Filesystems and Storage

- FAT12 ramdisk mounted at `/` from `initrd.img`
- MemFS mounted at `/tmp` for writable volatile files
- FAT32 ATA disk mounted at `/disk` when present

### Platform and Boot

- Early serial diagnostics on COM1
- Descriptor tables and interrupt handling operational on x64 BSP
- Keyboard/mouse IRQ routing via PIC (single-core)

## Boot Architecture Paths

### `ARCH=i386` (legacy path)

- Bootloader: GRUB (Multiboot v1)
- Entry: `boot.s` -> `kernel_main`
- Paging model: legacy 32-bit bootstrap paging

### `ARCH=x86_64` (new foundation path)

- Bootloader: Limine
- Entry: `arch/x86_64/boot/entry.asm` -> `tsukasa_x64_entry` -> `kernel_main_x64`
- Boot info parser: `arch/x86_64/boot/boot_info.c`
- New CPU platform code:
  - `arch/x86_64/cpu/gdt.*`
  - `arch/x86_64/cpu/idt.*`
  - `arch/x86_64/cpu/isr.asm`
- New memory layer:
  - `mm/vmm_x64.*`
  - higher-half and HHDM translation helpers

## Build Dependencies (WSL/Linux)

Required tools:

- `build-essential` (`gcc`, `make`, `ld`)
- `nasm`
- `xorriso`
- `dosfstools` (`mkfs.fat`, `mcopy`)
- `qemu-system-x86_64` and/or `qemu-system-i386`
- `git` (used to fetch/build Limine artifacts for `ARCH=x86_64`)
- `grub-mkrescue` (for `ARCH=i386` ISO path)

Setup helper (Ubuntu/Debian):

```bash
chmod +x setup_wsl.sh
./setup_wsl.sh
```

## Build Instructions

Always run `make clean` when switching architectures.

## Quick Start (Windows PowerShell + WSL)

Run these from PowerShell in the project root:

```powershell
cd C:\Users\frost145\Projects\tsukasa
```

Optional tool check in WSL:

```powershell
wsl bash -lc "which gcc nasm make xorriso qemu-system-x86_64 qemu-system-i386"
```

Build + run `x86_64` (Limine):

```powershell
wsl bash -lc "cd /mnt/c/Users/frost145/Projects/tsukasa && make clean && make initrd && make ARCH=x86_64 iso"
wsl bash -lc "cd /mnt/c/Users/frost145/Projects/tsukasa && qemu-system-x86_64 -cdrom tsukasa.iso -hda disk.img -boot d -m 256 -smp 1 -vga std -serial stdio"
```

Build + run legacy `i386` (GRUB):

```powershell
wsl bash -lc "cd /mnt/c/Users/frost145/Projects/tsukasa && make clean && make initrd && make ARCH=i386 iso"
wsl bash -lc "cd /mnt/c/Users/frost145/Projects/tsukasa && qemu-system-i386 -cdrom tsukasa.iso -hda disk.img -boot d -m 64 -vga std -serial stdio"
```

### Build initrd

```bash
make initrd
```

### Build x86_64 (Limine) ISO

```bash
make clean
make initrd
make ARCH=x86_64 iso
```

Output kernel artifact: `tsukasa_x64.elf`

### Build i386 (GRUB) ISO

```bash
make clean
make initrd
make ARCH=i386 iso
```

Output kernel artifact: `tsukasa.bin`

## Run in QEMU

### x86_64 path (recommended)

```bash
qemu-system-x86_64 -cdrom tsukasa.iso -hda disk.img -boot d -m 256 -smp 1 -vga std -serial stdio
```

### i386 legacy path

```bash
qemu-system-i386 -cdrom tsukasa.iso -hda disk.img -boot d -m 64 -vga std -serial stdio
```

## Validated x64 Phase 1 Milestones

The current x64 path has been validated to:

- boot reliably in QEMU single-core mode
- emit early serial diagnostics
- initialize framebuffer
- initialize PMM/heap from Limine-derived boot info
- initialize x64 GDT/IDT/TSS and handle exceptions
- mount FAT12/FAT32 paths and enter desktop loop

## Known Limitations (Current)

- Single-core only on x64 (no SMP scheduler/AP bring-up yet)
- PIC-based IRQ path retained (APIC/timer preemption deferred to later phase)
- Full x64 syscall/process ABI expansion is not complete yet
- Some workflows still require manual testing (GUI interaction regressions)

## Notes for Contributors

- Keep architecture-specific code under clear folder boundaries (`arch/x86_64/*` vs legacy paths).
- Prefer serial logs for early boot debugging.
- When changing migration behavior, update:
  - `docs/migration/PHASE_1_EXECUTION_LOG.md`
  - `TSUKASA_BOREDOS_GAP_AND_IMPLEMENTATION_PLAN.md`
