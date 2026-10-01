#include "elf64.h"
#include "string.h"

static uint32_t rd16(const uint8_t* p){ return (uint32_t)p[0] | ((uint32_t)p[1]<<8); }
static uint32_t rd32(const uint8_t* p){ return (uint32_t)p[0] | ((uint32_t)p[1]<<8) | ((uint32_t)p[2]<<16) | ((uint32_t)p[3]<<24); }
static uint64_t rd64(const uint8_t* p){ return (uint64_t)rd32(p) | ((uint64_t)rd32(p + 4) << 32); }

int elf64_load(uint8_t* img, uint32_t len, uint64_t* entry_out){
    if(len < 64) return -1;
    if(img[0] != 0x7F || img[1] != 'E' || img[2] != 'L' || img[3] != 'F') return -1;
    if(img[4] != 2) return -1;
    if(rd16(img + 18) != 0x3E) return -1;
    uint64_t entry = rd64(img + 24);
    uint32_t phoff = (uint32_t)rd64(img + 32);
    uint32_t phentsz = rd16(img + 54);
    uint32_t phnum = rd16(img + 56);
    if(phoff + phnum * phentsz > len) return -1;
    for(uint32_t i = 0; i < phnum; i++){
        uint8_t* ph = img + phoff + i * phentsz;
        uint32_t type = rd32(ph + 0);
        if(type != 1) continue;
        uint32_t offset = (uint32_t)rd64(ph + 8);
        uint32_t paddr  = (uint32_t)rd64(ph + 24);
        uint32_t filesz = (uint32_t)rd64(ph + 32);
        uint32_t memsz  = (uint32_t)rd64(ph + 40);
        if(offset + filesz > len) return -1;
        memcpy((void*)paddr, img + offset, filesz);
        if(memsz > filesz) memset((void*)(paddr + filesz), 0, memsz - filesz);
    }
    *entry_out = entry;
    return 0;
}
