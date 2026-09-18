;
; Project Tsukasa — x86_64 Interrupt and Exception Service Routine stubs
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

section .text

global isr_x64_ignore
global isr_x64_128
global process_entry_resume
extern idt_exception_handler_x64
extern irq_handler_x64
extern process_entry_trampoline
extern syscall_dispatch

%macro PUSH_GPRS 0
    push r15
    push r14
    push r13
    push r12
    push r11
    push r10
    push r9
    push r8
    push rbp
    push rdi
    push rsi
    push rdx
    push rcx
    push rbx
    push rax
%endmacro

%macro POP_GPRS 0
    pop rax
    pop rbx
    pop rcx
    pop rdx
    pop rsi
    pop rdi
    pop rbp
    pop r8
    pop r9
    pop r10
    pop r11
    pop r12
    pop r13
    pop r14
    pop r15
%endmacro

%macro ISR_NOERR 1
global isr_x64_%1
isr_x64_%1:
    push 0
    push %1
    jmp isr_exception_common
%endmacro

%macro ISR_ERR 1
global isr_x64_%1
isr_x64_%1:
    push %1
    jmp isr_exception_common
%endmacro

%macro IRQ_STUB 1
global isr_x64_%1
isr_x64_%1:
    push %1
    PUSH_GPRS
    test qword [rsp + 136], 3
    jz %%from_kernel
    swapgs
%%from_kernel:
    mov rdi, [rsp + 120]
    mov rsi, rsp
    cld
    mov r15, rsp
    and rsp, -16
    sub rsp, 8
    call irq_handler_x64
    mov rsp, r15
    test rax, rax
    jz %%keep_rsp
    mov rsp, rax
%%keep_rsp:
    ; Ensure no latent NT/TF flag state can poison iretq task/trace semantics.
    pushfq
    pop rax
    and rax, ~((1 << 14) | (1 << 8))
    push rax
    popfq
    test qword [rsp + 136], 3
    jz %%skip_swap
    swapgs
%%skip_swap:
    POP_GPRS
    add rsp, 8
    iretq
%endmacro

ISR_NOERR 0
ISR_NOERR 1
ISR_NOERR 2
ISR_NOERR 3
ISR_NOERR 4
ISR_NOERR 5
ISR_NOERR 6
ISR_NOERR 7
ISR_ERR   8
ISR_NOERR 9
ISR_ERR   10
ISR_ERR   11
ISR_ERR   12
ISR_ERR   13
ISR_ERR   14
ISR_NOERR 15
ISR_NOERR 16
ISR_ERR   17
ISR_NOERR 18
ISR_NOERR 19
ISR_NOERR 20
ISR_ERR   21
ISR_NOERR 22
ISR_NOERR 23
ISR_NOERR 24
ISR_NOERR 25
ISR_NOERR 26
ISR_NOERR 27
ISR_NOERR 28
ISR_ERR   29
ISR_ERR   30
ISR_NOERR 31

IRQ_STUB 32
IRQ_STUB 33
IRQ_STUB 34
IRQ_STUB 35
IRQ_STUB 36
IRQ_STUB 37
IRQ_STUB 38
IRQ_STUB 39
IRQ_STUB 40
IRQ_STUB 41
IRQ_STUB 42
IRQ_STUB 43
IRQ_STUB 44
IRQ_STUB 45
IRQ_STUB 46
IRQ_STUB 47
IRQ_STUB 65

isr_exception_common:
    PUSH_GPRS
    test qword [rsp + 144], 3
    jz .from_kernel
    swapgs
.from_kernel:
    cld
    mov r15, rsp
    and rsp, -16
    sub rsp, 8
    mov rdi, r15
    call idt_exception_handler_x64
    mov rsp, r15
    test qword [rsp + 144], 3
    jz .skip_swap
    swapgs
.skip_swap:
    POP_GPRS
    add rsp, 16
    iretq

isr_x64_ignore:
    iretq

isr_x64_128:
    test qword [rsp + 8], 3
    jz .from_kernel
    swapgs
.from_kernel:
    PUSH_GPRS
    push qword [rsp + 64]
    mov r9,  [rsp + 56 + 8]
    mov r8,  [rsp + 72 + 8]
    mov rcx, [rsp + 24 + 8]
    mov rdx, [rsp + 32 + 8]
    mov rsi, [rsp + 40 + 8]
    mov rdi, [rsp + 0 + 8]
    cld
    call syscall_dispatch
    add rsp, 8
    mov [rsp + 0], rax
    POP_GPRS
    test qword [rsp + 8], 3
    jz .skip_swap
    swapgs
.skip_swap:
    iretq

process_entry_resume:
    push rbp
    mov rbp, rsp
    call process_entry_trampoline
    leave
    iretq

section .note.GNU-stack noalloc noexec nowrite progbits
