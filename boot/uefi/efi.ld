ENTRY(efi_main)
SECTIONS {
  . = 0;
  ImageBase = .;
  . = 0x1000;
  .text : { *(.text*) }
  . = ALIGN(0x1000);
  .data : { *(.data*) *(.rodata*) *(.sdata*) *(.got*) *(.bss*) *(COMMON) }
  . = ALIGN(0x1000);
  .reloc : { *(.reloc) }
  . = ALIGN(0x1000);
  .dynamic : { *(.dynamic) }
  .dynsym : { *(.dynsym) }
  .dynstr : { *(.dynstr) }
  .hash : { *(.hash) }
  .gnu.hash : { *(.gnu.hash) }
  /DISCARD/ : { *(.comment) *(.note*) *(.eh_frame*) }
}
