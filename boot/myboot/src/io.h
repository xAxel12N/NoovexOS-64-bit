#ifndef IO_H
#define IO_H
#include "types.h"
static inline void outb(uint16_t p, uint8_t v){ __asm__ volatile("outb %0,%1"::"a"(v),"Nd"(p)); }
static inline uint8_t inb(uint16_t p){ uint8_t r; __asm__ volatile("inb %1,%0":"=a"(r):"Nd"(p)); return r; }
static inline void outw(uint16_t p, uint16_t v){ __asm__ volatile("outw %0,%1"::"a"(v),"Nd"(p)); }
static inline uint16_t inw(uint16_t p){ uint16_t r; __asm__ volatile("inw %1,%0":"=a"(r):"Nd"(p)); return r; }
static inline void insw(uint16_t p, void* a, uint32_t c){ __asm__ volatile("cld; rep insw":"+D"(a),"+c"(c):"d"(p):"memory"); }
static inline void outsw(uint16_t p, const void* a, uint32_t c){ __asm__ volatile("cld; rep outsw":"+S"(a),"+c"(c):"d"(p)); }
static inline void io_wait(void){ outb(0x80,0); }
#endif
