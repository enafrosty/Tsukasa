;
; Project Tsukasa — Switch to user mode via iret
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

.section .text
.global switch_to_user
.type switch_to_user, @function

.section .note.GNU-stack,"",@progbits

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
