#include "rtc.h"
#include "io.h"

static uint8_t cmos_read(uint8_t reg){
    outb(0x70, reg);
    return inb(0x71);
}

uint8_t rtc_seconds(void){
    while(cmos_read(0x0A) & 0x80) { }
    uint8_t s = cmos_read(0x00);
    uint8_t b = cmos_read(0x0B);
    if(!(b & 0x04)) s = (uint8_t)((s & 0x0F) + ((s >> 4) * 10));
    return s;
}
