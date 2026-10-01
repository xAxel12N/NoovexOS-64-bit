#ifndef DISK_H
#define DISK_H
#include "types.h"
#define DISK_BOUNCE 0x20000u
#define DISK_CHUNK 64u
int disk_read(uint32_t lba, uint32_t count, uint8_t* dest);
#endif
