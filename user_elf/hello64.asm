;
; Project Tsukasa — SPEC-C01 acceptance test ELF: raw Linux-convention syscalls via int 0x80
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
global _start

section .text
_start:
    mov rax, 2                  ; SYS open
    lea rdi, [rel path]
    mov rsi, 2                  ; TSUKASA_O_WRONLY
    int 0x80
    test rax, rax
    js .fail

    mov rdi, rax                ; SYS dup2(fd, 1)
    mov rsi, 1
    mov rax, 33
    int 0x80

    mov rax, 1                  ; SYS write(1, "hi", 2)
    mov rdi, 1
    lea rsi, [rel msg]
    mov rdx, 2
    int 0x80
    cmp rax, 2
    jne .fail

    mov rdi, 42                 ; exit(42) => acceptance PASS
    jmp .exit
.fail:
    mov rdi, 1
.exit:
    mov rax, 60                 ; SYS exit
    int 0x80
.hang:
    jmp .hang

section .rodata
path: db "/dev/tty0", 0
msg:  db "hi"
