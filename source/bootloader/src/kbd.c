#include "kbd.h"
#include "io.h"

static const char ascii_map[128] = {
    0,0,'1','2','3','4','5','6','7','8','9','0','-','=','\b','\t',
    'q','w','e','r','t','y','u','i','o','p','[',']','\n',0,'a','s',
    'd','f','g','h','j','k','l',';','\'','`',0,'\\','z','x','c','v',
    'b','n','m',',','.','/',0,'*',0,' ',0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0
};

int kbd_poll(void){
    uint8_t st = inb(0x64);
    if((st & 1) == 0) return KEY_NONE;
    if(st & 0x20){ (void)inb(0x60); return KEY_NONE; }
    uint8_t sc = inb(0x60);
    if(sc & 0x80) return KEY_NONE;
    switch(sc){
        case 0x48: return KEY_UP;
        case 0x50: return KEY_DOWN;
        case 0x1C: return KEY_ENTER;
        case 0x2E: return 'c';
        case 0x02: return '1';
        case 0x03: return '2';
        case 0x04: return '3';
        case 0x05: return '4';
        case 0x06: return '5';
        case 0x07: return '6';
        case 0x08: return '7';
        case 0x09: return '8';
        case 0x0A: return '9';
        case 0x0B: return '0';
        default: return KEY_NONE;
    }
}

int kbd_getchar(void){
    for(;;){
        uint8_t st = inb(0x64);
        if((st & 1) == 0) continue;
        if(st & 0x20){ (void)inb(0x60); continue; }
        uint8_t sc = inb(0x60);
        if(sc & 0x80) continue;
        if(sc < 128){
            char c = ascii_map[sc];
            if(c) return (int)(unsigned char)c;
        }
    }
}

void kbd_flush(void){
    for(int i = 0; i < 4096; i++){
        if((inb(0x64) & 1) == 0) break;
        (void)inb(0x60);
    }
}
