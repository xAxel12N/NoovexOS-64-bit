/* NoovexOS 64 - ELF64 kernel for myboot's dual-firmware handoff.
   Same physical layout as the flat build (.text @ 1 MiB, .bss @ 19 MiB),
   but kept as an ELF (myboot loads program segments) with __bss_start
   exported so the entry stub can zero .bss. */
ENTRY(_start)
SECTIONS {
  . = 0x100000;
  .text : {
    *(.text.entry)
    *(.text .text.* .text.unlikely)
  }
  .rodata : { *(.rodata .rodata.*) }
  . = ALIGN(8);
  .init_array : { __init_array_start = .; KEEP(*(SORT_BY_INIT_PRIORITY(.init_array.*))) KEEP(*(.init_array .ctors)) __init_array_end = .; }
  . = ALIGN(16);
  __immutable_end = .;
  .data   : { *(.data .data.*) }
  . = ALIGN(16);
  __file_end = .;

  . = 0x10000000;
  __bss_start = .;
  .bss (NOLOAD) : { *(.bss .bss.* COMMON) }
  . = ALIGN(8);
  __bss_end = .;
}
ASSERT(__file_end <= 0x06500000, "kernel image is too large: it must end below 101 MiB")
