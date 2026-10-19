#ifndef MULTIBOOT_H
#define MULTIBOOT_H
#include "types.h"

#define MB_HEADER_MAGIC 0x1BADB002u
#define MB_BOOT_MAGIC   0x2BADB002u
#define MB_FLAG_ADDR    0x00010000u

struct mb_header {
    uint32_t magic;
    uint32_t flags;
    uint32_t checksum;
    uint32_t header_addr;
    uint32_t load_addr;
    uint32_t load_end_addr;
    uint32_t bss_end_addr;
    uint32_t entry_addr;
};

struct mb_mmap_entry {
    uint32_t size;
    uint64_t addr;
    uint64_t len;
    uint32_t type;
} __attribute__((packed));

struct mb_info {
    uint32_t flags;
    uint32_t mem_lower;
    uint32_t mem_upper;
    uint32_t boot_device;
    uint32_t cmdline;
    uint32_t mods_count;
    uint32_t mods_addr;
    uint32_t syms[4];
    uint32_t mmap_length;
    uint32_t mmap_addr;
    uint32_t drives_length;
    uint32_t drives_addr;
    uint32_t config_table;
    uint32_t boot_loader_name;
    uint32_t apm_table;
    uint32_t vbe_control_info;
    uint32_t vbe_mode_info;
    uint16_t vbe_mode;
    uint16_t vbe_interface_seg;
    uint16_t vbe_interface_off;
    uint16_t vbe_interface_len;
    uint64_t framebuffer_addr;
    uint32_t framebuffer_pitch;
    uint32_t framebuffer_width;
    uint32_t framebuffer_height;
    uint8_t framebuffer_bpp;
    uint8_t framebuffer_type;
    uint8_t fb_red_pos;
    uint8_t fb_red_size;
    uint8_t fb_green_pos;
    uint8_t fb_green_size;
    uint8_t fb_blue_pos;
    uint8_t fb_blue_size;
} __attribute__((packed));

struct fb_info;
int mb_load_kernel(uint8_t* img, uint32_t len, uint32_t* entry_out);
struct mb_info* mb_build_info(const char* cmdline);
void mb_set_fb(struct mb_info* info, const struct fb_info* fb);
#endif
