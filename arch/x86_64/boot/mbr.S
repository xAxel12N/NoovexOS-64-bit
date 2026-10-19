/* mbr.S - NoovexOS custom bootloader, stage 1 (512-byte MBR).
   BIOS loads this to 0x7C00 in 16-bit real mode with DL = boot drive.
   It loads stage2 (LBA 1..32) to 0x8000 and jumps to it. */
.code16
.section .text
.globl _start
_start:
    cli
    xorw    %ax, %ax
    movw    %ax, %ds
    movw    %ax, %es
    movw    %ax, %ss
    movw    $0x7C00, %sp            /* stack grows down from below the MBR */
    movb    %dl, 0x0500            /* save BIOS boot drive at phys 0x0500 */
    sti

    movw    $dap, %si
    movb    $0x42, %ah             /* INT 13h extended read (LBA) */
    movb    0x0500, %dl
    int     $0x13
    jc      err
    ljmp    $0x0000, $0x8000       /* enter stage2 */

err:
    movw    $emsg, %si
1:  lodsb
    testb   %al, %al
    jz      2f
    movb    $0x0E, %ah
    movb    $0x07, %bl
    int     $0x10
    jmp     1b
2:  hlt
    jmp     2b
emsg: .asciz "MBR disk err"

.align 4
dap:
    .byte   0x10                   /* DAP size */
    .byte   0
    .word   32                     /* sectors to read (stage2 <= 16 KiB) */
    .word   0x8000                 /* dest offset */
    .word   0x0000                 /* dest segment -> phys 0x8000 */
    .quad   1                      /* start LBA (stage2 begins at LBA 1) */

.org 510
.word   0xAA55
