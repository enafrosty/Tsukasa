# Project Tsukasa Development Roadmap

This document tracks what Project Tsukasa already ships today and the upcoming engineering workstreams leading toward a self-hosting, multi-user POSIX desktop operating system on `x86_64`.

---

## Completed Foundations

### Kernel Stabilization, Safety & Tooling
* **Single `x86_64` Architecture**: Retired the legacy 32-bit Multiboot path and unified the kernel, userland, and Limine v8 (BIOS + UEFI) boot pipeline around 64-bit Long Mode.
* **Ring-3 Isolation & Exception Stacks**: Isolated user-mode faults (`#PF`, `#GP`, `#UD`, etc.) from panicking the kernel and provisioned per-CPU TSS `IST1` stacks for `#DF` and `#NMI` handling.
* **Interruptible Syscalls & Timing**: Enabled interrupts (`sti`) around blocking syscall dispatch, moved `process_yield()` to a dedicated `int 0x40` vector, implemented blocking `sys_nanosleep`, and ensured PID 1 reaps exited startup helpers.
* **Framebuffer Acceleration**: Enabled PAT Write-Combining (`MMU_FLAG_WC`) for kernel and user `/dev/fb0` mappings.
* **Diagnostics, Sanitizers & Dual-Toolchain CI**: Enabled frame-pointer symbolicated kernel backtraces, global stack protector (`-fstack-protector-strong`), optional `UBSAN=1` and COM2 `GDBSTUB=1` targets, full `TOOLCHAIN=llvm` + `TOOLCHAIN=gcc` parity, and headless QMP/QEMU automated testing (`make check`, `make visual-test`, storage matrix, installer E2E).

### Platform, Storage, VFS & Persistence
* **ACPI, APIC & PCIe**: Activated Limine RSDP ACPI parsing (`RSDT`/`XSDT`, `MADT`, `FADT` `_S5_` shutdown, `MCFG`), multi-I/O APIC routing with 8259 PIC fallback, dynamic IRQ vector allocation (`0x50..0xEF`), and PCIe ECAM + 64-bit BAR + MSI/MSI-X support.
* **LZ4 Initramfs & UNIX Rootfs**: Replaced the 16 MiB FAT12 ramdisk with an LZ4-compressed USTAR archive (`initrd.tar.lz4`) unpacked into a multi-instance `tmpfs` with standard UNIX directories (`/bin`, `/sbin`, `/usr`, `/etc`, `/home`, `/root`, `/var`, `/tmp`, `/dev`, `/proc`, `/sys`, `/boot`, `/mnt`).
* **IRQ/DMA Storage & GPT**: Added interrupt/DMA drivers for AHCI SATA (`/dev/sda*`), VirtIO-blk (`/dev/vda*`), and NVMe (`/dev/nvme0n1*`), kept ATA PIO fallback (`/dev/hda*`), validated primary+backup GPT headers/entries with CRC32, and exposed block ioctls (`BLKGETSIZE64`, `BLKSSZGET`, `BLKRRPART`).
* **Locked VFS, LRU Page Cache, `ext4` & `FAT32`**: Hardened VFS locking, raised per-process open FDs to 256, decoupled Unix sockets from pipe rings, added dentry caching, symlinks (`lstat`/`readlink`/`-ELOOP`), per-inode `flock`, and POSIX metadata/mount/sync calls. Integrated a unified bounded LRU page cache backing journaled read/write `ext4` (`lwext4`) and repaired LFN/nested-directory `FAT32`.
* **Persistent Disk Root & Unified Installer**: Added `root=/dev/...` and `root=PARTUUID=...` root mounting with clean filesystem sync on shutdown/reboot, plus GPT `fdisk`, `mkfs.ext4`, repaired `mkfs.ext2`, and a unified BIOS+UEFI GPT/`ext4` `installer`.

### Project Vanilla & Core Userland
* **Display Server & Services**: Retired the legacy in-kernel WM; separated `vanilla_srv` (`/bin/vsrv`) and the decoupled `shell` client; added `/bin/supervisord`, `registryd`, `clipboardd`, `notifyd`, generated protocol headers, SSE2 blitter, dirty-rect compositor, rounded corners/shadows, DPI scaling, cursors, drag-and-drop, and theme tokens.
* **Libraries & Applications**: Shipped `libvanilla`, `libvanilla-ui`, `libvanillalayout`, `terminal` (VT emulator), `filemgr`, `notepad`, `calc`, `taskmgr`, `settings`, `devtools` (`microui`), and the SDL2 host emulator (`make -C vanilla host-run`).
* **Runtime & Utilities**: Fixed `tsukasa-libc` IEEE 754 `frexp`/`ldexp`, float `printf`, `atexit`/stdio exit flush, O(N log N) `qsort`, and zero-length user pointer validation; packaged all 27 `tsukasa-coreutils` and third-party ports (`doom`, `lua`/`luac`, `tcc`).

---

## Upcoming Workstreams

