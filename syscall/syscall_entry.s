;
; syscall_entry.s - int 0x80 syscall entry point.
; Syscall number in eax, args in ebx, ecx, edx. Return value in eax.
;

.section .text
.global isr_128
.type isr_128, @function

isr_128:
    pushl %ebx
    pushl %ecx
    pushl %edx
    pushl %esi
    pushl %edi
    pushl %ebp

    pushl %edx
    pushl %ecx
    pushl %ebx
    pushl %eax
    call syscall_handler
    addl $16, %esp

    popl %ebp
    popl %edi
    popl %esi
    popl %edx
    popl %ecx
    popl %ebx
    iret

.size isr_128, . - isr_128
