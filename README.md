![logo](https://i.imgur.com/PRzEoKE.png)

# Project Tsukasa

[![License: GPL v3](https://img.shields.io/badge/License-GPLv3-blue.svg)](LICENSE)
[![Build](https://github.com/enafrosty/Tsukasa/actions/workflows/build.yml/badge.svg)](https://github.com/enafrosty/Tsukasa/actions/workflows/build.yml)
[![Wiki](https://img.shields.io/badge/Docs-GitHub%20Wiki-blueviolet)](https://github.com/enafrosty/Tsukasa/wiki)

Tsukasa is a freestanding `x86_64` operating system written in C11 and NASM assembly. Built without external standard runtime dependencies (`-ffreestanding -nostdlib`), it implements its own Limine boot handoff, 4-level virtual memory manager, unified LRU page cache, journaled `ext4` and `FAT32` filesystems, IRQ/DMA storage and network drivers, preemptive priority scheduler, POSIX system call interface, standalone C library (`tsukasa-libc`), UNIX userland utilities (`tsukasa-coreutils`), disk partitioning/installation tools (`tsukasa-disktools`), third-party ports (`tsukasa-ports`), and the client-server **Project Vanilla** compositing display server and desktop environment.

* **Website:** [tsukasa.heyfrosty.space](https://tsukasa.heyfrosty.space)
* **Wiki:** [github.com/enafrosty/Tsukasa/wiki](https://github.com/enafrosty/Tsukasa/wiki)
* **Roadmap:** [ROADMAP.md](ROADMAP.md)

---

## Key Architecture & Features

### Kernel Core (`x86_64` Long Mode)
* **Boot & Platform Bringup**: Boots via Limine v8 (BIOS and UEFI) into 64-bit Long Mode with Higher-Half Direct Map (HHDM). Parses ACPI (`RSDT`/`XSDT`, `MADT`, `FADT` `_S5_` shutdown, `MCFG`), initializes multi-I/O APIC routing (with 8259 PIC fallback), Local APIC, SMP (`sys/smp.c`), PIT 8254 (`100 Hz`), CMOS RTC, dynamic IRQ vector allocation (`0x50..0xEF`), and PCIe ECAM configuration space with 64-bit BAR decoding and MSI/MSI-X support.
* **Isolation, Fault Recovery & Diagnostics**: Ring-3 exception isolation with per-CPU TSS `IST1` stacks for `#DF`/`#NMI`, interruptible system calls (`sti` in syscall dispatch), dedicated scheduler yield vector (`int 0x40`), blocking `nanosleep`, global stack protector (`-fstack-protector-strong`), frame-pointer symbolicated backtraces, optional `UBSAN=1` instrumentation, and an optional COM2 remote GDB stub (`GDBSTUB=1`).
* **Memory Management**: Bitmap Physical Memory Manager (PMM), 4-level `x86_64` Virtual Memory Manager (PML4/PDPT/PD/PT), PAT Write-Combining (`MMU_FLAG_WC`) for kernel and `/dev/fb0` framebuffer mappings, TLSF constant-time kernel heap, SLAB object caches, per-process `vm_space_t`, strict syscall user-pointer validation (`vmm_validate_user_ptr`), and a unified bounded LRU page cache (`mm/pagecache.c`).
* **Scheduling & IPC**: Preemptive 256-level priority scheduler (`40 ms` timeslices), wait queues, work queues, futexes, signals, shared memory (`shm`), Unix domain sockets (`AF_UNIX`), and VFS pipes.

### Storage, Filesystems & Installer
* **PCI Storage Controllers & Partitioning**: Interrupt/DMA drivers for **AHCI SATA** (`/dev/sda*`), **VirtIO-blk** (`/dev/vda*`), and **NVMe** (`/dev/nvme0n1*`), plus legacy ATA PIO fallback (`/dev/hda*`), with full primary+backup GPT CRC32 validation, MBR support, and block device ioctls (`BLKGETSIZE64`, `BLKSSZGET`, `BLKRRPART`).
* **Virtual File System (VFS)**: Thread-safe VFS with up to 256 open file descriptors per process, hierarchical dentry cache, symlinks (`lstat`/`readlink` with `-ELOOP` detection), per-inode `flock`, POSIX metadata/mount/sync calls (`stat`, `getdents64`, `chmod`, `chown`, `truncate`, `umask`, `access`, `mount`, `umount2`, `fsync`, `sync`), and clean filesystem flush on shutdown/reboot.
* **Filesystems**:
  * Multi-instance `tmpfs` mounted at `/`, `/tmp`, and `/var/run` in live boot, populated from an LZ4-compressed USTAR archive (`initrd.tar.lz4`) with a standard UNIX layout (`/bin`, `/sbin`, `/usr`, `/etc`, `/home`, `/root`, `/var`, `/dev`, `/proc`, `/sys`, `/boot`, `/mnt`).
  * Journaled read/write **ext4** (`lwext4` + page cache), supporting persistent disk root (`root=/dev/...` and `root=PARTUUID=...`).
  * Page-cached **FAT32** with Long File Name (LFN), nested subdirectories, truncation, and ESP mounting (`/boot`) or `/mnt/*`.
  * Synthetic and boot filesystems: **DevFS** (`/dev`), **ProcFS** (`/proc`), **SysFS** (`/sys`), and **BootFS**.
* **Disk Tools (`tsukasa-disktools`)**: `fdisk`, `mkfs.fat32`, `mkfs.ext2`, `mkfs.ext4`, and a unified BIOS+UEFI GPT/`ext4` OS `installer`.

### Project Vanilla (Display Server & Desktop)
* **Display Server (`vanilla_srv` / `/bin/vsrv`)**: Pure userspace compositor communicating over `/tmp/vanilla.sock` and SHM surfaces, writing directly to PAT Write-Combined `/dev/fb0` via an SSE2 blitter and dirty-rectangle damage tracking. Supports rounded window chrome, drop shadows, DPI scaling, cursors, drag-and-drop, runtime themes, `stb_truetype` vector fonts, and `stb_image`/BMP assets.
* **Desktop Services & Libraries**: Managed by `/bin/supervisord` (`/etc/services.conf`), including `registryd`, `clipboardd`, `notifyd`, and the decoupled `shell` client (statusbar with live CPU/RAM/clock, dock, quick launcher, system menu, and context menus), built atop `libvanilla`, `libvanilla-ui`, `libvanillalayout`, and generated IPC protocol definitions.
* **Applications**: `terminal` (VT emulator + `tsh`), `filemgr`, `notepad`, `calc`, `taskmgr`, `settings`, and `devtools` (`microui`). Includes a native SDL2 host emulator (`make -C vanilla host-run`) for rapid UI iteration.

### Userland, Ports & Networking
* **Standard C Library (`tsukasa-libc`)**: Freestanding C11/POSIX headers, `crt0.o`, `libc.a`, and IEEE 754 `libm.a` with `tsukasa_app.ld`.
* **Core Utilities (`tsukasa-coreutils`)**: 27 standalone utilities (`cat`, `cp`, `cut`, `date`, `fetch`, `free`, `grep`, `head`, `kill`, `ls`, `mkdir`, `mv`, `net`, `ping`, `poweroff`, `ps`, `reboot`, `rm`, `sort`, `sysfetch`, `tail`, `telnet`, `touch`, `tsh`, `uname`, `uniq`, `wc`).
* **Third-Party Ports (`tsukasa-ports`)**: Doom (`doomgeneric`), Lua 5.4.7 (`lua`, `luac`), and Tiny C Compiler (`tcc`).
* **Networking**: Intel E1000 (`0x8086:0x100E/100F`) and VirtIO-net (`0x1AF4:0x1000`) drivers backed by lwIP 2.x (DHCP, DNS, ICMP, TCP, UDP). Note: `sys_socket` currently serves `AF_UNIX`; IP networking goes through `sys_netcall` until full BSD `AF_INET`/`AF_INET6` sockets land.

---

## Directory Structure

```text
Tsukasa/
|-- arch/x86_64/        Boot entry, Limine boot info parser, GDT/IDT/TSS, ISRs, SYSCALL entry
|-- assets/             Vanilla desktop themes (.ini) and ARGB cursors
|-- dev/                PCI/PCIe ECAM bus enumeration, BARs, MSI/MSI-X
|-- docs/               Architecture overview, automated testing guide, third-party inventory
|-- drv/                ACPI, IOAPIC, LAPIC, IRQ allocator, AHCI, VirtIO-blk, NVMe, ATA, FB, PS/2, serial
|-- fs/                 VFS core, dcache, tmpfs, ext4 (lwext4), FAT32, DevFS, ProcFS, SysFS, BootFS, USTAR
|-- gfx/                Minimal kernel boot/panic graphics helpers (legacy kernel WM retired)
|-- include/            Kernel headers, hardware registers, errno, syscall & POSIX types
|-- ipc/                Shared memory (SHM) and Unix domain sockets (AF_UNIX)
|-- lib/                kprintf, LZ4 decompression, ksymbols/backtrace, stack protector, UBSan handlers
|-- loader/             ELF64 loader and executable runner
|-- mm/                 PMM, 4-level x86_64 VMM + PAT WC, TLSF heap, SLAB cache, LRU page cache
|-- net/                NIC abstraction, E1000/VirtIO-net drivers, lwIP 2.x TCP/IP port
|-- proc/               PCBs, preemptive 256-band priority scheduler, assembly switch, signals
|-- scripts/            Build helpers (ksymbols), CI runners, headless QMP QEMU automation & test suites
|-- sys/                SMP bringup, futex, wait/work queues, panic screen, kconsole, COM2 gdbstub
|-- syscall/            POSIX flat syscall table and compatibility syscall handlers
|-- tools/              Host build utilities (lz4pack, GPT image builders, generators)
|-- tsukasa-coreutils/  27 standalone CLI utilities and the interactive tsh shell
|-- tsukasa-disktools/  fdisk, mkfs.fat32, mkfs.ext2, mkfs.ext4, and unified BIOS+UEFI installer
|-- tsukasa-libc/       Freestanding C library (libc.a, libm.a, crt0.o) and test binaries
|-- tsukasa-ports/      Third-party ports (doomgeneric, Lua 5.4.7, TinyCC)
|-- tty/                Virtual terminal (TTY) subsystem
|-- user/               Kernel/SDK test drivers (TESTDRV.ELF) and minimal ELF stubs
|-- vanilla/            Project Vanilla display server, shell, supervisor, daemons, UI libs, apps, host emulator
`-- Makefile            Top-level build, ISO packaging, QEMU launch, and test orchestration
```

---

## Prerequisites and Toolchain Setup

### Linux / Fedora
Run the setup script or install packages directly:

```bash
chmod +x setup_fedora.sh
./setup_fedora.sh
```

Or manually:

```bash
sudo dnf install -y clang lld llvm nasm lz4 xorriso mtools dosfstools e2fsprogs \
    qemu-system-x86 qemu-img SDL2-devel gcc make git binutils gdb patch python3
```

### Linux / Debian / Ubuntu / WSL2
Run the setup script or install packages directly:

```bash
chmod +x setup_wsl.sh
./setup_wsl.sh
```

Or manually:

```bash
sudo apt-get update
sudo apt-get install -y build-essential clang lld llvm nasm lz4 xorriso mtools \
    dosfstools e2fsprogs git gdb python3 qemu-system-x86 qemu-utils libsdl2-dev
```

For optional UEFI OVMF boot testing (`test-installer-e2e.py` / `--firmware uefi`), also install your distribution's EDK2 OVMF package (`edk2-ovmf` on Fedora, `ovmf` on Debian/Ubuntu).

---

## Building the Operating System

### Primary Bootable ISO (`x86_64`)

```bash
make -j$(nproc) iso-x86_64
```

This builds `tsukasa-libc`, `tsukasa-coreutils`, `tsukasa-disktools`, `tsukasa-ports`, `vanilla`, guest test suites, packs `build/initrd.tar.lz4`, fetches/caches Limine `v8.x-binary` into `.limine/` (requires network access on first build), compiles `tsukasa_x64.elf`, and produces `tsukasa.iso`.

### Build Options and Subsystems

```bash
# Build with GCC instead of LLVM/Clang (default is TOOLCHAIN=llvm)
make -j$(nproc) TOOLCHAIN=gcc iso-x86_64

# Enable Kernel UBSan or COM2 remote GDB stub
make -j$(nproc) UBSAN=1 iso-x86_64
make -j$(nproc) GDBSTUB=1 iso-x86_64

# Kernel ELF only
make -j$(nproc)

# Individual userland targets
make libc
make vanilla
make coreutils
make disktools
make ports
```

Always run `make clean` before major linker or standalone toolchain changes if you manually customize build flags, though compiler flags and header dependencies (`-MMD -MP`) are tracked automatically.

---

## Running in QEMU

### Quick Launch

```bash
# Build (if needed) and launch QEMU via Makefile
make run

# Or use the helper scripts
./run_qemu.sh          # Linux / macOS / WSL2
.\run_qemu.ps1         # Windows PowerShell
```

### Q35 + AHCI SATA / VirtIO-blk / NVMe

Tsukasa automatically discovers and initializes AHCI, VirtIO-blk, NVMe, and legacy IDE/ATA controllers. For modern Q35 SATA testing:

```bash
qemu-img create -f raw disk.img 256M   # minimum 96M if testing OS installation
qemu-system-x86_64 -machine q35 -cdrom tsukasa.iso -boot d \
    -drive if=none,id=d0,file=disk.img,format=raw \
    -device ahci,id=ahci -device ide-hd,drive=d0,bus=ahci.0 \
    -m 256 -smp 2 -vga std -serial stdio \
    -netdev user,id=u1 -device e1000,netdev=u1
```

> **Note for Wayland / Hyprland users (small laptop screens or fractional scaling):**
> Tsukasa currently uses a relative PS/2 mouse at `1280x800x32`. If GTK pointer grab clips or skews inside a tiled/scaled window, use SDL display mode with `Ctrl+Alt+G` to capture/release the mouse and `Ctrl+Alt+F` for fullscreen:
> `SDL_VIDEODRIVER=wayland qemu-system-x86_64 ... -display sdl`

See [BOOT_QEMU.md](BOOT_QEMU.md) for NVMe, VirtIO-blk, UEFI OVMF, installed-disk boot, and GDB instructions.

---

## Project Vanilla Host Emulator

You can run the Vanilla display server, compositor, shell, and apps natively on Linux/WSL2/Windows via SDL2 without booting a VM:

```bash
cd vanilla
make host-run
```

---

## Testing and Verification

Tsukasa includes automated host, guest, QMP visual, storage-matrix, and disk-installer E2E test suites:

```bash
# 1. Host unit tests (8 Vanilla suites) + Headless QEMU guest test suite (169 assertions total)
make check

# 2. Headless QEMU boot smoke test + kernel selftests
bash scripts/ci/boot-test.sh tsukasa.iso boot.log

# 3. Automated QMP visual compositor & GUI desktop test suite
make visual-test

# 4. Storage controller matrix (pc/q35 x ahci/virtio/nvme)
python3 scripts/test/test-storage-matrix.py --iso tsukasa.iso --machine both --mode probe --require-driver

# 5. Live ISO -> GPT/ext4 install -> SeaBIOS + UEFI double-reboot persistence + host fsck
python3 scripts/test/test-installer-e2e.py --iso tsukasa.iso --storage ahci
```

Full documentation of the headless QMP automation framework lives in [docs/AUTOMATED_TESTING.md](docs/AUTOMATED_TESTING.md).

---

## Kernel Debugging with GDB

1. Start QEMU paused at the reset vector with GDB server enabled:

```bash
qemu-system-x86_64 -cdrom tsukasa.iso -s -S -no-reboot -serial stdio
```

2. Attach GDB to `tsukasa_x64.elf`:

```bash
gdb tsukasa_x64.elf
(gdb) target remote localhost:1234
(gdb) break kernel_main_x64
(gdb) continue
```

---

## Roadmap & Documentation

* **Project Roadmap & Future Plans:** [ROADMAP.md](ROADMAP.md)
* **Architecture Overview:** [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md)
* **Booting & QEMU Guide:** [BOOT_QEMU.md](BOOT_QEMU.md)
* **Automated QMP & Storage Testing:** [docs/AUTOMATED_TESTING.md](docs/AUTOMATED_TESTING.md)
* **Third-Party Sources & Licenses:** [docs/THIRD_PARTY.md](docs/THIRD_PARTY.md)
* **Contributing Guide:** [CONTRIBUTING.md](CONTRIBUTING.md)
* **Changelog:** [CHANGELOG.md](CHANGELOG.md)
* **Security Policy:** [SECURITY.md](SECURITY.md)
* **GitHub Wiki:** [github.com/enafrosty/Tsukasa/wiki](https://github.com/enafrosty/Tsukasa/wiki)

---

## License

Project Tsukasa is licensed under the GNU General Public License v3.0. See [LICENSE](LICENSE) for the full text.
