/* NoovexOS 64 - ELF64 kernel for myboot's dual-firmware handoff.
   Same physical layout as the flat build (.text @ 1 MiB, .bss @ 12 MiB),
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
  . = ALIGN(16);
  __immutable_end = .;
  .data   : { *(.data .data.*) }
  . = ALIGN(16);
  __file_end = .;

  . = 0x00C00000;
  __bss_start = .;
  .bss (NOLOAD) : { *(.bss .bss.* COMMON) }
  . = ALIGN(8);
  __bss_end = .;
}
