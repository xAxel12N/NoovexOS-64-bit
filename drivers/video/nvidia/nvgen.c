// SPDX-License-Identifier: MIT
// GA102 falcon/GSP boot register sequences follow nouveau nvkm/subdev/gsp/ga102.c and nvkm/falcon/ga102.c, Copyright 2021-2023 Red Hat Inc.
// SEC2 PMC reset follows nouveau nvkm/falcon/gm200.c, nvkm/engine/sec2/ga102.c, nvkm/subdev/mc/ga100.c and nvkm/subdev/top/ga100.c (MIT, Red Hat Inc.).
// Per-generation class numbers follow nouveau nvkm/subdev/gsp/rm/tu1xx.c and ga1xx.c (MIT, Copyright (c) 2025 NVIDIA CORPORATION) and NVIDIA open-gpu-kernel-modules class headers clc56f.h clc7b5.h clc7c0.h clc797.h clc670.h clc67d.h clc67e.h (MIT, Copyright (c) 2020 NVIDIA CORPORATION).
#include "include/nvx/base.h"
#include "include/nvx/arch/x86_64/time/hrtimer.h"
#include "include/nvx/drivers/video/nvidia/nvgpu.h"
#include "include/nvx/drivers/video/nvidia/nvgsp.h"
#include "include/nvx/drivers/video/nvidia/nvgsp_gen.h"

int g_nv_ampere_on=0;
const nvgsp_cls nvgsp_cls_tu10x={ 0xC46Fu,0xC461u,0xC5B5u,0x902Du,0xC597u,0xC5C0u, 0xC570u,0xC57Du,0xC57Eu,0xC57Bu,0xC57Au, 0xBB0090u,0x10u, 2,75 };
const nvgsp_cls nvgsp_cls_ga10x={ 0xC56Fu,0xC561u,0xC7B5u,0x902Du,0xC797u,0xC7C0u, 0xC670u,0xC67Du,0xC67Eu,0xC67Bu,0xC67Au, 0xBB0090u,0x00u, 3,86 };
const nvgsp_gen* g_nvgen=0;

const nvgsp_gen nvgsp_gen_tu10x={ "Turing TU10x",0x162,0x166,0,".fwsignature_tu10x","tu102","booter_load-535.113.01.bin","booter_unload-535.113.01.bin","bootloader-535.113.01.bin","gsp-535.113.01.bin",0x100000ULL,0,0,24u<<20,0,0,0,0,0,0,0,&nvgsp_cls_tu10x,{0} };
const nvgsp_gen nvgsp_gen_tu11x={ "Turing TU11x",0x167,0x168,0,".fwsignature_tu11x","tu116","booter_load-535.113.01.bin","booter_unload-535.113.01.bin","bootloader-535.113.01.bin","gsp-535.113.01.bin",0x100000ULL,0,0,24u<<20,0,0,0,0,0,0,0,&nvgsp_cls_tu10x,{0} };
const nvgsp_gen nvgsp_gen_ga10x={ "Ampere GA10x",0x172,0x177,1,".fwsignature_ga10x","ga102","ga102-booter_load-535.113.01.bin","ga102-booter_unload-535.113.01.bin","ga102-bootloader-535.113.01.bin","ga102-gsp-535.113.01.bin",0x100000ULL,20ULL<<20,84ULL<<20,40u<<20,
    nvgsp_gen_ga102_reset,nvgsp_gen_ga102_load,nvgsp_gen_ga102_boot,nvgsp_gen_ga102_booter_sig,nvgsp_gen_ga102_fwsec_sig,nvgsp_gen_ga102_gsp_reset,nvgsp_gen_ga102_riscv_active,&nvgsp_cls_ga10x,{0} };

