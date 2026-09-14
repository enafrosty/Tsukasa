;
; Project Tsukasa — guide 02 acceptance test binary (SYSCALL/SYSRET path)
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
    ; /tmp is memfs (fs/vfs.c mount_register), the writable backend vfs_write
    ; actually implements. NOT /dev/tty0 like hello64.asm: devfs write only
    ; handles fb0, so tty writes return 0 and would mask the result of this
    ; test with an unrelated pre-existing VFS gap. See the PR notes.
    mov rax, 2                  ; open("/tmp/guide02.txt", O_WRONLY|O_CREAT)
    lea rdi, [rel path]
    mov rsi, 0x0A
    syscall
    test rax, rax
    js .fail_open

    mov rdi, rax                ; dup2(fd, 1)
    mov rsi, 1
    mov rax, 33
    syscall

    mov rax, 1                  ; write(1, "hi", 2)
    mov rdi, 1
    lea rsi, [rel msg]
    mov rdx, 2
    syscall
    cmp rax, 2
    jne .fail_write

    mov rdi, 42                 ; exit(42) => acceptance PASS
    jmp .exit
    ; Failure exit codes name the failing step: 10 = open, otherwise write()'s
    ; own return value truncated to 8 bits (0 = wrote nothing, 0xF7 = -EBADF...).
.fail_open:
    mov rdi, 10
    jmp .exit
.fail_write:
    mov rdi, rax
    and rdi, 0xff
.exit:
    mov rax, 60
    syscall
.hang:
    jmp .hang

section .rodata
path: db "/tmp/guide02.txt", 0
msg:  db "hi"
