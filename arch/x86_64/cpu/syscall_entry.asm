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
extern syscall_dispatch

section .text

syscall_entry_x64:
    ; Swap to kernel GS base
    swapgs
    
    ; Save user RSP into GS scratch area (offset 48) and load kernel stack (offset 40)
    mov [gs:48], rsp
    mov rsp, [gs:40]
    
    ; Save user callee-saved registers, RIP, RFLAGS, RSP
    push qword [gs:48]          ; User RSP
    push r11                    ; Saved RFLAGS (placed by SYSCALL)
    push rcx                    ; Saved RIP (placed by SYSCALL)
    push rbp
    push rbx
    push r12
    push r13
    push r14
    push r15
    
    ; Preserve user argument registers across C dispatch
    push r10
    push r9
    push r8
    push rdx
    push rsi
    push rdi
    
    ; Prepare arguments for syscall_dispatch(nr, a1, a2, a3, a4, a5, a6):
    ; RAX = syscall number -> RDI
    ; RDI = arg1 -> RSI
    ; RSI = arg2 -> RDX
    ; RDX = arg3 -> RCX
    ; R10 = arg4 -> R8
    ; R8  = arg5 -> R9
    ; R9  = arg6 -> stack [rsp + 0]
    
    push r9                     ; 7th C argument (arg6), aligns stack to 16 bytes
    mov r9, r8                  ; arg5
    mov r8, r10                 ; arg4
    mov rcx, rdx                ; arg3
    mov rdx, rsi                ; arg2
    mov rsi, rdi                ; arg1
    mov rdi, rax                ; nr (syscall number)
    
    cld
    call syscall_dispatch
    add rsp, 8                  ; Pop 7th C argument
    
    ; Restore user argument registers (RAX preserved as return value)
    pop rdi
    pop rsi
    pop rdx
    pop r8
    pop r9
    pop r10
    
    ; Restore callee-saved registers, RIP, RFLAGS, RSP
    pop r15
    pop r14
    pop r13
    pop r12
    pop rbx
    pop rbp
    pop rcx                     ; Restore user RIP for SYSRET
    pop r11                     ; Restore user RFLAGS for SYSRET
    pop rsp                     ; Restore user RSP
    
    ; Swap back to user GS base
    swapgs
    
    ; Return to Ring 3 (64-bit operand SYSRETQ)
    o64 sysret