extern const nvgsp_gen nvgsp_gen_ad10x __attribute__((weak));
extern int g_nv_ada_on __attribute__((weak));
static const nvgsp_gen* const GENS[]={ &nvgsp_gen_tu10x,&nvgsp_gen_tu11x,&nvgsp_gen_ga10x,&nvgsp_gen_ad10x };

const nvgsp_gen* nvgsp_gen_for(u32 chip){
    if(chip==0x175||chip==0x195)return 0;
    for(u32 i=0;i<sizeof(GENS)/sizeof(GENS[0]);i++) if(GENS[i]&&chip>=GENS[i]->chip_lo&&chip<=GENS[i]->chip_hi)return GENS[i];
    return 0; }
int nvgsp_gen_hooked(void){ return g_nvgen&&g_nvgen->flcn_load; }
const nvgsp_cls* nvgsp_cls_cur(void){ return g_nvgen&&g_nvgen->cls?g_nvgen->cls:&nvgsp_cls_tu10x; }
int nvgsp_gen_pci_ok(u16 d){
    if((d>=0x1E00u&&d<=0x1FFFu)||(d>=0x2180u&&d<=0x21FFu))return 1;
    if(d>=NVGSP_PCI_AD10X_LO&&d<=NVGSP_PCI_AD10X_HI)return &g_nv_ada_on&&&nvgsp_gen_ad10x&&g_nv_ada_on;
    return g_nv_ampere_on&&d>=NVGSP_PCI_GA102_LO&&d<=NVGSP_PCI_GA107_HI; }

static int fls32(u32 v){ return v?32-__builtin_clz(v):0; }
int nvgsp_gen_fuse_idx(u32 reg,u32 sig,u32 cnt){
    int b=fls32(reg); if(b>31)return -1; reg=1u<<b; if(!(reg&sig))return -1;
    int idx=0; while(!(reg&sig&1u)){ idx+=(int)(sig&1u); reg>>=1; sig>>=1; }
    return (u32)idx<cnt?idx:-1; }

static int us_poll(u32 off,u32 mask,u32 val,u32 us){ u64 t0=hr_ns(); for(;;){ if((nvr32(off)&mask)==val)return 1; if(hr_ns()-t0>(u64)us*1000ULL)return 0; cpu_pause(); } }
static int ga_select(u32 f){
    u32 r=f+NVGSP_ADDR2_GA102+0x668;
    if(!(nvr32(r)&0x10u))return 1;
    nvw32(r,0); return nv_poll(r,1u,1u,10); }
static int ga_reset_eng(u32 f){
    (void)nvr32(f+0x0F4); us_poll(f+0x0F4,0x80000000u,0x80000000u,150);
    nvm32(f+0x3C0,1u,1u); hr_delay_us(10); nvm32(f+0x3C0,1u,0u);
    return nv_poll(f+0x0F4,0x1000u,0,20); }
u32 nvgsp_gen_top_reset(u32 type){
    u32 size=nvr32(0x0224FCu)>>20, n=0, t=0xFFFFFFFFu, rst=0xFFFFFFFFu; if(size>256)size=256;
    for(u32 i=0;i<size;i++){ u32 d=nvr32(0x022800u+i*4u); if(!d&&!n)continue;
        if(n==0){ t=(d>>24)&0x3Fu; rst=0xFFFFFFFFu; } else if(n==1)rst=d&0x1Fu;
        n++; if(d&0x80000000u)continue;
        n=0; if(t==type&&rst!=0xFFFFFFFFu)return 1u<<rst; }
    return 0; }
int nvgsp_gen_ga102_pmc(u32 mask,int on){
    if(!mask)return 1;
    nvm32(NVGSP_PMC_ENABLE,mask,on?mask:0); (void)nvr32(NVGSP_PMC_ENABLE); (void)nvr32(NVGSP_PMC_ENABLE);
    return ((nvr32(NVGSP_PMC_ENABLE)&mask)==(on?mask:0)); }
