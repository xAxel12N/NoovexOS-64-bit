ENTRY(_start)
SECTIONS {
  . = 0x200000;
  .text : { *(.text .text.*) *(.s3tramp) }
  .rodata : { *(.rodata .rodata.*) }
  .data : { *(.data .data.*) }
  _edata = .;
  .bss : { *(.bss .bss.* COMMON) }
  _end = .;
  /DISCARD/ : { *(.note*) *(.eh_frame*) *(.comment) }
}
