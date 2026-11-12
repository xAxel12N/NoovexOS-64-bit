SECTIONS
{
  . = SIZEOF_HEADERS;
  .hash : { *(.hash) } :text
  .gnu.hash : { *(.gnu.hash) }
  .dynsym : { *(.dynsym) }
  .dynstr : { *(.dynstr) }
  .gnu.version : { *(.gnu.version) }
  .gnu.version_d : { *(.gnu.version_d) }
  .gnu.version_r : { *(.gnu.version_r) }
  .dynamic : { *(.dynamic) } :text :dynamic
  .rodata : { *(.rodata*) } :text
  .note : { *(.note.*) } :text :note
  .eh_frame_hdr : { *(.eh_frame_hdr) } :text :eh
  .eh_frame : { KEEP(*(.eh_frame)) } :text
  .text : { *(.text*) } :text
  /DISCARD/ : { *(.data*) *(.bss*) *(.comment) *(.got*) *(.plt*) }
  . = ALIGN(4096);
  nvx_vvar = .;
}
PHDRS
{
  text PT_LOAD FILEHDR PHDRS FLAGS(5);
  dynamic PT_DYNAMIC FLAGS(4);
  note PT_NOTE FLAGS(4);
  eh PT_GNU_EH_FRAME FLAGS(4);
}