static void ga_prep(u32 f){ (void)nvr32(f+0x0F4); us_poll(f+0x0F4,0x80000000u,0x80000000u,150); }
int nvgsp_gen_ga102_reset(u32 f){
    u32 pmc=f==NVF_SEC2?nvgsp_gen_top_reset(NVGSP_TOP_SEC2):0;
    if(!ga_select(f))return 0;
    nvm32(f+0x048,3u,0); nvw32(f+0x014,0xFFFFFFFFu);
    if(pmc){ ga_prep(f); nvgsp_gen_ga102_pmc(pmc,0); }
    if(!ga_reset_eng(f))return 0;
    if(!ga_reset_eng(f)||!ga_select(f))return 0;
    if(pmc&&!nvgsp_gen_ga102_pmc(pmc,1))return 0;
    if(!nv_poll(f+0x0F4,0x1000u,0,20))return 0;
    nvw32(f+0x084,nvr32(0)); return 1; }
static int ga_dma(u32 f,u64 pa,u32 src,u32 dst,u32 len,int imem,int sec){
    if(!len||(len&0xFFu))return 0;
    u32 cmd=(6u<<8)|(imem?0x10u:0u)|(sec?4u:0u);
    nvw32(f+0x110,(u32)((pa+(imem?0:src))>>8)); nvw32(f+0x128,0);
    for(u32 o=0;o<len;o+=256){ nvw32(f+0x114,dst+o); nvw32(f+0x11C,(imem?src:0)+o); nvw32(f+0x118,cmd); if(!nv_poll(f+0x118,2u,2u,2000))return 0; }
    return 1; }
int nvgsp_gen_ga102_load(u32 f,const nvgsp_ldr* l){
    nvm32(f+0x624,0x80u,0x80u); nvw32(f+0x10C,0); nvm32(f+0x600,0x10007u,5u);
    if(!ga_dma(f,l->pa,l->imem_off,l->imem_base,l->imem_size,1,1))return 0;
    return ga_dma(f,l->pa,l->dmem_off,l->dmem_base,l->dmem_size,0,0); }
int nvgsp_gen_ga102_boot(u32 f,const nvgsp_ldr* l,u32 mb0,u32 mb1,u32* o0,u32* o1,u32 ms){
    u32 a=f+NVGSP_ADDR2_GA102;
    nvw32(a+0x210,l->dmem_sign); nvw32(a+0x19C,l->engine_id); nvw32(a+0x198,l->ucode_id); nvw32(a+0x180,1);
    return nvf_boot(f,l->bootvec,mb0,mb1,o0,o1,ms); }
int nvgsp_gen_ga102_booter_sig(u32 cnt,const nvgsp_ldr* l){
    u32 base=(l->engine_id&1u)?0x824140u:(l->engine_id&4u)?0x824100u:(l->engine_id&0x400u)?0x8241C0u:0;
    if(!base||!l->ucode_id||!cnt)return -1;
    u32 reg=nvr32(base+(l->ucode_id-1)*4);
    if(!reg)return (int)cnt-1;
    u32 r=(u32)fls32(reg); if(l->fuse_ver<r)return -1;
    u32 idx=l->fuse_ver-r; return idx<cnt?(int)idx:-1; }
int nvgsp_gen_ga102_fwsec_sig(u32 cnt,const nvgsp_ldr* l){
    if(!(l->engine_id&0x400u)||!l->ucode_id)return -1;
    return nvgsp_gen_fuse_idx(nvr32(0x8241C0u+(l->ucode_id-1)*4),l->fuse_ver,cnt); }
int nvgsp_gen_ga102_gsp_reset(void){
    if(!ga_reset_eng(NVF_GSP))return 0;
    nvm32(NVF_GSP+0x1668,0x111u,0x111u); return 1; }
int nvgsp_gen_ga102_riscv_active(void){ return (nvr32(NVF_GSP+NVGSP_ADDR2_GA102+0x388)&0x80u)!=0; }
