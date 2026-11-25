// SPDX-License-Identifier: MIT
/*
 * Mailbox register offsets and PPSMC message numbers from Linux
 * drivers/gpu/drm/amd/pm/swsmu (smu_v12_0_ppsmc.h, smu_v13_0_1_ppsmc.h,
 * smu_v13_0_4_ppsmc.h, smu_v13_0_5_ppsmc.h, smu_v13_0_4_ppt.c, smu_v13_0_5_ppt.c,
 * smu13_driver_if_v13_0_5.h):
 * Copyright 2019-2022 Advanced Micro Devices, Inc.
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND.
 */
#include "drivers/video/amd/amd.h"

#define SMU_C2PMSG_66 0x0282
#define SMU_C2PMSG_82 0x0292
#define SMU_C2PMSG_90 0x029a
#define SMU_THM_TCON_CUR_TMP 0x00059800u

typedef struct { u32 ver; u8 seg, m_ver, m_gfx, m_fclk; const char* name; } amd_smu_tab_t;
static const amd_smu_tab_t amd_smu_tabs[] = {
    { AMD_IPV(12,0,0), 0, 0x02, 0x2A, 0x2B, "SMU 12.0 (Renoir)" },
    { AMD_IPV(12,0,1), 0, 0x02, 0x2A, 0x2B, "SMU 12.0.1 (Cezanne/Barcelo)" },
    { AMD_IPV(13,0,1), 0, 0x02, 0x17, 0x18, "SMU 13.0.1 (Yellow Carp)" },
    { AMD_IPV(13,0,3), 0, 0x02, 0x17, 0x18, "SMU 13.0.3 (Rembrandt)" },
    { AMD_IPV(13,0,8), 0, 0x02, 0x17, 0x18, "SMU 13.0.8" },
    { AMD_IPV(13,0,4), 1, 0x02, 0x17, 0x18, "SMU 13.0.4 (Phoenix)" },
    { AMD_IPV(13,0,11),1, 0x02, 0x17, 0x18, "SMU 13.0.11 (Phoenix2)" },
    { AMD_IPV(13,0,5), 0, 0x02, 0x0F, 0x00, "SMU 13.0.5 (Mendocino)" },
};

typedef struct { const amd_smu_tab_t* t; u32 msg, arg, resp; int ok, fails; u32 fw; } amd_smu_t;
static amd_smu_t g_smu;

static const amd_smu_tab_t* amd_smu_pick(void){
    u32 v=amd_ipver(AMD_IP_MP1);
    for(u32 i=0;i<sizeof(amd_smu_tabs)/sizeof(amd_smu_tabs[0]);i++) if(amd_smu_tabs[i].ver==v) return &amd_smu_tabs[i];
    if(v) return 0;
    switch(g_amd.family){
    case AMD_FAM_RV: return &amd_smu_tabs[1];
    case AMD_FAM_YC: return &amd_smu_tabs[3];
    case AMD_FAM_GC_10_3_7: return &amd_smu_tabs[7];
    default: return 0; }
}

static int amd_smu_setup(void){
    if(g_smu.ok) return 1;
    if(g_smu.fails>=3||!g_amd.found||!g_amd.apu||!g_amd.mmio) return 0;
    const amd_smu_tab_t* t=amd_smu_pick(); if(!t) return 0;
    if(t->ver==AMD_IPV(13,0,5)) return 0;
    u32 base=g_amd.ip[AMD_IP_MP1].base[t->seg];
    if(!base){ if(t->seg||amd_ipver(AMD_IP_MP1)) return 0; base=0x16000; }
    g_smu.t=t; g_smu.msg=base+SMU_C2PMSG_66; g_smu.arg=base+SMU_C2PMSG_82; g_smu.resp=base+SMU_C2PMSG_90;
    if((u64)(g_smu.resp+1)*4>g_amd.mmio_len) return 0;
    g_smu.ok=1; return 1;
}

static int amd_smu_send(u32 msg,u32 arg,u32* out){
    if(!amd_smu_setup()||!msg) return -1;
    u32 r=amd_rreg(g_smu.resp);
    if(r==0xFFFFFFFFu){ g_smu.fails++; return -2; }
    if(!r&&amd_wait(g_smu.resp,0xFFFFFFFFu,1,2000)<0&&!amd_rreg(g_smu.resp)){ g_smu.fails++; return -3; }
    amd_wreg(g_smu.resp,0); amd_wreg(g_smu.arg,arg); amd_wreg(g_smu.msg,msg);
    u64 t0=hr_ns();
    for(;;){ r=amd_rreg(g_smu.resp); if(r) break; if(hr_ns()-t0>20000000ULL){ g_smu.fails++; return -4; } __builtin_ia32_pause(); }
    if(r!=1) return -(int)(0x100|(r&0xFF));
    g_smu.fails=0;
    if(out) *out=amd_rreg(g_smu.arg);
    return 0;
}

