#ifndef FAT_H
#define FAT_H
#include "types.h"
int fat_mount(uint32_t partition_lba);
int fat_read_file(const char* path, uint8_t* dest, uint32_t max, uint32_t* size_out);
int fat_readdir(int index, char* name, uint32_t* size, int* isdir);
#endif
