/* NoovexOS 64 - flat kernel image loaded by stage2 at physical 0x100000.
   .text.entry is placed first so _start sits exactly at 0x100000.
   .bss is parked at 0x01300000 (19 MiB), above the image, boot stack and DMA buffers,
   so the early 32-bit code can build page tables there before paging is on. */
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

  /DISCARD/ : { *(.comment) *(.note*) *(.eh_frame*) *(.eh_frame_hdr) }
}
ASSERT(__bss_end <= 0x18000000, "kernel bss is too large: move big buffers to the heap")
ASSERT(__file_end <= 0x06500000, "kernel image is too large: it must end below 101 MiB")
