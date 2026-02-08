ENTRY(hello_gui_main)
SECTIONS
{
    . = 0x00401000;
    .text : { *(.text) }
    .rodata : { *(.rodata) }
    .data : { *(.data) }
    .bss : { *(.bss) }
}
