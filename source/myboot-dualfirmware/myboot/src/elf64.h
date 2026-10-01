#ifndef ELF64_H
#define ELF64_H
#include "types.h"
int elf64_load(uint8_t* img, uint32_t len, uint64_t* entry_out);
#endif
