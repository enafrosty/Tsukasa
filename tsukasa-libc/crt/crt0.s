/*
 * Project Tsukasa — CRT0 C Runtime Startup Entry Point (GAS Syntax)
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

.intel_syntax noprefix
.global _start
.extern main
.extern exit

.section .text
_start:
    xor rbp, rbp
    mov rdi, [rsp]
    lea rsi, [rsp + 8]
    lea rdx, [rsp + rdi*8 + 16]
    and rsp, -16
    call main
    mov rdi, rax
    call exit

.hang:
    mov rax, 60
    syscall
    jmp .hang
