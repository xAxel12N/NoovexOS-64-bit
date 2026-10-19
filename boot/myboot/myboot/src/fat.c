#include "fat.h"
#include "disk.h"
#include "string.h"

static uint32_t g_part_lba;
static uint16_t g_bytes_per_sec;
static uint8_t  g_sec_per_clus;
static uint32_t g_fat_begin;
static uint32_t g_cluster_begin;
static uint32_t g_root_cluster;

static uint8_t secbuf[512];
static uint8_t fatbuf[512];
static uint32_t fatbuf_lba = 0xFFFFFFFFu;

static uint32_t rd16(const uint8_t* p){ return (uint32_t)p[0] | ((uint32_t)p[1] << 8); }
static uint32_t rd32(const uint8_t* p){ return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24); }

int fat_mount(uint32_t partition_lba){
    g_part_lba = partition_lba;
    if(disk_read(partition_lba, 1, secbuf) != 0) return -1;
    g_bytes_per_sec = (uint16_t)rd16(secbuf + 11);
    g_sec_per_clus  = secbuf[13];
    uint32_t reserved = rd16(secbuf + 14);
    uint8_t num_fats  = secbuf[16];
    uint32_t fat_size = rd32(secbuf + 36);
    g_root_cluster    = rd32(secbuf + 44);
    if(g_bytes_per_sec != 512) return -1;
    if(g_sec_per_clus == 0) return -1;
    if(fat_size == 0) return -1;
    g_fat_begin = partition_lba + reserved;
    g_cluster_begin = g_fat_begin + (uint32_t)num_fats * fat_size;
    fatbuf_lba = 0xFFFFFFFFu;
    return 0;
}

static uint32_t cluster_to_lba(uint32_t c){
    return g_cluster_begin + (c - 2) * g_sec_per_clus;
}

static uint32_t next_cluster(uint32_t c){
    uint32_t off = c * 4;
    uint32_t sec = g_fat_begin + (off / 512);
    uint32_t idx = off % 512;
    if(sec != fatbuf_lba){
        if(disk_read(sec, 1, fatbuf) != 0) return 0x0FFFFFFFu;
        fatbuf_lba = sec;
    }
    return rd32(fatbuf + idx) & 0x0FFFFFFFu;
}

static void to83(const char* name, char* out){
    for(int i = 0; i < 11; i++) out[i] = ' ';
    int i = 0, o = 0;
    while(name[i] && name[i] != '.' && o < 8){
        char ch = name[i];
        if(ch >= 'a' && ch <= 'z') ch = (char)(ch - 32);
        out[o++] = ch;
        i++;
    }
    while(name[i] && name[i] != '.') i++;
    if(name[i] == '.'){
        i++;
        o = 8;
        while(name[i] && o < 11){
            char ch = name[i];
            if(ch >= 'a' && ch <= 'z') ch = (char)(ch - 32);
            out[o++] = ch;
            i++;
        }
    }
}

static int scan_dir(uint32_t dir_cluster, const char* name83, uint32_t* out_cluster, uint32_t* out_size, int* out_isdir){
    uint32_t cluster = dir_cluster;
    uint32_t guard = 0;
    while(cluster >= 2 && cluster < 0x0FFFFFF8u){
        uint32_t lba = cluster_to_lba(cluster);
        for(uint32_t s = 0; s < g_sec_per_clus; s++){
            if(disk_read(lba + s, 1, secbuf) != 0) return 0;
            for(int e = 0; e < 512; e += 32){
                uint8_t* ent = secbuf + e;
                if(ent[0] == 0x00) return 0;
                if(ent[0] == 0xE5) continue;
                uint8_t attr = ent[11];
                if(attr == 0x0F) continue;
                if(attr & 0x08) continue;
                if(memcmp(ent, name83, 11) == 0){
                    uint32_t hi = rd16(ent + 20);
                    uint32_t lo = rd16(ent + 26);
                    *out_cluster = (hi << 16) | lo;
                    *out_size = rd32(ent + 28);
                    *out_isdir = (attr & 0x10) ? 1 : 0;
                    return 1;
                }
            }
        }
        cluster = next_cluster(cluster);
        if(++guard > 100000u) return 0;
    }
    return 0;
}

static int fat_find(const char* path, uint32_t* out_cluster, uint32_t* out_size, int* out_isdir){
    while(*path == '/') path++;
    uint32_t cur = g_root_cluster;
    char comp[13];
    char name83[11];
    while(*path){
        int n = 0;
        while(*path && *path != '/' && n < 12){ comp[n++] = *path++; }
        comp[n] = 0;
        while(*path && *path != '/') path++;
        while(*path == '/') path++;
        to83(comp, name83);
        uint32_t c, sz;
        int isd;
        if(!scan_dir(cur, name83, &c, &sz, &isd)) return 0;
        if(*path == 0){
            *out_cluster = c;
            *out_size = sz;
            *out_isdir = isd;
            return 1;
        }
        if(!isd) return 0;
        cur = c;
    }
    return 0;
}

int fat_read_file(const char* path, uint8_t* dest, uint32_t max, uint32_t* size_out){
    uint32_t cluster, size;
    int isd;
    if(!fat_find(path, &cluster, &size, &isd)) return -1;
    if(isd) return -1;
    if(size > max) return -1;
    uint32_t remaining = size;
    uint32_t guard = 0;
    while(remaining > 0 && cluster >= 2 && cluster < 0x0FFFFFF8u){
        uint32_t lba = cluster_to_lba(cluster);
        for(uint32_t s = 0; s < g_sec_per_clus && remaining > 0; s++){
            if(disk_read(lba + s, 1, secbuf) != 0) return -1;
            uint32_t tocopy = remaining < 512 ? remaining : 512;
            memcpy(dest, secbuf, tocopy);
            dest += tocopy;
            remaining -= tocopy;
        }
        cluster = next_cluster(cluster);
        if(++guard > 1000000u) return -1;
    }
    if(size_out) *size_out = size;
    return 0;
}

int fat_readdir(int index, char* name, uint32_t* size, int* isdir){
    uint32_t cluster = g_root_cluster;
    int n = 0;
    uint32_t guard = 0;
    while(cluster >= 2 && cluster < 0x0FFFFFF8u){
        uint32_t lba = cluster_to_lba(cluster);
        for(uint32_t s = 0; s < g_sec_per_clus; s++){
            if(disk_read(lba + s, 1, secbuf) != 0) return -1;
            for(int e = 0; e < 512; e += 32){
                uint8_t* ent = secbuf + e;
                if(ent[0] == 0x00) return -1;
                if(ent[0] == 0xE5) continue;
                uint8_t attr = ent[11];
                if(attr == 0x0F) continue;
                if(attr & 0x08) continue;
                if(n == index){
                    int o = 0;
                    for(int i = 0; i < 8 && ent[i] != ' '; i++) name[o++] = (char)ent[i];
                    if(ent[8] != ' '){
                        name[o++] = '.';
                        for(int i = 8; i < 11 && ent[i] != ' '; i++) name[o++] = (char)ent[i];
                    }
                    name[o] = 0;
                    *size = rd32(ent + 28);
                    *isdir = (attr & 0x10) ? 1 : 0;
                    return 0;
                }
                n++;
            }
        }
        cluster = next_cluster(cluster);
        if(++guard > 100000u) return -1;
    }
    return -1;
}
