;
; Project Tsukasa — x86_64 fast SYSCALL assembly entry point
;
; Copyright (C) 2025-2026 frosty (@enafrosty) and Project Tsukasa contributors.
;
; Project Tsukasa was created and is maintained by frosty (@enafrosty).
; This program is free software: you can redistribute it and/or modify it
; under the terms of the GNU General Public License as published by the
; Free Software Foundation, either version 3 of the License, or (at your
; option) any later version. See the top-level LICENSE file.
;
; This program is distributed in the hope that it will be useful, but
; WITHOUT ANY WARRANTY; without even the implied warranty of
; MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
;

BITS 64
global syscall_entry_x64
extern syscall_entry_dispatch

section .text

syscall_entry_x64:
    ; Swap to kernel GS base
    swapgs

    ; Save user RSP into GS scratch area (offset 48) and load kernel stack (offset 40)
    mov [gs:48], rsp
    mov rsp, [gs:40]

    ; Reserve optional iretq tail + 16-byte alignment padding (6 qwords), then push 16 GPR/RIP/RFLAGS/RSP qwords
    sub rsp, 48
    push qword [gs:48]          ; User RSP
    push r11                    ; Saved RFLAGS (placed by SYSCALL)
    push rcx                    ; Saved RIP (placed by SYSCALL)
    push rbp
    push rbx
    push r12
    push r13
    push r14
    push r15
    push r10
    push r9
    push r8
    push rdx
    push rsi
    push rdi
    push rax

    mov rdi, rsp
    cld
    sti
    call syscall_entry_dispatch
    cli
    test eax, eax
    jnz .iretq_return

    ; Fast return via SYSRET
    pop rax
    pop rdi
    pop rsi
    pop rdx
    pop r8
    pop r9
    pop r10
    pop r15
    pop r14
    pop r13
    pop r12
    pop rbx
    pop rbp
    pop rcx                     ; Restore user RIP for SYSRET
    pop r11                     ; Restore user RFLAGS for SYSRET
    pop rsp                     ; Restore user RSP

    swapgs
    o64 sysret

.iretq_return:
    ; Full register restore via IRETQ (preserves independent RCX/R11/RIP/RFLAGS)
    pop rax
    pop rdi
    pop rsi
    pop rdx
    pop r8
    pop r9
    pop r10
    pop r15
    pop r14
    pop r13
    pop r12
    pop rbx
    pop rbp
    pop rcx
    pop r11
    add rsp, 8                  ; Skip syscall-entry rsp slot; land on iret_rip
    swapgs
    iretq

; Forked child resumes here via iretq from process_schedule_core with IF=0
; and RSP pointing directly at its cloned syscall_frame_t.
global fork_child_return_x64
fork_child_return_x64:
    cli
    xor eax, eax
    pop rdi
    pop rsi
    pop rdx
    pop r8
    pop r9
    pop r10
    pop r15
    pop r14
    pop r13
    pop r12
    pop rbx
    pop rbp
    pop rcx
    pop r11
    pop rsp
    swapgs
    o64 sysret
