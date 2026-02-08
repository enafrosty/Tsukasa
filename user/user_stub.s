;
; user_stub.s - Minimal user-mode stub. Calls int 0x80 (yield) and loops.
; Must be at an address we can mark PTE_USER. Placed in .user_stub section.
;

.section .user_stub, "ax"
.global _user_stub_start
.global _user_stub_entry

_user_stub_start:
_user_stub_entry:
    movl $0, %eax    /* SYS_YIELD */
    int $0x80
    jmp _user_stub_entry
