;
; Project Tsukasa — user-process C runtime entry (_start) for SDK-built apps
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
    xor rbp, rbp                    ; outermost frame marker (psABI: %rbp = 0)
    mov rdi, [rsp]                  ; argc
    lea rsi, [rsp + 8]              ; argv
    lea rdx, [rsp + rdi*8 + 16]     ; envp = &argv[argc + 1]
    ; RSP % 16 == 0 here (process entry); the call pushes 8 bytes, so main
    ; observes the normal C calling convention alignment.
    call main
    mov edi, eax                    ; exit(main's return value)
    call exit
.hang:                              ; exit() must not return; belt and braces
    jmp .hang
