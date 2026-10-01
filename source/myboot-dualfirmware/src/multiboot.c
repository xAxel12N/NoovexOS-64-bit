#include "multiboot.h"
#include "vbe.h"
#include "elf.h"
#include "string.h"

#define E820_COUNT_ADDR 0x00000FF0u
#define E820_ENTRY_ADDR 0x00001000u

struct e820_entry {
    uint64_t base;
    uint64_t len;
    uint32_t type;
    uint32_t acpi;
} __attribute__((packed));

static uint32_t rd32(const uint8_t* p){ return (uint32_t)p[0] | ((uint32_t)p[1]<<8) | ((uint32_t)p[2]<<16) | ((uint32_t)p[3]<<24); }

static int mb_scan(uint8_t* img, uint32_t len, struct mb_header* hdr, uint32_t* off_out){
    uint32_t limit = len < 8192 ? len : 8192;
    if(limit < 12) return -1;
    for(uint32_t o = 0; o + 12 <= limit; o += 4){
        uint32_t magic = rd32(img + o);
        if(magic != MB_HEADER_MAGIC) continue;
        uint32_t flags = rd32(img + o + 4);
        uint32_t csum  = rd32(img + o + 8);
        if((uint32_t)(magic + flags + csum) != 0) continue;
        hdr->magic = magic;
        hdr->flags = flags;
        hdr->checksum = csum;
        if(flags & MB_FLAG_ADDR){
            hdr->header_addr   = rd32(img + o + 12);
            hdr->load_addr     = rd32(img + o + 16);
            hdr->load_end_addr = rd32(img + o + 20);
            hdr->bss_end_addr  = rd32(img + o + 24);
            hdr->entry_addr    = rd32(img + o + 28);
        }
        *off_out = o;
        return 0;
    }
    return -1;
}

int mb_load_kernel(uint8_t* img, uint32_t len, uint32_t* entry_out){
    struct mb_header hdr;
    uint32_t off;
    if(mb_scan(img, len, &hdr, &off) != 0) return -1;
    if(hdr.flags & MB_FLAG_ADDR){
        uint32_t skew = hdr.header_addr - hdr.load_addr;
        if(skew > off) return -1;
        uint32_t file_off = off - skew;
        uint32_t copy_len;
        if(hdr.load_end_addr != 0) copy_len = hdr.load_end_addr - hdr.load_addr;
        else copy_len = len - file_off;
        if(file_off + copy_len > len) return -1;
        memcpy((void*)hdr.load_addr, img + file_off, copy_len);
        if(hdr.bss_end_addr > hdr.load_addr + copy_len)
            memset((void*)(hdr.load_addr + copy_len), 0, hdr.bss_end_addr - (hdr.load_addr + copy_len));
        *entry_out = hdr.entry_addr;
        return 0;
    }
    return elf_load(img, len, entry_out);
}

struct mb_info* mb_build_info(const char* cmdline){
    static struct mb_info info;
    static struct mb_mmap_entry mmap[64];
    static char loader_name[] = "myboot";
    static char cmdbuf[128];

    memset(&info, 0, sizeof(info));
    volatile uint32_t* countp;
    struct e820_entry* src;
    __asm__("" : "=r"(countp) : "0"((uint32_t)E820_COUNT_ADDR));
    __asm__("" : "=r"(src) : "0"((uint32_t)E820_ENTRY_ADDR));
    uint32_t count = *countp;
    if(count > 64) count = 64;

    uint32_t mem_upper = 0;
    for(uint32_t i = 0; i < count; i++){
        mmap[i].size = 20;
        mmap[i].addr = src[i].base;
        mmap[i].len  = src[i].len;
        mmap[i].type = src[i].type;
        if(src[i].type == 1){
            uint64_t base = src[i].base;
            uint64_t end  = base + src[i].len;
            if(base <= 0x100000ull && end > 0x100000ull){
                uint32_t kb = (uint32_t)((end - 0x100000ull) >> 10);
                if(kb > mem_upper) mem_upper = kb;
            }
        }
    }

    info.flags = 0;
    info.mem_lower = 640;
    info.mem_upper = mem_upper;
    info.flags |= 0x01;

    if(count > 0){
        info.mmap_addr = (uint32_t)(uint32_t*)mmap;
        info.mmap_length = count * sizeof(struct mb_mmap_entry);
        info.flags |= 0x40;
    }

    if(cmdline && cmdline[0]){
        strncpy_z(cmdbuf, cmdline, sizeof(cmdbuf));
        info.cmdline = (uint32_t)(uint32_t*)cmdbuf;
        info.flags |= 0x04;
    }

    info.boot_loader_name = (uint32_t)(uint32_t*)loader_name;
    info.flags |= 0x200;

    return &info;
}

void mb_set_fb(struct mb_info* info, const struct fb_info* fb){
    info->framebuffer_addr = (uint64_t)fb->addr;
    info->framebuffer_pitch = fb->pitch;
    info->framebuffer_width = fb->width;
    info->framebuffer_height = fb->height;
    info->framebuffer_bpp = fb->bpp;
    info->framebuffer_type = 1;
    info->fb_red_pos = fb->red_pos;
    info->fb_red_size = fb->red_size;
    info->fb_green_pos = fb->green_pos;
    info->fb_green_size = fb->green_size;
    info->fb_blue_pos = fb->blue_pos;
    info->fb_blue_size = fb->blue_size;
    info->flags |= 0x1000;
}
