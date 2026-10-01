#include "elf.h"
#include "string.h"

static uint32_t rd32(const uint8_t* p){ return (uint32_t)p[0] | ((uint32_t)p[1]<<8) | ((uint32_t)p[2]<<16) | ((uint32_t)p[3]<<24); }
static uint32_t rd16(const uint8_t* p){ return (uint32_t)p[0] | ((uint32_t)p[1]<<8); }

int elf_load(uint8_t* img, uint32_t len, uint32_t* entry_out){
    if(len < 52) return -1;
    if(img[0] != 0x7F || img[1] != 'E' || img[2] != 'L' || img[3] != 'F') return -1;
    if(img[4] != 1) return -1;
    if(rd16(img + 18) != 3) return -1;
    uint32_t entry   = rd32(img + 24);
    uint32_t phoff   = rd32(img + 28);
    uint32_t phentsz = rd16(img + 42);
    uint32_t phnum   = rd16(img + 44);
    if(phoff + phnum * phentsz > len) return -1;
    for(uint32_t i = 0; i < phnum; i++){
        uint8_t* ph = img + phoff + i * phentsz;
        uint32_t type = rd32(ph + 0);
        if(type != 1) continue;
        uint32_t offset = rd32(ph + 4);
        uint32_t paddr  = rd32(ph + 12);
        uint32_t filesz = rd32(ph + 16);
        uint32_t memsz  = rd32(ph + 20);
        if(offset + filesz > len) return -1;
        memcpy((void*)paddr, img + offset, filesz);
        if(memsz > filesz) memset((void*)(paddr + filesz), 0, memsz - filesz);
    }
    *entry_out = entry;
    return 0;
}
