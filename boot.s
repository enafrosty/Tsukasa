# boot.s - Assembly entry point: Multiboot header and stack setup.
# GRUB loads this at 0x100000 and jumps to _start.
# We set up a minimal stack and call the C kernel.
#
# Stack and execution assumptions (see kernel.md):
#   - Single kernel stack: 16 KiB, stack_top is 16-byte aligned.
#   - After pushing eax/ebx (8 bytes), esp is 8-byte aligned at kernel_main entry.
#   - No separate stacks for ISRs or tasks yet; all code shares this stack.
#

.equ MB_MAGIC,        0x1BADB002
.equ MB_FLAGS,        0x00000007
.equ MB_CHECKSUM,     -(MB_MAGIC + MB_FLAGS)
.equ MB_VIDEO_MODE,   1
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
stack_bottom:
    .space 16384
stack_top:

.section .text
.global _start
.type _start, @function

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
