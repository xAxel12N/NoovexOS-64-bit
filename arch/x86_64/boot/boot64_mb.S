/* NoovexOS 64 - entry for myboot's dual-firmware handoff contract.
   myboot (BIOS or UEFI) loads this ELF64 to its physical addresses and enters
   here already in 64-bit long mode with paging enabled and:
       RDI = 0x2BADB002  (Multiboot magic)
       RSI = physical address of the Multiboot information structure
   myboot identity-maps >=4 GiB on both firmwares, so we don't build page tables.
   We just fill the 0x5000 framebuffer block the rest of the kernel reads,
   zero .bss, set up a stack, and call kmain(). */
.code64
.section .text.entry
.global _start
_start:
    cld
    /* ---- FIRST: copy the framebuffer info out of the mbi (RSI) to the fixed
       0x5000 block, BEFORE we zero .bss -- the loader may have placed the mbi
       inside the .bss address range, and 0x5000 is below .bss so it's safe. ---- */
    mov     %rsi, %rbx
    mov     $0x5000, %rdi
    movl    88(%rbx),  %eax
    movl    %eax, 0(%rdi)          /* 0x5000  LFB addr low 32  */
    movl    92(%rbx),  %eax
    movl    %eax, 4(%rdi)          /* 0x5004  LFB addr high 32 (full 64-bit) */
    movl    96(%rbx),  %eax
    movl    %eax, 8(%rdi)          /* 0x5008  pitch (full 32-bit bytes/line) */
    movl    100(%rbx), %eax
    movw    %ax,  12(%rdi)         /* 0x500C  width  */
    movl    104(%rbx), %eax
    movw    %ax,  14(%rdi)         /* 0x500E  height */
    movb    108(%rbx), %al
    movb    %al, 32(%rdi)          /* 0x5020  bpp    */
    movl    $0, 16(%rdi)           /* 0x5010  WAD module addr (none) */
    movl    $0, 24(%rdi)           /* 0x5018  WAD module size        */

    /* ---- now zero .bss (the loader did NOT load/zero it for us) ---- */
    lea     __bss_start(%rip), %rdi
    lea     __bss_end(%rip), %rcx
    sub     %rdi, %rcx
    xor     %eax, %eax
    rep     stosb

    /* ---- stack + into the kernel ---- */
    lea     stack_top(%rip), %rsp
    xor     %rbp, %rbp
    mov     $0x2BADB002, %edi
    call    kmain
1:  cli
    hlt
    jmp     1b

.section .bss
.align 16
stack_bottom:
    .skip   131072
stack_top:
.section .note.GNU-stack,"",@progbits

