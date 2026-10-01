/* ===========================================================================
   NoovexOS 64  -  boot64.S
   From-scratch x86-64 long-mode entry.

   NoovexOS stage2 (unchanged BIOS bootloader) hands control to physical
   0x100000 in 32-bit PROTECTED mode: flat segments, A20 on, interrupts off,
   and the VBE linear framebuffer already configured with its parameters
   stored at physical 0x5000:

       0x5000  dword   LFB physical address
       0x5004  word    pitch  (bytes per scanline)
       0x5006  word    width  (pixels)
       0x5008  word    height (pixels)
       0x500A  byte    bits per pixel

   This stub:
     1. (optionally) fills 0x5000 from a Multiboot info block, so GRUB works too
     2. builds identity page tables for the low 4 GiB using 2 MiB pages
     3. enables PAE, sets EFER.LME, loads a 64-bit GDT, enables paging
     4. far-jumps into a 64-bit code segment and calls kmain()

   The kernel is a flat binary loaded at 0x100000.  Byte 24 (the Multiboot
   load_end_addr slot) is stamped post-build with the kernel size so stage2's
   size-aware loader reads exactly the right number of sectors.
   =========================================================================== */

.set KDST, 0x100000

.code32
.section .text.entry
.global _start
_start:
    jmp     start32                     /* stage2 lands at offset 0 -> jump over header */

    .align 4
mb_header:
    .long   0x1BADB002                  /* Multiboot1 magic                 (+ 4) */
    .long   0x00000007                  /* flags: ALIGN | MEMINFO | VIDINFO (+ 8) */
    .long   -(0x1BADB002 + 0x00000007)  /* checksum                         (+12) */
    .long   0                           /* header_addr   (unused)           (+16) */
    .long   0                           /* load_addr     (unused)           (+20) */
    .long   0                           /* load_end_addr <- kernel size here (+24) */
    .long   0                           /* bss_end_addr  (unused)           (+28) */
    .long   0                           /* entry_addr    (unused)           (+32) */
    .long   0                           /* mode_type = linear framebuffer   (+36) */
    .long   1024                        /* preferred width                  (+40) */
    .long   768                         /* preferred height                 (+44) */
    .long   32                          /* preferred depth (bpp)            (+48) */

start32:
    cli
    cld
    /* If a bootloader (myboot) pre-loaded the WAD it sets 0x501C='WAD!'; keep its
       0x5010/0x5018 in that case, otherwise clear them (old native stage2 leaves garbage). */
    cmpl    $0x21444157, 0x501C
    je      1f
    movl    $0, 0x5010                   /* WAD module addr (0 = none / use FAT) */
    movl    $0, 0x5018                   /* WAD module size */
1:

    /* ---- If a Multiboot loader started us, fill the 0x5000 block from EBX.
            Native stage2 boot enters with EAX != this magic and 0x5000 set. ---- */
    cmpl    $0x2BADB002, %eax
    jne     1f
    movl    88(%ebx),  %eax             /* framebuffer_addr low 32    */
    movl    %eax, 0x5000
    movl    92(%ebx),  %eax             /* framebuffer_addr high 32   */
    movl    %eax, 0x5004
    movl    96(%ebx),  %eax             /* framebuffer_pitch (32-bit) */
    movl    %eax, 0x5008
    movl    100(%ebx), %eax             /* framebuffer_width          */
    movw    %ax,  0x500C
    movl    104(%ebx), %eax             /* framebuffer_height         */
    movw    %ax,  0x500E
    movb    108(%ebx), %al              /* framebuffer_bpp            */
    movb    %al,  0x5020

    /* ---- Multiboot module (the WAD): copy the first module clear of the kernel
            (to 0x5800000 = 88 MiB, above .bss) and record addr+size for kmain. ---- */
    movl    20(%ebx), %ecx              /* mods_count */
    testl   %ecx, %ecx
    jz      1f
    movl    24(%ebx), %esi              /* mods_addr -> first module struct */
    movl    0(%esi), %eax               /* mod_start */
    movl    4(%esi), %edx               /* mod_end   */
    subl    %eax, %edx                  /* edx = size */
    jle     1f
    movl    %edx, 0x5018                /* store size */
    movl    %eax, %esi                  /* src = mod_start */
    movl    $0x5800000, %edi            /* dst = 88 MiB */
    movl    %edx, %ebp                  /* save size (no stack yet) */
    movl    %edx, %ecx
    shrl    $2, %ecx
    rep     movsl                       /* copy dwords */
    movl    %ebp, %ecx
    andl    $3, %ecx
    rep     movsb                       /* copy remainder */
    movl    $0x5800000, 0x5010          /* WAD now lives at 88 MiB */
