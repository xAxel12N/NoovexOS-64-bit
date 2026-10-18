/* cdboot.S - NoovexOS custom bootloader for a bootable CD (El Torito, no
   emulation).  El Torito loads this whole image to 0x7C00 in 16-bit real mode
   with DL = the CD drive number.  It then:
     1. enables A20
     2. sets a VBE linear-framebuffer mode -> params at 0x5000..0x500A
     3. reads the kernel off the CD (INT 13h AH=42h, 2048-byte sectors) to
        0x100000 via unreal mode
     4. enters 32-bit protected mode and jumps to the kernel (EAX=0)
   Load RBA of the kernel (in CD sectors) and its sector count are patched into
   kern_lba / kern_secs by mkiso.py. */
.code16
.section .text
.globl _start
_start:
    cli
    xorw    %ax, %ax
    movw    %ax, %ds
    movw    %ax, %es
    movw    %ax, %ss
    movw    $0x7000, %sp
    movb    %dl, 0x0500            /* save CD drive number */
    sti

    /* ---- A20 ---- */
    inb     $0x92, %al
    testb   $0x02, %al
    jnz     1f
    orb     $0x02, %al
    outb    %al, $0x92
1:
    call    e820_scan
    /* ---- VBE linear-framebuffer mode ---- */
    movw    $0x118, %bx
    call    try_mode
    jnc     vok
    movw    $0x115, %bx
    call    try_mode
    jnc     vok
    movw    $0x112, %bx
    call    try_mode
    jnc     vok
    movw    $vmsg, %si
    call    prints
    jmp     halt
vok:
    /* ---- load kernel from CD to 0x100000 (unreal mode) ---- */
    call    unreal_on
    /* probe the CD drive number: the passed DL may be a floppy id (VirtualBox
       hands El Torito a diskette number that can't do AH=42h).  Try it; if the
       LBA read fails, fall back to 0xE0 (the BIOS CD-ROM drive). */
    movl    kern_lba, %eax
    movw    $1, dap_cnt
    movl    %eax, dap_lba
    movb    0x0500, %dl
    movw    $dap, %si
    movb    $0x42, %ah
    int     $0x13
    jnc     probe_ok
    movb    $0xE0, 0x0500          /* fall back to the CD-ROM drive */
probe_ok:
    movl    kern_lba, %eax         /* CD LBA (2048-byte sectors), patched */
    movl    kern_secs, %edx        /* CD sectors remaining, patched */
    movl    $0x100000, %edi
kl:
    testl   %edx, %edx
    jz      kldone
    movl    %edx, %ecx
    cmpl    $16, %ecx
    jbe     1f
    movl    $16, %ecx              /* up to 16 CD sectors (32 KiB) per read */
1:  movw    %cx, dap_cnt
    movl    %eax, dap_lba
    pushal
    movw    $dap, %si
    movb    $0x42, %ah
    movb    0x0500, %dl
    int     $0x13
    jc      klerr
    popal
    /* copy ecx*2048 bytes 0x30000 -> es:edi */
    pushal
    shll    $11, %ecx              /* sectors * 2048 = bytes */
    movl    $0x30000, %esi
    .byte   0x67
    .byte   0xF3, 0xA4             /* a32 rep movsb (DS:ESI -> ES:EDI) */
    popal
    movl    %ecx, %ebx
    shll    $11, %ebx
    addl    %ebx, %edi
    addl    %ecx, %eax
    subl    %ecx, %edx
    jmp     kl
klerr:
    popal
    movw    $kmsg, %si
    call    prints
    jmp     halt
kldone:
    /* ---- protected mode -> kernel ---- */
    cli
    lgdt    gdtr
    movl    %cr0, %eax
    orl     $1, %eax
    movl    %eax, %cr0
    ljmp    $0x08, $pm32

e820_scan:
    pushal
    pushw   %es
    xorw    %ax, %ax
    movw    %ax, %es
    movw    $0x7008, %di
    xorl    %ebx, %ebx
    xorw    %bp, %bp
8:  movl    $1, %es:20(%di)
    movl    $0xE820, %eax
    movl    $24, %ecx
    movl    $0x534D4150, %edx
    int     $0x15
    jc      9f
    cmpl    $0x534D4150, %eax
    jne     9f
    jcxz    7f
    incw    %bp
    addw    $24, %di
7:  testl   %ebx, %ebx
    jz      9f
    cmpw    $40, %bp
    jb      8b
9:  movzwl  %bp, %eax
    movl    %eax, 0x7004
    movl    $0x4558564E, 0x7000
    popw    %es
    popal
    ret
try_mode:                          /* BX=mode; set LFB, store params @0x5000, CF=0 ok */
    pushw   %bx
    movw    $0x4F01, %ax
    movw    %bx, %cx
    movw    $vinfo, %di
    int     $0x10
    cmpw    $0x004F, %ax
    jne     tm_fail
    testb   $0x80, vinfo+0
    jz      tm_fail
    popw    %bx
    pushw   %bx
    movw    %bx, %cx
    orw     $0x4000, %bx
    movw    $0x4F02, %ax
    int     $0x10
    cmpw    $0x004F, %ax
    jne     tm_fail
    movl    vinfo+40, %eax
    movl    %eax, 0x5000
    movw    vinfo+16, %ax
    movw    %ax, 0x5004
    movw    vinfo+18, %ax
    movw    %ax, 0x5006
    movw    vinfo+20, %ax
    movw    %ax, 0x5008
    movb    vinfo+25, %al
    movb    %al, 0x500A
    popw    %bx
    clc
    ret
tm_fail:
    popw    %bx
    stc
    ret

unreal_on:
    cli
    lgdt    gdtr
    movl    %cr0, %eax
    orl     $1, %eax
    movl    %eax, %cr0
    movw    $0x10, %bx
    movw    %bx, %es
    movw    %bx, %ds
    movl    %cr0, %eax
    andb    $0xFE, %al
    movl    %eax, %cr0
    sti
    ret

prints:
1:  lodsb
    testb   %al, %al
    jz      2f
    movb    $0x0E, %ah
    movb    $0x07, %bl
    int     $0x10
    jmp     1b
2:  ret
halt:
    hlt
    jmp     halt
vmsg: .asciz " VBE fail"
kmsg: .asciz " CD read err"

.code32
pm32:
    movw    $0x10, %ax
    movw    %ax, %ds
    movw    %ax, %es
    movw    %ax, %ss
    movw    %ax, %fs
    movw    %ax, %gs
    movl    $0x9F000, %esp
    xorl    %eax, %eax
    ljmp    $0x08, $0x100000

.align 8
gdt:
    .quad   0x0000000000000000
    .quad   0x00CF9A000000FFFF
    .quad   0x00CF92000000FFFF
gdt_end:
gdtr:
    .word   gdt_end - gdt - 1
    .long   gdt

.align 4
dap:
    .byte   0x10
    .byte   0
dap_cnt:
    .word   0
    .word   0x0000                 /* buffer offset */
    .word   0x3000                 /* buffer segment -> phys 0x30000 */
dap_lba:
    .quad   0
.globl kern_lba
kern_lba:  .long 0                 /* patched: kernel CD LBA */
.globl kern_secs
kern_secs: .long 0                 /* patched: kernel CD sector count */
vinfo:  .space 256
