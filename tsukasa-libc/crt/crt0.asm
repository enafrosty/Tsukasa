;
; Project Tsukasa — CRT0 C Runtime Startup Entry Point
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
extern main
extern exit

section .text
_start:
    ; Terminate stack frame pointer chain per System V AMD64 ABI
    xor rbp, rbp

    ; Extract arguments from user stack prepared by kernel ELF loader:
    ; [rsp] = argc
    ; [rsp + 8] = argv[0]
    ; [rsp + 8 * (argc + 1)] = NULL (argv terminator)
    ; [rsp + 8 * (argc + 2)] = envp[0]
    mov rdi, [rsp]              ; arg1: argc
    lea rsi, [rsp + 8]          ; arg2: argv
    lea rdx, [rsp + rdi*8 + 16] ; arg3: envp = &argv[argc + 1]

    ; Align stack to 16-byte boundary before calling main
    and rsp, -16

    ; Call main(argc, argv, envp)
    call main

    ; Exit with return value from main
    mov rdi, rax
    call exit

    ; Infinite loop trap if exit returns
.hang:
    mov rax, 60                 ; SYS_exit
    syscall
    jmp .hang
