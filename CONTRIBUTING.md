# Contributing to Project Tsukasa

Thank you for your interest in contributing to Project Tsukasa! Because a bug in freestanding kernel or memory-management code can silently corrupt state or triple-fault a virtual CPU, every change goes through strict code-style checks and clean-tree verification.

---

## Getting Started

1. **Fork and clone** the repository, then install the toolchain:
   * Fedora: `./setup_fedora.sh`
   * Debian / Ubuntu / WSL2: `./setup_wsl.sh`
2. **Build and boot** the `x86_64` Limine ISO:
   ```bash
   make -j$(nproc) iso-x86_64
   ./run_qemu.sh
   ```
3. **Iterate quickly on GUI code** using the SDL2 host emulator:
   ```bash
   cd vanilla
   make host-run
   ```

---

## Branching & Pull Requests

* Never commit directly to `main`. Create a descriptive branch such as `feat/<topic>` or `fix/<topic>`.
* Keep pull requests focused on a single subsystem or well-scoped change. Note unrelated bugs or follow-ups in the PR description instead of bundling drive-by refactors.
* Write commit messages and PR descriptions clearly for future maintainers reading `git log`. Keep comments and commit messages free of emojis and em/en dashes.
* Never edit vendored upstream code (`net/third_party/lwip/`, `fs/third_party/lwext4/`, `vanilla/include/stb_*.h`, `vanilla/devtools/vendor/microui/`, `tsukasa-ports/ports/*`) or generated artifacts (`vanilla/server/default_font.c`, `lib/ksymbols_table.c`, `.limine/`) without documenting upstream provenance in [docs/THIRD_PARTY.md](docs/THIRD_PARTY.md).

---

## Coding Conventions

* **Language & Formatting**: C11 freestanding (`-ffreestanding -fno-pie -mno-red-zone`), NASM `elf64` assembly, **4 spaces** per indent level, LF line endings, and **no tabs in C/H/ASM/Python** (see [.editorconfig](.editorconfig)).
* **Naming**: `snake_case` functions prefixed by subsystem (`vfs_`, `vmm_`, `pagecache_`, `ahci_`, `compositor_`, `blt_`), `UPPER_SNAKE` macros, `_t` typedef suffixes. Mark module-local symbols `static`.
* **Error Codes**: Return negative POSIX errno values from `include/errno.h` (`-EINVAL`, `-EFAULT`, `-ENOENT`, `-ENOMEM`, `-EIO`).

### Mandatory File Header

Every `.c`, `.h`, `.asm`, `.s`, and shell script must open with the Project Tsukasa GPL-3.0 header (updating only the description line):

```c
/*
 * Project Tsukasa - <Brief Subsystem / File Description>
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
```

---

## Kernel & Syscall Invariants

1. **Validate Every User Pointer**: Any syscall or driver entrypoint accepting a Ring-3 pointer must validate buffers with `vmm_validate_user_ptr()` (via `user_ok(ptr, len)` / `user_str_ok(ptr)` in `syscall/syscall_table.c`) and return `-EFAULT` on invalid ranges.
2. **Respect Interrupt & Lock Contexts**: Never allocate heap memory, sleep, or call blocking VFS operations inside an interrupt handler or while holding a spinlock (`spin_lock_irqsave`). Follow the lock hierarchy:
   `VFS / Mount Lock` -> `Page Cache Lock` -> `Driver / Device Lock` -> `Scheduler Lock`.
3. **Keep Policy Out of Kernel Space**: Window management, desktop shell widgets, and GUI layout belong in `vanilla/`, not the kernel.
4. **Register New Files**: Add new `.c` sources to the appropriate `Makefile`, and stage any runtime guest ELF binary under `build/initrd/bin/` in the top-level `Makefile`.

---

## Verification Checklist Before Submitting a PR

At minimum, run and report the exact commands you executed on a clean build:

1. **Clean Build (LLVM and ideally GCC)**:
   ```bash
   make -j$(nproc) iso-x86_64
   make -j$(nproc) TOOLCHAIN=gcc iso-x86_64
   ```
2. **Headless Boot & Kernel Selftests**:
   ```bash
   bash scripts/ci/boot-test.sh tsukasa.iso boot.log
   ```
3. **Full Host + Guest Test Suite (`make check`)**:
   ```bash
   make check
   ```
4. **Subsystem Checks (as applicable)**:
   * GUI / Project Vanilla changes: `make -C vanilla host` and `make visual-test`
   * Block driver / VFS / filesystem changes: `python3 scripts/test/test-storage-matrix.py --iso tsukasa.iso --machine both --mode probe --require-driver`
   * Installer / boot / partition changes: `python3 scripts/test/test-installer-e2e.py --iso tsukasa.iso --storage ahci`

Do not disable tests or relax assertions to make CI pass; investigate or report failures transparently in your PR.
