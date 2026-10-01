ENTRY(stage2_start)
SECTIONS
{
    . = 0x8000;
    .text : {
        *(.text.boot)
        *(.text .text.*)
        *(.rodata .rodata.*)
        *(.data .data.*)
    }
    . = ALIGN(4);
    __bss_start = .;
    .bss : { *(.bss .bss.*) *(COMMON) }
    . = ALIGN(4);
    __bss_end = .;
    /DISCARD/ : { *(.comment) *(.note*) *(.eh_frame*) *(.gnu*) }
}
