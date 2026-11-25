#ifndef NVX_DRIVERS_VIDEO_AMD_FW_H
#define NVX_DRIVERS_VIDEO_AMD_FW_H

#include "include/nvx/base.h"

#define AMD_DMUB_PSP_HDR 0x100u
#define AMD_DMUB_PSP_FTR 0x100u

typedef struct {
    const char* name;
    u8* blob; u32 len;
    u32 ucode_ver, ucode_off, ucode_len, inst_const, bss;
    u16 hdr_maj, hdr_min;
    const u8* inst; const u8* bss_init;
    int ok;
    const char* why;
} amd_dmcub_t;

extern amd_dmcub_t g_amd_dmcub;
extern int (*amd_fw_read)(const char* path,u8** out,u32* len);

const char* amd_fw_dmcub_name(u32 dcn_ver,u16 did);
int amd_fw_dmcub_parse(amd_dmcub_t* f,u8* b,u32 n);
int amd_fw_dmcub_load(u32 dcn_ver,u16 did);
void amd_fw_dmcub_free(void);
int amd_fw_describe(char* o,int cap);

#endif
