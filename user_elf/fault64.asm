;
; Project Tsukasa — ring-3 crash-isolation test binary
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
    ; 0xffffffff80000000 is kernel-image space: present but supervisor-only
    ; (U/S=0). A ring-3 read of it is a protection violation -> #PF.
    mov rax, 0xffffffff80000000
    mov rax, [rax]              ; faults here; control never returns to ring 3
.hang:
    jmp .hang
