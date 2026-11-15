// SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause
/* Copyright(c) 2018-2019  Realtek Corporation */
#include "include/nvx/base.h"
#include "include/nvx/mem/pmm_vmm.h"
#include "include/nvx/core/klog.h"
#include "include/nvx/shell/terminal.h"
#include "include/nvx/drivers/net/wifi/rtw_efuse.h"
#include "include/nvx/drivers/net/wifi/rtw8821ce.h"
#include "include/nvx/drivers/net/wifi/rtw_chip.h"

extern const rtw_chip rtw8821c_chip;
extern const rtw_chip rtw8822b_chip __attribute__((weak));
extern const rtw_chip rtw8822c_chip __attribute__((weak));
extern const rtw_chip rtw8822c_chip_c82f __attribute__((weak));

static const rtw_chip* const rtw_chip_tab[]={ &rtw8821c_chip, &rtw8822b_chip, &rtw8822c_chip };
static const struct { u16 device; const rtw_chip* chip; } rtw_chip_alias[]={
    {0xB821,&rtw8821c_chip}, {0xC82A,&rtw8821c_chip}, {0xC82B,&rtw8821c_chip}, {0xC82F,&rtw8822c_chip_c82f},
};
#define RTW_NTAB ((int)(sizeof(rtw_chip_tab)/sizeof(rtw_chip_tab[0])))
#define RTW_NALIAS ((int)(sizeof(rtw_chip_alias)/sizeof(rtw_chip_alias[0])))

const rtw_chip* g_rtw_chip=&rtw8821c_chip;
rtw_efuse_info g_rtw_efuse;
u16 g_rtw_pwr_fail_off=0;
int g_rtw_coex_ok=1;

int rtw_chip_count(void){ int n=0; for(int i=0;i<RTW_NTAB;i++) if(rtw_chip_tab[i])n++; return n; }
const rtw_chip* rtw_chip_at(int i){ for(int k=0;k<RTW_NTAB;k++) if(rtw_chip_tab[k]&&i--==0)return rtw_chip_tab[k]; return 0; }
const rtw_chip* rtw_chip_find(u16 vendor,u16 device,int bus){
    for(int i=0;i<RTW_NTAB;i++){ const rtw_chip* c=rtw_chip_tab[i]; if(c&&c->vendor==vendor&&c->device==device&&c->bus==bus)return c; }
    for(int i=0;i<RTW_NALIAS;i++){ const rtw_chip* c=rtw_chip_alias[i].chip; if(c&&rtw_chip_alias[i].device==device&&c->vendor==vendor&&c->bus==bus)return c; }
    return 0;
}
const rtw_chip* rtw_chip_select(u16 vendor,u16 device){
    const rtw_chip* c=rtw_chip_find(vendor,device,RTW_BUS_PCIE);
    if(c)g_rtw_chip=c;
    return c;
}

int rtw_pwr_seq_run(u32 m,const rtw_pwr_cmd* s,int n){
    int fails=0;
    for(int i=0;i<n;i++){ u16 o=s[i].off;
        if(s[i].cmd==RTW_PWR_DELAY){ rtw_delay_us(1000); continue; }
        if(s[i].cmd==RTW_PWR_POLL){ int got=0; for(int t=0;t<100000;t++){ if((rtw_r8(m,o)&s[i].mask)==s[i].val){ got=1; break; } io_wait(); }
            if(!got){ fails++; g_rtw_pwr_fail_off=o; } continue; }
        u8 v=rtw_r8(m,o); v=(u8)((v&~s[i].mask)|(s[i].val&s[i].mask)); rtw_w8(m,o,v);
        for(volatile int d=0;d<500;d++)io_wait();
    }
    return fails;
}

u16 rtw_rsvd_boundary(void){
    const rtw_chip* c=g_rtw_chip; u32 pages=c->txff_size>>7;
    u32 rsvd=c->rsvd_drv_pages+(c->wcpu==RTW_WCPU_3081?24u+8u+8u+0u+4u+c->csi_pages:0u);
    return (u16)(pages-rsvd);
}
static u16 rtw_fw_txbuf_page(void){ const rtw_chip* c=g_rtw_chip; return (u16)((c->txff_size>>7)-c->csi_pages-4u); }

#define RTW_H2C_NUM 32u
#define RTW_H2C_SLOT 128u
static u8* g_h2c_ring=0; static u8* g_h2c_buf=0; static u32 g_h2c_num=0, g_h2c_idx=0; static u16 g_h2c_seq=0; static int g_h2c_info=0;
u32 g_rtw_h2c_sent=0, g_rtw_h2c_fail=0;
int g_rtw_iqk_en=0, g_rtw_iqk_last=0;   /* off by default until tested on the owner's RTL8821CE: wifi iqk on */
int g_rtw_iqk_dead=0;

void rtw_h2c_ring_off(void){ g_h2c_num=0; g_h2c_info=0; }
int rtw_h2c_ring_setup(u32 m){
    g_h2c_num=0; g_h2c_info=0; g_rtw_iqk_dead=0;
    if(g_rtw_chip->wcpu!=RTW_WCPU_3081||g_rtw_bus->kind!=RTW_BUS_PCIE)return -1;
    if(!g_h2c_ring)g_h2c_ring=(u8*)dma_alloc_pages(1);
    if(!g_h2c_buf)g_h2c_buf=(u8*)dma_alloc_pages(1);
    if(!g_h2c_ring||!g_h2c_buf)return -1;
    for(u32 i=0;i<RTW_H2C_NUM*16u;i++)g_h2c_ring[i]=0;
    rtw_w16(m,0x1328,(u16)(RTW_H2C_NUM&0x0FFFu));
    rtw_w32(m,0x1320,(u32)(u64)g_h2c_ring);
    rtw_w32(m,0x1330,rtw_r32(m,0x1330)|(1u<<16)|(1u<<8));
    g_h2c_idx=0; g_h2c_num=RTW_H2C_NUM;
    return 0;
}

