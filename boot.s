/*
 * Project Tsukasa — Assembly entry point: Multiboot header and stack setup
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

.equ MB_MAGIC,        0x1BADB002
.equ MB_FLAGS,        0x00000007
.equ MB_CHECKSUM,     -(MB_MAGIC + MB_FLAGS)
.equ MB_VIDEO_MODE,   0
.equ MB_VIDEO_WIDTH,  1024
.equ MB_VIDEO_HEIGHT, 768
.equ MB_VIDEO_DEPTH,  32

.section .multiboot
.align 4
.long MB_MAGIC
.long MB_FLAGS
.long MB_CHECKSUM
/* Offsets 12-31: reserved (address fields only if flags[16]; video at 32+ per spec). */
.fill 5, 4, 0
/* Offsets 32-44: mode_type, width, height, depth (required when flags[2] VIDEO_MODE). */
.long MB_VIDEO_MODE
.long MB_VIDEO_WIDTH
.long MB_VIDEO_HEIGHT
.long MB_VIDEO_DEPTH

.section .bss
.align 16
.global stack_top
stack_bottom:
    .space 16384
stack_top:

.section .text
.global _start
.type _start, @function

.section .note.GNU-stack,"",@progbits

_start:
    mov $stack_top, %esp
    mov $stack_top, %ebp
    push %ebx
    push %eax
    call kernel_main
halt:
    cli
    hlt
    jmp halt

.size _start, . - _start