const char* amd_smu_name(void){ return amd_smu_setup()?g_smu.t->name:0; }

u32 amd_smu_fw_version(void){
    if(g_smu.fw) return g_smu.fw;
    u32 v=0; if(g_smu.t||amd_smu_setup()) if(amd_smu_send(g_smu.t->m_ver,0,&v)==0) g_smu.fw=v;
    return g_smu.fw;
}

int amd_sclk_mhz(void){ u32 v=0; if(!amd_smu_setup()) return -1; return amd_smu_send(g_smu.t->m_gfx,0,&v)==0&&v<10000?(int)v:-1; }

int amd_fclk_mhz(void){ u32 v=0; if(!amd_smu_setup()||!g_smu.t->m_fclk) return -1; return amd_smu_send(g_smu.t->m_fclk,0,&v)==0&&v<10000?(int)v:-1; }

static u32 amd_mdo_reg(u32 off){ u32 b=g_amd.ip[AMD_IP_MP1].base[0]; if(!b)b=0x16000u; return (b+0xEAE000u+off)*4u; }
#define MDO_MSG amd_mdo_reg(0x142)
#define MDO_RESP amd_mdo_reg(0x261)
#define MDO_ARG amd_mdo_reg(0x262)
int amd_vram_scratch(u64* mc,volatile u8** cpu);
static struct { int fails; u32 last; u64 t; int mc; } g_mdo;
static int amd_mdo(void){ return amd_ipver(AMD_IP_MP1)==AMD_IPV(13,0,5)||(!amd_ipver(AMD_IP_MP1)&&g_amd.family==AMD_FAM_GC_10_3_7); }
static int amd_mdo_send(u32 msg,u32 arg,u32* out){
    u32 r=amd_smn_rd(MDO_RESP); if(r==0xFFFFFFFFu)return -2;
    u64 t0=hr_ns(); while(!r){ if(hr_ns()-t0>2000000ULL)return -3; __builtin_ia32_pause(); r=amd_smn_rd(MDO_RESP); }
    amd_smn_wr(MDO_RESP,0); amd_smn_wr(MDO_ARG,arg); amd_smn_wr(MDO_MSG,msg);
    t0=hr_ns(); for(;;){ r=amd_smn_rd(MDO_RESP); if(r&&r!=0xFFFFFFFFu)break; if(hr_ns()-t0>20000000ULL)return -4; __builtin_ia32_pause(); }
    if(r!=1)return -(int)(0x100|(r&0xFF));
    if(out)*out=amd_smn_rd(MDO_ARG); return 0; }
int amd_mdo_temp_mc(void){
    if(g_mdo.fails>=3||!amd_mdo())return -1000000;
    if(g_mdo.t&&hr_ns()-g_mdo.t<500000000ULL)return g_mdo.mc;
    u64 mc=0; volatile u8* c=0; if(amd_vram_scratch(&mc,&c)<0){ g_mdo.fails=3; return -1000000; }
    c[26]=0xFF; c[27]=0xFF;
    if(amd_mdo_send(11,(u32)(mc>>32),0)||amd_mdo_send(12,(u32)mc,0)||amd_mdo_send(13,7,0)){ g_mdo.fails++; return -1000000; }
    u32 v=(u32)c[26]|((u32)c[27]<<8); if(v==0xFFFFu||v>15000){ g_mdo.fails++; return -1000000; }
    g_mdo.fails=0; g_mdo.t=hr_ns(); g_mdo.mc=(int)v*10; return g_mdo.mc; }

int amd_temp_mc(void){
    if(!g_amd.found||!g_amd.apu) return -1000000;
    if(amd_mdo()){ int m=amd_mdo_temp_mc(); if(m>-1000000)return m; }
    u32 r=amd_smn_rd(SMU_THM_TCON_CUR_TMP);
    if(r==0xFFFFFFFFu||!r) return -1000000;
    int mc=(int)((r>>21)*125);
    if(r&(1u<<19)) mc-=49000;
    return mc;
}

int amd_temp_c(void){ int mc=amd_temp_mc(); return mc<=-1000000?-1:(mc+500)/1000; }

int amd_gpu_busy_pct(void){ return -1; }

u32 amd_smu_mbox_byte(void){ return amd_smu_setup()?g_smu.msg<<2:0; }
