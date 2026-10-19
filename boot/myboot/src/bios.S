.section .text,"ax",@progbits

.code32
.global bios_disk_read
bios_disk_read:
    push %ebp
    mov %esp, %ebp
    push %ebx
    push %esi
    push %edi

    mov 8(%ebp), %eax
    mov 12(%ebp), %edx

    mov $0x0500, %edi
    movb $0x10, (%edi)
    movb $0x00, 1(%edi)
    movw %dx, 2(%edi)
    movw $0x0000, 4(%edi)
    movw $0x2000, 6(%edi)
    mov %eax, 8(%edi)
    movl $0, 12(%edi)

    mov %esp, bd_saved_esp
    cli
    ljmp $0x18, $bd_pm16

.code16
bd_pm16:
    mov $0x20, %ax
    mov %ax, %ds
    mov %ax, %es
    mov %ax, %fs
    mov %ax, %gs
    mov %ax, %ss
    mov %cr0, %eax
    and $0xFFFFFFFE, %eax
    mov %eax, %cr0
    ljmp $0x0000, $bd_rm

bd_rm:
    xor %ax, %ax
    mov %ax, %ds
    mov %ax, %es
    mov %ax, %fs
    mov %ax, %gs
    mov %ax, %ss
    mov $0x7C00, %sp
    lidt bd_rm_idt
    sti
    mov $0x0500, %si
    mov g_boot_drive, %dl
    mov $0x42, %ah
    int $0x13
    setc %bl
    cli
    lgdt gdtr
    mov %cr0, %eax
    or $1, %eax
    mov %eax, %cr0
    ljmp $0x08, $bd_pm32

.code32
bd_pm32:
    mov $0x10, %ax
    mov %ax, %ds
    mov %ax, %es
    mov %ax, %fs
    mov %ax, %gs
    mov %ax, %ss
    mov bd_saved_esp, %esp
    movzbl %bl, %eax
    pop %edi
    pop %esi
    pop %ebx
    pop %ebp
    ret

.code32
.global bios_int10
bios_int10:
    push %ebp
    mov %esp, %ebp
    push %ebx
    push %esi
    push %edi
    mov %esp, bi_saved_esp
    cli
    ljmp $0x18, $bi_pm16

.code16
bi_pm16:
    mov $0x20, %ax
    mov %ax, %ds
    mov %ax, %es
    mov %ax, %fs
    mov %ax, %gs
    mov %ax, %ss
    mov %cr0, %eax
    and $0xFFFFFFFE, %eax
    mov %eax, %cr0
    ljmp $0x0000, $bi_rm

bi_rm:
    xor %ax, %ax
    mov %ax, %ds
    mov %ax, %ss
    mov $0x7C00, %sp
    lidt bd_rm_idt
    sti
    mov 0x4300, %eax
    mov 0x4304, %ebx
    mov 0x4308, %ecx
    mov 0x430C, %edx
    mov 0x4310, %esi
    mov 0x4314, %edi
    mov 0x4318, %es
    int $0x10
    mov %eax, 0x4300
    mov %ebx, 0x4304
    mov %ecx, 0x4308
    mov %edx, 0x430C
    mov %esi, 0x4310
    mov %edi, 0x4314
    cli
    lgdt gdtr
    mov %cr0, %eax
    or $1, %eax
    mov %eax, %cr0
    ljmp $0x08, $bi_pm32

.code32
bi_pm32:
    mov $0x10, %ax
    mov %ax, %ds
    mov %ax, %es
    mov %ax, %fs
    mov %ax, %gs
    mov %ax, %ss
    mov bi_saved_esp, %esp
    pop %edi
    pop %esi
    pop %ebx
    pop %ebp
    ret

.code32
.global bios_get_font
bios_get_font:
    push %ebp
    mov %esp, %ebp
    push %ebx
    push %esi
    push %edi
    mov %esp, bf_saved_esp
    cli
    ljmp $0x18, $bf_pm16

.code16
bf_pm16:
    mov $0x20, %ax
    mov %ax, %ds
    mov %ax, %es
    mov %ax, %fs
    mov %ax, %gs
    mov %ax, %ss
    mov %cr0, %eax
    and $0xFFFFFFFE, %eax
    mov %eax, %cr0
    ljmp $0x0000, $bf_rm

bf_rm:
    xor %ax, %ax
    mov %ax, %ds
    mov %ax, %ss
    mov $0x7C00, %sp
    lidt bd_rm_idt
    sti
    mov $0x1130, %ax
    mov $0x06, %bh
    int $0x10
    push %ds
    mov %es, %ax
    mov %ax, %ds
    mov %bp, %si
    xor %ax, %ax
    mov %ax, %es
    mov $0x5000, %di
    mov $4096, %cx
    cld
    rep movsb
    pop %ds
    cli
    lgdt gdtr
    mov %cr0, %eax
    or $1, %eax
    mov %eax, %cr0
    ljmp $0x08, $bf_pm32

.code32
bf_pm32:
    mov $0x10, %ax
    mov %ax, %ds
    mov %ax, %es
    mov %ax, %fs
    mov %ax, %gs
    mov %ax, %ss
    mov bf_saved_esp, %esp
    pop %edi
    pop %esi
    pop %ebx
    pop %ebp
    ret

.section .data
bd_saved_esp:
    .long 0
bi_saved_esp:
    .long 0
bf_saved_esp:
    .long 0
bd_rm_idt:
    .word 0x03FF
    .long 0x00000000

.section .note.GNU-stack,"",@progbits
