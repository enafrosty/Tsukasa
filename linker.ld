/*
 * linker.ld - Linker script for kernel at 1 MiB (0x100000).
 * Multiboot spec expects kernel to be loaded at this address.
 */

ENTRY(_start)

SECTIONS
{
    /* Load and run at 1 MiB. */
    . = 0x100000;

    _kernel_start = .;

    /* Multiboot header must be in first 8 KiB of the kernel file (GRUB scans first 8KB).
     * Placing .multiboot inside .text ensures it is the first content of the first
     * PT_LOAD segment so no .note or other section can push it past 8KB. */
    .text ALIGN(4K) : {
        *(.multiboot)
        *(.text .text.*)
    }

    .rodata ALIGN(4K) : {
        *(.rodata .rodata.*)
    }

    .data ALIGN(4K) : {
        *(.data .data.*)
    }

    .bss ALIGN(4K) : {
        *(COMMON)
        *(.bss .bss.*)
    }

    /* End of kernel for debugging / size. */
    _kernel_end = .;

    /* User stub and stack: page-aligned for PTE_USER mapping. */
    . = ALIGN(4096);
    _user_stub_start = .;
    .user_stub : {
        *(.user_stub)
    }
    _user_stub_end = .;
    . = ALIGN(4096);
    _user_stack_bottom = .;
    .user_stack (NOLOAD) : {
        . += 4096;
    }
    _user_stack_top = .;
}
