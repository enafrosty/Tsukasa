# Changelog

All notable changes to Project Tsukasa are documented in this file.

## [Unreleased]

### Documentation & Repository
* Refreshed `README.md`, `BOOT_QEMU.md`, `docs/THIRD_PARTY.md`, `setup_fedora.sh`, and `setup_wsl.sh` to reflect the current `x86_64` system, storage/VFS/ext4/installer stack, and automated test suite.
* Added `ROADMAP.md`, `CONTRIBUTING.md`, `CHANGELOG.md`, `SECURITY.md`, `docs/ARCHITECTURE.md`, `.editorconfig`, and GitHub issue/PR templates.

---

## Storage, VFS, Journaled `ext4` & Persistent Installer
* **Headless QEMU Automation Framework**: Added `scripts/test/qemu_harness.py`, refactored `scripts/test/visual-test.py`, and introduced `scripts/test/test-storage-matrix.py` and `scripts/test/test-installer-e2e.py` along with `docs/AUTOMATED_TESTING.md`.
* **LZ4 Initramfs & Multi-Instance `tmpfs`**: Replaced the 16 MiB FAT12 initrd with `initrd.tar.lz4` (host `tools/lz4pack.c`, kernel `lib/lz4.c`, USTAR `fs/tar.c`) unpacked into `tmpfs` with full UNIX directories and all 27 coreutils, Vanilla themes, cursors, icons, disktools, ports, and Limine install artifacts.
* **ACPI, Multi-I/O APIC, PCIe ECAM & MSI/MSI-X**: Enabled `acpi_init()` with Limine RSDP, `MADT`/`FADT`/`MCFG` parsing, `drv/ioapic.c` + 8259 PIC masking, dynamic IRQ vector allocation (`0x50..0xEF`), and 64-bit PCIe BAR + MSI/MSI-X programming.
* **IRQ/DMA Storage & GPT Validation**: Added interrupt- and DMA-driven `drv/ahci.c`, `drv/virtio_blk.c`, and `drv/nvme.c` alongside ATA PIO fallback, primary+backup GPT CRC32 verification, `/dev/{sd,vd,nvme}*` device nodes, and `BLKGETSIZE64`/`BLKSSZGET`/`BLKRRPART` ioctls.
* **Locked VFS, LRU Page Cache & POSIX Syscalls**: Added `mm/pagecache.c`, locked VFS operations, per-process 256-FD limit, dentry caching, symlinks (`lstat`/`readlink`/`-ELOOP`), per-inode `flock`, POSIX metadata/mount/sync syscalls, and decoupled Unix domain sockets from VFS pipes.
* **Journaled `ext4` & Repaired `FAT32`**: Vendored `lwext4` (`fs/ext4fs.c`) and reworked `fs/fat32.c` for LFN, nested directories, truncation, and concurrency through the unified LRU page cache.
* **Persistent Disk Root & Unified BIOS+UEFI Installer**: Added kernel `root=/dev/*` and `root=PARTUUID=*` mounting, `sys_sync`/`sys_fsync` flush on shutdown/reboot, GPT `fdisk`, `mkfs.ext4`, repaired `mkfs.ext2`, and a unified GPT/`ext4` BIOS+UEFI `installer`.

## Kernel Stabilization, Isolation & GCC CI
* **Ring-3 Exception Isolation & IST1 Stacks**: Isolated user-mode CPU exceptions from kernel panics and wired per-CPU TSS `IST1` stacks for `#DF`/`#NMI` recovery.
* **Interruptible Syscalls & Yield Vector**: Enabled `sti` during blocking syscall dispatch, moved `process_yield()` to `int 0x40`, added blocking `sys_nanosleep` and `nanosleep`-paced daemons, and ensured PID 1 reaps finished bootstrap helpers.
* **PAT Write-Combining Framebuffer**: Added `PAGING_MAP_WC` / `MMU_FLAG_WC` and mapped kernel and `/dev/fb0` userspace framebuffers with write-combining attributes.
* **Project Vanilla Stabilization & Service Supervisor**: Fixed hover/key forwarding, `ack_configure` and resize buffer swaps, cursor shapes, alpha blending, dock minimize/maximize/close controls, and moved startup orchestration into `/bin/supervisord` + `registryd`/`clipboardd`/`notifyd`.
* **`tsukasa-libc` & VMM Hardening**: Fixed IEEE 754 `frexp`/`ldexp`, `%f/%e/%g` `printf` formatting, `exit()` stdio flushing and `atexit`, O(N log N) `qsort`, and zero-length pointer checks in `vmm_validate_user_ptr`.
* **GCC Toolchain Parity, CI Blocking Tests & Legacy Cleanup**: Fixed `TOOLCHAIN=gcc` global stack-protector builds, wired all 8 host Vanilla test suites into blocking `make check`, and retired the legacy kernel-side GUI/WM and 32-bit i386 target.

## Earlier Milestones
* Built the initial `x86_64` Limine Long Mode kernel (PMM, 4-level VMM, TLSF heap, SLAB cache, 256-level priority scheduler, SMP bringup, futexes, signals, SHM, `AF_UNIX`, DevFS/ProcFS/SysFS, E1000/VirtIO-net + lwIP).
* Created `tsukasa-libc`, `tsukasa-coreutils` + `tsh`, Project Vanilla compositing desktop + widget toolkits + `devtools`, `tsukasa-disktools`, and initial ports of Doom, Lua 5.4.7, and TinyCC.
