#ifndef XZ_CONFIG_H
#define XZ_CONFIG_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "xz.h"
void* memcpy(void*,const void*,unsigned long);
void* memmove(void*,const void*,unsigned long);
void* memset(void*,int,unsigned long);
int memcmp(const void*,const void*,unsigned long);
void* xzk_alloc(unsigned long n);
void xzk_free(void* p);
#define XZ_DEC_X86
#define XZ_USE_CRC64
#define XZ_DEC_ANY_CHECK
#define kmalloc(size, flags) xzk_alloc(size)
#define kfree(ptr) xzk_free(ptr)
#define vmalloc(size) xzk_alloc(size)
#define vfree(ptr) xzk_free(ptr)
#define memeq(a, b, size) (memcmp(a, b, size) == 0)
#define memzero(buf, size) memset(buf, 0, size)
#define min(x, y) ((x) < (y) ? (x) : (y))
#define min_t(type, x, y) min(x, y)
#define fallthrough __attribute__((__fallthrough__))
#define __always_inline inline __attribute__((__always_inline__))
static inline uint32_t get_unaligned_le32(const uint8_t *buf){ return (uint32_t)buf[0]|((uint32_t)buf[1]<<8)|((uint32_t)buf[2]<<16)|((uint32_t)buf[3]<<24); }
static inline uint32_t get_unaligned_be32(const uint8_t *buf){ return ((uint32_t)buf[0]<<24)|((uint32_t)buf[1]<<16)|((uint32_t)buf[2]<<8)|(uint32_t)buf[3]; }
static inline void put_unaligned_le32(uint32_t val, uint8_t *buf){ buf[0]=(uint8_t)val; buf[1]=(uint8_t)(val>>8); buf[2]=(uint8_t)(val>>16); buf[3]=(uint8_t)(val>>24); }
static inline void put_unaligned_be32(uint32_t val, uint8_t *buf){ buf[0]=(uint8_t)(val>>24); buf[1]=(uint8_t)(val>>16); buf[2]=(uint8_t)(val>>8); buf[3]=(uint8_t)val; }
#define get_le32 get_unaligned_le32
#endif
