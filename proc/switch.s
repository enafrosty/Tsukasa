;
; switch.s - Switch to user mode via iret.
; void switch_to_user(uint32_t eip, uint32_t esp, uint32_t eflags);
;

.section .text
.global switch_to_user
.type switch_to_user, @function

switch_to_user:
    movl 4(%esp), %eax     /* eip */
    movl 8(%esp), %ecx     /* esp (user stack) */
    movl 12(%esp), %edx    /* eflags */

    /* Build iret frame on current stack. Order: ss, esp, eflags, cs, eip */
    pushl $0x23            /* user data segment (ss) */
    pushl %ecx             /* user esp */
    pushl %edx             /* eflags (with IF=1 for interrupts) */
    pushl $0x1B            /* user code segment (cs) */
    pushl %eax             /* eip */

    iret

.size switch_to_user, . - switch_to_user
