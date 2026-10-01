ENTRY(efi_main)
SECTIONS {
  . = 0;
  ImageBase = .;
  .text : { *(.text*) }
  .data : { *(.data*) *(.rodata*) *(.sdata*) *(.got*) *(.bss*) *(COMMON) }
  .reloc : { *(.reloc) }
  /DISCARD/ : { *(.comment) *(.note*) *(.eh_frame*) }
}
