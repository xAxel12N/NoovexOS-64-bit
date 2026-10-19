#ifndef ELF_H
#define ELF_H
#include "types.h"
int elf_load(uint8_t* img, uint32_t len, uint32_t* entry_out);
#endif
