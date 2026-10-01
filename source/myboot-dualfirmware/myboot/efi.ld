ENTRY(efi_main)
SECTIONS
{
    . = 0x1000;
    .text : { *(.text .text.*) }
    . = ALIGN(16);
    .rodata : { *(.rodata .rodata.*) }
    . = ALIGN(16);
    .data : { *(.data .data.*) }
    . = ALIGN(16);
    __blob_end = .;
    .bss : { *(.bss .bss.*) *(COMMON) }
    . = ALIGN(16);
    __image_end = .;
    /DISCARD/ : { *(.comment) *(.note*) *(.eh_frame*) *(.dynamic) *(.dynsym) *(.dynstr) *(.hash) *(.gnu*) }
}
