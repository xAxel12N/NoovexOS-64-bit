#include "disk.h"
#include "string.h"

extern int bios_disk_read(uint32_t lba, uint32_t count);

int disk_read(uint32_t lba, uint32_t count, uint8_t* dest){
    while(count){
        uint32_t chunk = count > DISK_CHUNK ? DISK_CHUNK : count;
        int ok = -1;
        for(int tries = 0; tries < 3; tries++){
            if(bios_disk_read(lba, chunk) == 0){ ok = 0; break; }
        }
        if(ok != 0) return -1;
        memcpy(dest, (const void*)DISK_BOUNCE, chunk * 512);
        dest += chunk * 512;
        lba += chunk;
        count -= chunk;
    }
    return 0;
}