1:

    /* ---- zero the six page-table pages (PML4, PDPT, PD0..PD3) ---- */
    movl    $pml4, %edi
    movl    $(6*4096/4), %ecx
    xorl    %eax, %eax
    rep     stosl

    /* ---- PML4[0] -> PDPT ---- */
    movl    $pdpt, %eax
    orl     $0x03, %eax                 /* present | writable */
    movl    %eax, pml4

    /* ---- PDPT[0..3] -> PD0..PD3 ---- */
    movl    $pd0, %eax
    orl     $0x03, %eax
    movl    %eax, pdpt + 0
    movl    $pd1, %eax
    orl     $0x03, %eax
    movl    %eax, pdpt + 8
    movl    $pd2, %eax
    orl     $0x03, %eax
    movl    %eax, pdpt + 16
    movl    $pd3, %eax
    orl     $0x03, %eax
    movl    %eax, pdpt + 24

    /* ---- fill PD0..PD3: 2048 entries * 2 MiB = identity map 0 .. 4 GiB ---- */
    movl    $pd0, %edi                  /* dest PDE                     */
    xorl    %esi, %esi                  /* running physical address     */
    movl    $2048, %ecx                 /* number of 2 MiB pages        */
fill_pd:
    movl    %esi, %eax
    orl     $0x83, %eax                 /* present | writable | 2 MiB   */
    movl    %eax, (%edi)                /* low dword of PDE             */
    movl    $0,   4(%edi)               /* high dword = 0 (phys < 4GiB) */
    addl    $0x200000, %esi             /* += 2 MiB                     */
    addl    $8, %edi
    decl    %ecx
    jnz     fill_pd

    /* ---- enable PAE (CR4.bit5) ---- */
    movl    %cr4, %eax
    orl     $0x20, %eax
    movl    %eax, %cr4

    /* ---- load CR3 with PML4 physical address ---- */
    movl    $pml4, %eax
    movl    %eax, %cr3

    /* ---- set EFER.LME (MSR 0xC0000080, bit 8) ---- */
    movl    $0xC0000080, %ecx
    rdmsr
    orl     $0x100, %eax
    wrmsr

    /* ---- load the 64-bit GDT (6-byte pseudo-descriptor, 32-bit lgdt) ---- */
    lgdt    gdt64_ptr

    /* ---- enable paging (CR0.PG) -> activates long mode (compat submode) ---- */
    movl    %cr0, %eax
    orl     $0x80000001, %eax           /* PG | PE */
    movl    %eax, %cr0

    /* ---- far jump reloads CS with the L=1 descriptor -> true 64-bit mode ---- */
    ljmp    $0x08, $long_entry

/* --------------------------------------------------------------------------- */
.code64
long_entry:
    movw    $0x10, %ax
    movw    %ax, %ds
    movw    %ax, %es
    movw    %ax, %ss
    movw    %ax, %fs
    movw    %ax, %gs
    movq    $0x700000, %rsp             /* proven-free stack top */
    xorq    %rbp, %rbp
    call    kmain
1:  hlt
    jmp     1b

/* ---- UEFI 64-bit entry: reached via a far/near jump from BOOTX64.EFI, already
   in long mode. Framebuffer info is already at 0x5000 (the loader filled it).
   We load our own GDT, reload segments, set a stack, and call kmain. ---- */
.global uefi_entry
uefi_entry:
    lgdt    gdt64_ptr(%rip)
    /* reload CS via a far return */
    lea     .Luefi_cs(%rip), %rax
    pushq   $0x08
    pushq   %rax
    lretq
.Luefi_cs:
    movw    $0x10, %ax
    movw    %ax, %ds
    movw    %ax, %es
    movw    %ax, %ss
    movw    %ax, %fs
    movw    %ax, %gs
    movq    $0x700000, %rsp
    xorq    %rbp, %rbp
    call    kmain
1:  hlt
    jmp     1b

/* --------------------------------------------------------------------------- */
    .align 8
gdt64:
    .quad   0x0000000000000000          /* 0x00  null                        */
    .quad   0x00209B0000000000          /* 0x08  64-bit code (L=1,P,DPL0,R/X,accessed preset) */
    .quad   0x0000930000000000          /* 0x10  data        (P,DPL0,R/W,accessed preset) */
gdt64_end:
gdt64_ptr:
    .word   gdt64_end - gdt64 - 1
    .long   gdt64                        /* 32-bit base (GDT lives < 4 GiB)   */

/* --------------------------------------------------------------------------- */
.section .bss
    .align 4096
pml4:   .space 4096
pdpt:   .space 4096
pd0:    .space 4096
pd1:    .space 4096
pd2:    .space 4096
pd3:    .space 4096

.section .note.GNU-stack,"",@progbits
