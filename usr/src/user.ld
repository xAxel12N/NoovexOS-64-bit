ENTRY(_start)
SECTIONS {
  . = 0x40000000000;
  .text   : { *(.text*) }
  .rodata : { *(.rodata*) }
  .data   : { *(.data*) }
  .bss    : { *(.bss* COMMON) }
}