int rtw_h2c_pkt_send(u32 m,const u8* pkt,int len){
    if(!g_h2c_num||!g_h2c_ring||!g_h2c_buf||!pkt||len<8||len>32){ g_rtw_h2c_fail++; return -1; }
    u32 raw=rtw_r32(m,0x132C); if(raw==0xFFFFFFFFu){ g_rtw_h2c_fail++; return -1; }
    g_h2c_idx=(raw&0x0FFFu)%g_h2c_num;
    u32 rp=((raw>>16)&0x0FFFu)%g_h2c_num, nx=(g_h2c_idx+1u)%g_h2c_num;
    if(nx==rp){ g_rtw_h2c_fail++; return -2; }
    u8* b=g_h2c_buf+g_h2c_idx*RTW_H2C_SLOT;
    for(u32 i=0;i<48u+32u;i++)b[i]=0;
    for(int i=0;i<len;i++)b[48+i]=pkt[i];
    b[48+6]=(u8)g_h2c_seq; b[48+7]=(u8)(g_h2c_seq>>8);
    *(volatile u32*)(b+0)=32u;
    *(volatile u32*)(b+4)=19u<<8;
    u8* bd=g_h2c_ring+g_h2c_idx*16u;
    *(volatile u16*)(bd+0)=48; *(volatile u16*)(bd+2)=(u16)((48u+32u-1u)/128u+1u); *(volatile u32*)(bd+4)=(u32)(u64)b;
    *(volatile u16*)(bd+8)=32; *(volatile u16*)(bd+10)=0; *(volatile u32*)(bd+12)=(u32)(u64)(b+48);
    cpu_wmb();
    g_h2c_idx=nx; g_h2c_seq++;
    rtw_w16(m,0x132C,(u16)(g_h2c_idx&0x0FFFu));
    g_rtw_h2c_sent++;
    for(int t=0;t<100;t++){ u32 r=rtw_r32(m,0x132C); if(r==0xFFFFFFFFu)return -1; if(((r>>16)&0x0FFFu)%g_h2c_num==g_h2c_idx)return 0; rtw_delay_us(50); }
    return 1;
}

int rtw_rf_paths(u32 m){ int n=g_rtw_chip->n_paths?g_rtw_chip->n_paths:1; if(n>1&&!(rtw_r32(m,0x00F0)&(1u<<27)))n=1; return n>2?2:n; }
static void rtw_h2c_info(u32 m){
    u8 p[32];
    for(int i=0;i<32;i++)p[i]=0;
    p[0]=0x01; p[1]=0xFF; p[2]=0x0D; p[4]=12; p[10]=(u8)(rtw_fw_txbuf_page()-rtw_rsvd_boundary());
    rtw_h2c_pkt_send(m,p,32);
    for(int i=0;i<32;i++)p[i]=0;
    u32 cut=(rtw_r32(m,0x00F0)>>12)&0xFu; int np=rtw_rf_paths(m); u8 ant=np>1?3:1;
    p[0]=0x01; p[1]=0xFF; p[2]=0x11; p[4]=16; p[8]=(u8)rtw_rfe_type(); p[9]=np>1?2:4; p[10]=(u8)cut; p[11]=(u8)(ant|(ant<<4));
    rtw_h2c_pkt_send(m,p,32);
}

void rtw_chip_calibrate(u32 m){
    const rtw_chip* c=g_rtw_chip;
    if(!m||!c->calibrate||!g_rtw_iqk_en||g_rtw_iqk_dead)return;
    if(!g_h2c_num)rtw_h2c_ring_setup(m);
    if(!g_h2c_num){ g_rtw_iqk_last=-3; rtw_jlog("  calibration (IQK) skipped: command ring not set up","",-1); return; }
    if((rtw_r16(m,0x0080)&(3u<<14))!=(3u<<14)){ g_rtw_iqk_last=-4; rtw_jlog("  calibration (IQK) skipped: firmware not running","",-1); return; }
    if(!g_h2c_info){ g_h2c_info=1; rtw_h2c_info(m); }
    g_rtw_iqk_last=0;
    c->calibrate(m);
    if(!g_rtw_iqk_last)g_rtw_iqk_last=1;
    if(g_rtw_iqk_last>0){ rtw_jlog("  calibration (IQK) done, fail mask","",(g_rtw_iqk_last>>8)&0xFF); return; }
    g_rtw_iqk_dead=1;
    rtw_jlog(g_rtw_iqk_last==-2?"  calibration (IQK) timed out - turned off until Wi-Fi restarts":"  calibration (IQK) command not taken - turned off until Wi-Fi restarts","",-1);
    klog(g_rtw_iqk_last==-2?"wifi: calibration (IQK) timed out - continuing without it":"wifi: calibration (IQK) command was not taken - continuing without it");
}

void rtw_chmark(const char* t){ extern int g_chquiet; if(g_chquiet)return; tcol(0x9AA0B0u); term_puts(t); term_putc('\n'); tcol(0xFFFFFFu); }
