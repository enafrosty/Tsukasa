OUTPUT_FORMAT(elf64-x86-64)
ENTRY(_start)

KERNEL_VMA = 0xFFFFFFFF80000000;

SECTIONS
{
    . = KERNEL_VMA + 0x100000;

    _kernel_start = .;

    .limine_requests_start ALIGN(8) : {
        KEEP(*(.limine_requests_start))
    }

    .limine_requests ALIGN(8) : {
        KEEP(*(.limine_requests))
    }

    .limine_requests_end ALIGN(8) : {
        KEEP(*(.limine_requests_end))
    }

    .text ALIGN(4K) : {
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

    _kernel_end = .;

    /DISCARD/ : {
        *(.eh_frame)
        *(.note .note.*)
        *(.comment)
    }
}