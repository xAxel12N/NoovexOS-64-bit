.code16
.section .text
.global _start
_start:
    cli
    ljmp $0x0000, $start2
start2:
    xor %ax, %ax
    mov %ax, %ds
    mov %ax, %es
    mov %ax, %ss
    mov $0x7C00, %sp
    sti
    mov %dl, boot_drive

    mov $dap, %si
    mov boot_drive, %dl
    mov $0x42, %ah
    int $0x13
    jc disk_error

    mov boot_drive, %dl
    ljmp $0x0000, $0x8000

disk_error:
    mov $msg, %si
pr:
    lodsb
    test %al, %al
    jz hang
    mov $0x0E, %ah
    mov $0x07, %bl
    int $0x10
    jmp pr
hang:
    hlt
    jmp hang

boot_drive:
    .byte 0
msg:
    .asciz "myboot: disk error"

.align 4
dap:
    .byte 0x10
    .byte 0x00
    .word 64
    .word 0x8000
    .word 0x0000
    .long 1
    .long 0

.org 510
.word 0xAA55

.section .note.GNU-stack,"",@progbits
