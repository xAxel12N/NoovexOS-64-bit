OUTPUT_FORMAT(elf64-x86-64)
ENTRY(kstart)
SECTIONS
{
    . = 0x00100000;
    .text : { *(.text) *(.text.*) }
    .rodata : { *(.rodata) *(.rodata.*) }
    .data : { *(.data) *(.data.*) }
    .bss : { *(.bss) *(.bss.*) *(COMMON) }
}
