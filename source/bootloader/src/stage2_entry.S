.code16
.section .text.boot,"ax",@progbits
.global stage2_start
stage2_start:
    cli
    xor %ax, %ax
    mov %ax, %ds
    mov %ax, %es
    mov %ax, %ss
    mov %dl, g_boot_drive
    mov $0x7C00, %sp
    call enable_a20
    call do_e820
    lgdt gdtr
    mov %cr0, %eax
    or  $1, %eax
    mov %eax, %cr0
    ljmp $0x08, $pm_entry

enable_a20:
    call check_a20
    jnz a20_ok
    mov $0x2401, %ax
    int $0x15
    call check_a20
    jnz a20_ok
    in $0x92, %al
    test $2, %al
    jnz a20_after_fast
    or $2, %al
    and $0xFE, %al
    out %al, $0x92
a20_after_fast:
    call check_a20
    jnz a20_ok
    call a20_kbc
    call check_a20
    jnz a20_ok
    mov $a20_msg, %si
    call rm_print
a20_dead:
    hlt
    jmp a20_dead
a20_ok:
    ret

check_a20:
    push %ds
    push %es
    push %si
    push %di
    push %bx
    xor %ax, %ax
    mov %ax, %ds
    mov $0x0500, %si
    mov $0xFFFF, %ax
    mov %ax, %es
    mov $0x0510, %di
    mov (%si), %bl
    mov %es:(%di), %bh
    movb $0x00, (%si)
    movb $0xFF, %es:(%di)
    mov (%si), %al
    mov %bl, (%si)
    mov %bh, %es:(%di)
    cmp $0xFF, %al
    mov $0, %ax
    je check_a20_done
    mov $1, %ax
check_a20_done:
    pop %bx
    pop %di
    pop %si
    pop %es
    pop %ds
    test %ax, %ax
    ret

a20_kbc:
    cli
    call kbc_wait_in
    mov $0xAD, %al
    out %al, $0x64
    call kbc_wait_in
    mov $0xD0, %al
    out %al, $0x64
    call kbc_wait_out
    in $0x60, %al
    push %ax
    call kbc_wait_in
    mov $0xD1, %al
    out %al, $0x64
    call kbc_wait_in
    pop %ax
    or $2, %al
    out %al, $0x60
    call kbc_wait_in
    mov $0xAE, %al
    out %al, $0x64
    call kbc_wait_in
    sti
    ret

kbc_wait_in:
    in $0x64, %al
    test $2, %al
    jnz kbc_wait_in
    ret

kbc_wait_out:
    in $0x64, %al
    test $1, %al
    jz kbc_wait_out
    ret

rm_print:
    lodsb
    test %al, %al
    jz rm_print_done
    mov $0x0E, %ah
    mov $0x07, %bl
    int $0x10
    jmp rm_print
rm_print_done:
    ret

a20_msg:
    .asciz "myboot: A20 line error"

do_e820:
    mov $0x1000, %di
    xor %ebx, %ebx
    xor %ebp, %ebp
e_next:
    movl $1, %es:20(%di)
    mov $0xE820, %eax
    mov $24, %ecx
    mov $0x534D4150, %edx
    int $0x15
    jc  e_done
    cmp $0x534D4150, %eax
    jne e_done
    inc %ebp
    add $24, %di
    cmp $64, %bp
    jae e_done
    test %ebx, %ebx
    jnz e_next
e_done:
    movl %ebp, 0x0FF0
    ret

.align 8
gdt:
    .quad 0x0000000000000000
    .quad 0x00CF9A000000FFFF
    .quad 0x00CF92000000FFFF
    .quad 0x00009A000000FFFF
    .quad 0x000092000000FFFF
gdt_end:
.global gdtr
gdtr:
    .word gdt_end - gdt - 1
    .long gdt
.global g_boot_drive
g_boot_drive:
    .byte 0

.code32
.section .text,"ax",@progbits
.global pm_entry
pm_entry:
    mov $0x10, %ax
    mov %ax, %ds
    mov %ax, %es
    mov %ax, %fs
    mov %ax, %gs
    mov %ax, %ss
    mov $0x80000, %esp
    movl $__bss_start, %edi
    movl $__bss_end, %ecx
    sub  %edi, %ecx
    xor  %eax, %eax
    cld
    rep stosb
    call stage2_main
1:  cli
    hlt
    jmp 1b

.global mb_jump
mb_jump:
    mov 4(%esp), %edx
    mov 8(%esp), %ebx
    mov $0x2BADB002, %eax
    cli
    jmp *%edx

.global raw_boot
raw_boot:
    mov 4(%esp), %esi
    mov 8(%esp), %ecx
    mov 12(%esp), %ebx
    mov $0x100000, %edi
    cld
    rep movsb
    mov $0x2BADB002, %eax
    cli
    ljmp $0x08, $0x100000


.code32
.global enter_long_mode
enter_long_mode:
    mov 4(%esp), %eax
    mov %eax, 0x76000
    mov 8(%esp), %eax
    mov %eax, 0x76004
    mov 12(%esp), %eax
    mov %eax, 0x76008

    mov $0x70000, %edi
    mov $0x1800, %ecx
    xor %eax, %eax
    cld
    rep stosl

    movl $0x71003, 0x70000
    movl $0x72003, 0x71000
    movl $0x73003, 0x71008
    movl $0x74003, 0x71010
    movl $0x75003, 0x71018

    mov $0x72000, %edi
    xor %ecx, %ecx
    xor %esi, %esi
lm_fill:
    mov %esi, %eax
    or $0x83, %eax
    mov %eax, (%edi)
    movl $0, 4(%edi)
    add $8, %edi
    add $0x200000, %esi
    inc %ecx
    cmp $2048, %ecx
    jb lm_fill

    mov $0x70000, %eax
    mov %eax, %cr3
    mov %cr4, %eax
    or $0x20, %eax
    mov %eax, %cr4
    mov $0xC0000080, %ecx
    rdmsr
    or $0x100, %eax
    wrmsr
    mov %cr0, %eax
    or $0x80000000, %eax
    mov %eax, %cr0
    lgdt gdt64ptr
    ljmp $0x08, $lm64

.code64
lm64:
    mov $0x10, %ax
    mov %ax, %ds
    mov %ax, %es
    mov %ax, %fs
    mov %ax, %gs
    mov %ax, %ss
    mov $0x80000, %esp
    mov $0x76000, %r10d
    mov 4(%r10), %eax
    shl $32, %rax
    mov 0(%r10), %ecx
    or %rcx, %rax
    mov %rax, %r11
    mov 8(%r10), %esi
    mov $0x2BADB002, %edi
    mov %esi, %ebx
    mov $0x2BADB002, %eax
    jmp *%r11

.align 16
gdt64:
    .quad 0x0000000000000000
    .quad 0x00AF9A000000FFFF
    .quad 0x00CF92000000FFFF
gdt64_end:
gdt64ptr:
    .word gdt64_end - gdt64 - 1
    .long gdt64

.section .note.GNU-stack,"",@progbits
