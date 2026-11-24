# Security Policy

## Reporting a Vulnerability

If you discover a security vulnerability, kernel memory-safety bug, Ring-3 to Ring-0 privilege escalation, or pointer-validation bypass in Project Tsukasa, please report it responsibly rather than filing a public issue with exploit details first.

* **Contact:** `iyad@heyfrosty.space`
* **GitHub:** Open a private Security Advisory on [github.com/enafrosty/Tsukasa](https://github.com/enafrosty/Tsukasa) if available.

Please include:
1. A clear description of the affected file, syscall, or driver.
2. Steps to reproduce (QEMU command line, C snippet, or serial log).
3. Your toolchain (`TOOLCHAIN=llvm` or `TOOLCHAIN=gcc`) and commit SHA.

---

## Current Security Boundaries

Tsukasa enforces the following protections on `x86_64`:
* **User/Kernel Page Isolation**: Higher-half kernel pages omit the `USER` bit in PML4/PDPT/PD/PT entries.
* **Syscall Pointer Validation**: Userspace pointers passed to system calls are validated via `vmm_validate_user_ptr()` (returning `-EFAULT` on invalid, unmapped, or kernel-half ranges).
* **Ring-3 Exception Isolation & IST1**: User-mode faults deliver termination/signals without panicking the kernel, and `#DF`/`#NMI` exceptions execute on dedicated per-CPU `IST1` stacks.
* **Compiler Hardening**: Kernel and userland compile with `-fstack-protector-strong -mstack-protector-guard=global`, `-Wall -Wextra`, and optional `UBSAN=1` instrumentation verified in CI.

## Planned Hardening

Upcoming security workstreams in [ROADMAP.md](ROADMAP.md) include:
* Hardware `EFER.NXE`, `SMEP`, `SMAP`, and `UMIP` enforcement.
* Exception-table guarded `copy_from_user` / `copy_to_user` primitives.
* PIE ASLR, ChaCha20 CSPRNG backing `/dev/urandom` and randomized stack canaries, and multi-user authentication (`login`, `passwd`, `su`, `doas`).
