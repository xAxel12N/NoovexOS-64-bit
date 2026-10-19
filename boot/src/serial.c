#include "serial.h"
#include "io.h"

#define COM1 0x3F8

void serial_init(void){
    outb(COM1 + 1, 0x00);
    outb(COM1 + 3, 0x80);
    outb(COM1 + 0, 0x01);
    outb(COM1 + 1, 0x00);
    outb(COM1 + 3, 0x03);
    outb(COM1 + 2, 0xC7);
    outb(COM1 + 4, 0x0B);
}

static void tx(char c){
    for(int i = 0; i < 100000; i++){ if(inb(COM1 + 5) & 0x20) break; }
    outb(COM1, (uint8_t)c);
}

void serial_putc(char c){
    if(c == '\n') tx('\r');
    tx(c);
}
