/* stage2.S - NoovexOS custom bootloader, stage 2.  Loaded at phys 0x8000 (16-bit
   real mode, ds=es=ss=0, sp=0x7C00, boot drive at 0x0500).
     1. enable A20
     2. set a VBE linear-framebuffer mode, store params at 0x5000..0x500A
     3. load the kernel (LBA 33..) to 0x100000 via "unreal mode" + INT 13h
     4. enter 32-bit protected mode and jump to the kernel at 0x100000 (EAX=0) */
.code16
.section .text
.globl _start2
_start2:
    cli
    xorw    %ax, %ax
    movw    %ax, %ds
    movw    %ax, %es
    movw    %ax, %ss
    movw    $0x7C00, %sp

    /* ---- 1. enable A20 (fast gate) ---- */
    inb     $0x92, %al
    testb   $0x02, %al
    jnz     1f
    orb     $0x02, %al
    andb    $0xFE, %al             /* don't accidentally reset */
    outb    %al, $0x92
1:

    call    e820_scan
    /* ---- 2. set a VBE mode with a linear framebuffer ---- */
    movw    $0x118, %bx            /* 1024x768 (x24/x32) */
    call    try_mode
    jnc     vok
    movw    $0x115, %bx            /* 800x600 */
    call    try_mode
    jnc     vok
    movw    $0x112, %bx            /* 640x480 */
    call    try_mode
    jnc     vok
    movw    $vmsg, %si
    call    prints
    jmp     halt
vok:

    /* ---- 3. load the kernel to 0x100000 (unreal mode) ---- */
    call    unreal_on
    movl    kern_lba, %eax         /* LBA cursor (patched by mkboot.py) */
    movl    kern_secs, %edx        /* sectors remaining (patched) */
    movl    $0x100000, %edi        /* physical dest cursor */
kl_loop:
    testl   %edx, %edx
    jz      kl_done
    movl    %edx, %ecx
    cmpl    $64, %ecx
    jbe     1f
    movl    $64, %ecx
1:  movw    %cx, dap_cnt           /* sectors this round */
    movl    %eax, dap_lba          /* LBA */
    pushal
    movw    $dap, %si
    movb    $0x42, %ah
    movb    0x0500, %dl
    int     $0x13
    jc      kl_err
    popal
    /* copy ecx*512 bytes from phys 0x20000 (buffer) to es:edi (0x100000+) */
    pushal
    movl    %ecx, %esi             /* remember sector count */
    shll    $9, %ecx               /* bytes = sectors*512 */
    movl    $0x20000, %esi         /* src physical */
    /* rep movsb  with 32-bit addressing (a32) : copy esi->edi (es flat) */
    .byte   0x67                   /* addr32 */
    .byte   0xF3, 0xA4             /* rep movsb (uses ESI/EDI via a32, ES:EDI dest) */
    popal
    /* advance */
    movl    %ecx, %ebx
    shll    $9, %ebx
    addl    %ebx, %edi             /* dest += bytes */
    addl    %ecx, %eax             /* lba += sectors */
    subl    %ecx, %edx             /* remaining -= sectors */
    jmp     kl_loop
kl_err:
    popal
    movw    $kmsg, %si
    call    prints
    jmp     halt
kl_done:

    /* ---- 4. enter protected mode and jump to the kernel ---- */
    cli
    lgdt    gdtr
    movl    %cr0, %eax
    orl     $1, %eax
    movl    %eax, %cr0
    ljmp    $0x08, $pm32

/* e820_scan:
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
try_mode: BX = VBE mode number.  On success sets the mode with a linear
   framebuffer, stores params at 0x5000, returns CF=0.  CF=1 on failure. */
try_mode:
    pushw   %bx
    movw    $0x4F01, %ax
    movw    %bx, %cx
    movw    $vinfo, %di
    int     $0x10
    cmpw    $0x004F, %ax
    jne     tm_fail
    testb   $0x80, vinfo+0         /* attr bit 7: linear framebuffer available */
    jz      tm_fail
    popw    %bx
    pushw   %bx
    movw    %bx, %cx
    orw     $0x4000, %bx           /* request LFB */
    movw    $0x4F02, %ax
    int     $0x10
    cmpw    $0x004F, %ax
    jne     tm_fail
    /* store framebuffer params at 0x5000 (ds=0 -> phys) */
    movl    vinfo+40, %eax         /* PhysBasePtr */
    movl    %eax, 0x5000
    movw    vinfo+16, %ax          /* BytesPerScanLine (pitch) */
    movw    %ax, 0x5004
    movw    vinfo+18, %ax          /* XResolution */
    movw    %ax, 0x5006
    movw    vinfo+20, %ax          /* YResolution */
    movw    %ax, 0x5008
    movb    vinfo+25, %al          /* BitsPerPixel */
    movb    %al, 0x500A
    popw    %bx
    clc
    ret
tm_fail:
    popw    %bx
    stc
    ret

/* unreal_on: briefly enter PM, load DS and ES with a flat 4 GiB data
   descriptor, return to real mode so 32-bit offsets address all of memory.
   Both stay flat (base 0) for the whole load: INT 13h reads its DAP at DS:SI
   (SI small) and writes to the DAP's own seg:off buffer, so a flat DS is fine. */
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
    movl    %eax, %cr0            /* DS/ES keep 4 GiB limit, base 0 = unreal mode */
    sti
    ret

prints:                            /* SI -> asciz, print via BIOS teletype */
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
kmsg: .asciz " kern load err"

/* ---- protected mode ---- */
.code32
pm32:
    movw    $0x10, %ax
    movw    %ax, %ds
    movw    %ax, %es
    movw    %ax, %ss
    movw    %ax, %fs
    movw    %ax, %gs
    movl    $0x9F000, %esp
    xorl    %eax, %eax             /* EAX != Multiboot magic -> kernel native path */
    ljmp    $0x08, $0x100000       /* into the kernel (_start -> start32) */

/* ---- GDT: null / 32-bit code / 32-bit data (flat 4 GiB) ---- */
.align 8
gdt:
    .quad   0x0000000000000000
    .quad   0x00CF9A000000FFFF     /* 0x08 code32 base0 lim4G */
    .quad   0x00CF92000000FFFF     /* 0x10 data32 base0 lim4G */
gdt_end:
gdtr:
    .word   gdt_end - gdt - 1
    .long   gdt

/* ---- disk address packet + VBE buffers ---- */
.align 4
dap:
    .byte   0x10
    .byte   0
dap_cnt:
    .word   0                      /* sectors this transfer */
    .word   0x0000                 /* buffer offset */
    .word   0x2000                 /* buffer segment -> phys 0x20000 */
dap_lba:
    .quad   0

.globl kern_lba
kern_lba:  .long 33                /* patched by mkboot.py: kernel start LBA */
.globl kern_secs
kern_secs: .long 0                 /* patched by mkboot.py: kernel sector count */

vinfo:  .space 256                 /* VBE mode info block */
