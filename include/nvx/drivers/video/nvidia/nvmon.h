#ifndef NVX_DRIVERS_VIDEO_NVIDIA_NVMON_H
#define NVX_DRIVERS_VIDEO_NVIDIA_NVMON_H

#include "include/nvx/base.h"

typedef struct { u8 type, i2c, port, heads, conn, conntype, sor; } nvm_out_t;
typedef struct { char maker[4]; u16 product; char name[14]; u32 pclk_khz; u16 hact, vact, htot, vtot; u16 hz; int ok; } nvm_edid_t;
typedef struct { int active; u16 hact, vact, htot, vtot; u32 pclk; u16 hz; } nvm_head_t;

int nvm_outputs(const u8* v,int n,int dcb,nvm_out_t* out,int max);
const char* nvm_conn(u8 c);
int nvm_sor_index(u8 mask);
const char* nvm_sig(u8 t);
int nvm_edid_parse(const u8* e,nvm_edid_t* m);
int nvm_read_edid(int port,u8* e);
void nvm_decode_head(u32 total,u32 synce,u32 blanke,u32 blanks,u32 pclk,nvm_head_t* h);
const char* nvm_proto(u32 p);

#endif
