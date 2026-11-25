#ifndef NVX_DRIVERS_VIDEO_AMD_PSP_CORE_H
#define NVX_DRIVERS_VIDEO_AMD_PSP_CORE_H
#include "include/nvx/base.h"
#include "drivers/video/amd/sdma/sdma_core.h"

#define APS_RING 0x0u
#define APS_RING_SZ 0x1000u
#define APS_FENCE 0x1000u
#define APS_CMD 0x2000u
#define APS_FW 0x10000u
#define APS_FWMAX 0xC0000u
#define APS_AREA 0xD0000u
#define APS_TMR_SZ 0x400000u
#define APS_TMR_AL 0x100000u
#define APS_RESP 864u

#define APS_C64 0x80u
#define APS_C67 0x83u
#define APS_C69 0x85u
#define APS_C70 0x86u
#define APS_C71 0x87u
#define APS_C81 0x91u

#define APS_CMD_SETUP_TMR 5u
#define APS_CMD_LOAD_IP_FW 6u
#define APS_CMD_LOAD_TOC 0x20u
#define APS_FW_SDMA0 9u
#define APS_FW_SDMA_TH0 71u
#define APS_FW_SDMA_TH1 72u

typedef struct { u32 ver; const char* name; int seg; const char* toc; } aps_gen_t;
typedef struct { const asd_io_t* io; const aps_gen_t* g; u32 base; volatile u8* mem; u64 mc; u32 fence, cmds, timeouts, last; int ring, tmr; u64 tmr_mc; u32 tmr_sz; const char* why; } aps_t;

const aps_gen_t* aps_gen(u32 mp0_ver);
int aps_alive(aps_t* p);
int aps_ring_up(aps_t* p,u32 us);
void aps_ring_down(aps_t* p,u32 us);
int aps_cmd(aps_t* p,u32 id,const u32* body,int n,u32 us);
u32 aps_resp(const aps_t* p,int dw);
int aps_toc_file(const u8* b,u32 n,const u8** d,u32* sz);
int aps_toc(aps_t* p,const u8* toc,u32 n,u32* tmr_sz,u32 us);
int aps_tmr(aps_t* p,u64 mc,u64 pa,u32 sz,u32 us);
int aps_load(aps_t* p,u32 type,const u8* d,u32 n,u32 us);
int aps_sdma(aps_t* p,const asd_gen_t* g,const asd_fw_t* f,u32 us);
#endif
