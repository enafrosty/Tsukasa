/*
 * Project Tsukasa — x86_64 clone Thread Creation Entry (Userspace)
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
.global tsk_clone
.extern __syscall_check

.section .text

tsk_clone:
    test rdi, rdi
    jz .einval
    push r12
    push r13
    mov r12, rdi
    mov r13, rcx

    mov rax, 56
    mov rdi, rdx
    and rsi, -16
    mov rdx, r8
    mov r10, qword ptr [rsp + 24]
    mov r8, r9
    xor r9d, r9d
    syscall

    test rax, rax
    jz .child_start

    pop r13
    pop r12
    cmp rax, -4096
    ja .neg_errno
    ret

.einval:
    mov rax, -22
.neg_errno:
    mov rdi, rax
    jmp __syscall_check

.child_start:
    xor ebp, ebp
    mov rdi, r13
    call r12
    mov rdi, rax
    mov rax, 60
    syscall
.hang:
    jmp .hang

.section .note.GNU-stack,"",@progbits
