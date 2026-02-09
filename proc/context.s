# context.s - Context switch. Saves current stack pointer, loads next.
# void context_switch(uint32_t *save_esp, uint32_t next_esp);
# Call with interrupts disabled.
#

.section .text
.global context_switch
.type context_switch, @function

context_switch:
    movl 4(%esp), %eax    /* save_esp (pointer) */
    movl 8(%esp), %ecx    /* next_esp */

    /* Save current stack pointer. */
    movl %esp, (%eax)

    /* Load new stack pointer. */
    movl %ecx, %esp

    /* Return into new context. */
    ret

.size context_switch, . - context_switch
