# isr.s - Exception stub handlers. Push vector (and 0 for no error code), then jump to common.
# CPU exceptions 0-31. Common handler calls idt_handler(vector, error_code) then we iret.
#

.section .text

.macro ISR_NOERR num
.global isr_\num
.type isr_\num, @function
isr_\num:
    pushl $0
    pushl $\num
    jmp isr_common
.size isr_\num, . - isr_\num
.endm

.macro ISR_ERR num
.global isr_\num
.type isr_\num, @function
isr_\num:
    pushl $\num
    jmp isr_common
.size isr_\num, . - isr_\num
.endm

ISR_NOERR 0
ISR_NOERR 1
ISR_NOERR 2
ISR_NOERR 3
ISR_NOERR 4
ISR_NOERR 5
ISR_NOERR 6
ISR_NOERR 7
ISR_NOERR 8
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
ISR_NOERR 29
ISR_NOERR 30
ISR_NOERR 31

.global isr_common
.type isr_common, @function
isr_common:
    popl %eax
    popl %ebx
    pushl %ebx
    pushl %eax
    call idt_handler
    addl $8, %esp
    iret
.size isr_common, . - isr_common

.global isr_ignore
.type isr_ignore, @function
isr_ignore:
    iret
.size isr_ignore, . - isr_ignore

# IRQ 1 (keyboard) -> vector 33
.global isr_33
.type isr_33, @function
isr_33:
    pushl $0
    pushl $33
    jmp irq_common
.size isr_33, . - isr_33

.global irq_common
.type irq_common, @function
irq_common:
    pusha
    pushl %ds
    pushl %es
    pushl %fs
    pushl %gs
    mov $0x10, %ax
    mov %ax, %ds
    mov %ax, %es
    mov %ax, %fs
    mov %ax, %gs
    pushl 48(%esp)
    call irq_handler
    addl $4, %esp
    popl %gs
    popl %fs
    popl %es
    popl %ds
    popa
    addl $8, %esp
    iret
.size irq_common, . - irq_common
