BITS 64

section .bss
align 16
global x64_boot_stack_top
x64_boot_stack_bottom:
    resb 32768
x64_boot_stack_top:

section .text
global _start
extern tsukasa_x64_entry

_start:
    cli
    lea rsp, [rel x64_boot_stack_top]
    xor rbp, rbp
    call tsukasa_x64_entry

.hang:
    hlt
    jmp .hang

section .note.GNU-stack noalloc noexec nowrite progbits