### 1. Core POSIX Memory, Process Model & True SMP
* **Memory & Demand Paging**: Replace linear/bitmap page management with a zoned buddy PMM, per-page reference counts (`struct page`), slab reclaim, per-process augmented RB-tree VMAs (`mprotect`, `brk`, automatic stack guard/growth), synchronous multi-core TLB shootdowns, and true demand-paged file-backed `mmap` (`MAP_PRIVATE` COW and `MAP_SHARED` writeback over the LRU page cache).
* **COW `fork`, Standard `execve` & Signals**: Implement copy-on-write `fork()`, standard `execve(path, argv, envp)` (`FD_CLOEXEC`, SUID, PIE `ET_DYN`, `PT_INTERP`, ELF auxiliary vectors), user signal trampolines with `rt_sigreturn` (full GPR/Red Zone/FPU/XSAVE + `sa_mask` restoration), `SIGCHLD`, orphan reparenting to PID 1, and blocking pipes / race-free `poll()`/`epoll`.
* **PTYs, `termios` & Multi-VT**: Add `/dev/ptmx` and `/dev/pts/N` pseudoterminals, kernel `termios` line discipline, POSIX session/process-group job control (`setsid`, `setpgid`, `tcsetpgrp`, `SIGTSTP`/`SIGTTIN`/`SIGTTOU`), and a virtual console switcher (`/bin/vterm`).
* **Threads, Timers & SMP Scheduling**: Separate `mm_struct` and `files_struct` for kernel threads and `clone()`/`exit_group()`/`arch_prctl`, key futexes on physical pages, enable HPET + calibrated TSC + per-CPU LAPIC timers + `timerfd`, add `XSAVE`/`XRSTOR` FPU state, and move scheduling from a single BSP lock to per-CPU SMP runqueues.

### 2. Hosted Toolchain, Dynamic Linking & POSIX Shell
* **Cross-Toolchain (`x86_64-tsukasa`)**: Build a hosted GCC/Binutils C and C++ cross-toolchain targeting Tsukasa with PIE and shared library emission.
* **Dynamic Linker & Standard C Runtime**: Align the syscall ABI for dynamic linking, adopt a full POSIX `libc` with `/usr/lib/ld.so` dynamic loader and `libc.so`, and build shared Vanilla libraries while retiring minimal standalone runtime shims.
* **Full Interactive `tsh` & Coreutils**: Upgrade `tsh` with `$VAR` expansion, `$(...)` command substitution, `;`/`&&`/`||`, `2>&1` and heredoc redirections, globbing, control-flow constructs (`if`/`for`/`while`/`case`/functions), job control, persistent history, and Tab completion; expand and harden `tsukasa-coreutils` (iterative tree traversal, regex `grep`, LZ4/Zstd `tar`).

### 3. Full BSD Networking, TLS, Multi-User Init & Hardening
* **VFS BSD Socket Layer**: Route `AF_INET`, `AF_INET6`, `AF_UNIX` (with `SCM_RIGHTS` FD passing and `SO_PEERCRED`), `AF_PACKET`, `/dev/net/tun`, and loopback through standard VFS sockets with IRQ-driven E1000/VirtIO-net/Realtek drivers, retiring the legacy `sys_netcall` gateway.
* **TLS & Network Tools**: Integrate BearSSL/mbedTLS with a root CA bundle and ship `curl` (HTTPS), `dhclient`, `dig`, `ping`/`ping6`, `ifconfig`, `route`, `sntp`, `httpd`, and an SSH server/client.
* **Ring-3 PID 1 & Multi-User Authentication**: Unify boot supervision in a Ring-3 PID 1 `/sbin/init` with service supervision, `/etc/passwd` + `$6$` `/etc/shadow`, `getty`, `login`, `passwd`, `su`, `doas`, and `useradd`.
* **CPU & Kernel Hardening**: Enforce `EFER.NXE`, SMEP, SMAP, and UMIP; implement fault-safe exception-table `copy_from_user`/`copy_to_user`; randomize PIE ASLR base addresses; and back `/dev/urandom` and stack canaries with a ChaCha20 CSPRNG.

### 4. Modern Hardware Drivers (USB, Audio & GPU)
* **USB xHCI & HID**: Implement IRQ/MSI-X USB xHCI (BIOS handoff, scratchpads, event rings), hub enumeration, HID keyboard (full usage tables), HID relative mouse & **absolute tablet** pointer (seamless QEMU/Wayland cursor integration), and USB Mass Storage (BOT).
* **Audio Subsystem**: Add Intel AC97, Intel HDA, and VirtIO-snd drivers behind a multi-client `/dev/dsp` software mixer and `audioplay` utility.
* **VirtIO-GPU 2D & Multi-Monitor**: Add dirty-rect `FBIO_FLUSH_RECT` / mode-setting ioctls and multi-head output support across the kernel, compositor, and shell.

### 5. Project Vanilla Desktop Evolution
* **Next-Gen Compositor**: Adopt `SCM_RIGHTS` SHM surface passing, cursor fast-path updates, live window resize previews, scaled motion timings, and unified context menus.
* **Unified `libvanilla-ui.so` & Desktop Polish**: Partial dirty widget repaints, end-to-end DPI scaling, richer widgets (`TreeView`, `TableView`, `ComboBox`, File Picker, Find/Undo), `.desktop` application discovery, interactive statusbar applets, desktop icons, a graphical login/lock screen, and a full PTY-backed `terminal`, thumbnail `filemgr`, `settings`, `taskmgr`, and media/archive viewers.

### 6. Packaging, 3D Graphics & Ecosystem
* **Package Manager (`tpkg`)**: Build a signed, dependency-resolving package manager integrated with a CI-built package repository and `--root` installer support.
* **SDL2 & 3D**: Port SDL2 over `libvanilla`, `/dev/dsp`, and evdev, paired with TinyGL (OpenGL 1.1) and optional VirtIO-GPU VirGL 3D acceleration.
* **Showcase Ports & Release QA**: Distribute self-hosting TCC/`make`, Lua, `vim`/`kilo`, DOOM with sound, Quake I/II, ClassicCube, and NetSurf via `tpkg`, backed by syscall/protocol fuzzers and nightly release ISOs.
