#include "pit.h"
#include "io.h"

void pit_wait_ms(uint32_t ms){
    while(ms){
        uint32_t chunk = ms > 50 ? 50 : ms;
        ms -= chunk;
        uint32_t count = 1193u * chunk;
        if(count > 65535u) count = 65535u;
        uint8_t p = (uint8_t)((inb(0x61) & 0xFC) | 0x01);
        outb(0x61, p);
        outb(0x43, 0xB0);
        outb(0x42, (uint8_t)count);
        outb(0x42, (uint8_t)(count >> 8));
        uint32_t guard = 0;
        while(!(inb(0x61) & 0x20)){
            if(++guard > 50000000u) break;
        }
    }
}
