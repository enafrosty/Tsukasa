;
; Project Tsukasa — Minimal user-mode stub. Calls int 0x80 (yield) and loops
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

.section .user_stub, "ax"
.global _user_stub_start
.global _user_stub_entry

_user_stub_start:
_user_stub_entry:
    movl $0, %eax    /* SYS_YIELD */
    int $0x80
    jmp _user_stub_entry

/* Mark stack non-executable to satisfy linker and avoid deprecation warning. */
.section .note.GNU-stack,"",@progbits
