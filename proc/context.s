;
; Project Tsukasa — Context switch. Saves current stack pointer, loads next
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
.global context_switch
.type context_switch, @function

.section .note.GNU-stack,"",@progbits

context_switch:
    movl 4(%esp), %eax    /* save_esp (pointer) */
    movl 8(%esp), %ecx    /* next_esp */

    movl %esp, (%eax)

    movl %ecx, %esp

    ret

.size context_switch, . - context_switch
