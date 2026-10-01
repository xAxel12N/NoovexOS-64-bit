/* NoovexOS 64 - flat kernel image loaded by stage2 at physical 0x100000.
   .text.entry is placed first so _start sits exactly at 0x100000.
   .bss is parked at 0x00C00000 (12 MiB), well clear of the <=1 MiB kernel,
   so the early 32-bit code can build page tables there before paging is on. */
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
  .bss (NOLOAD) : { *(.bss .bss.* COMMON) }
  . = ALIGN(8);
  __bss_end = .;

  /DISCARD/ : { *(.comment) *(.note*) *(.eh_frame*) *(.eh_frame_hdr) }
}
