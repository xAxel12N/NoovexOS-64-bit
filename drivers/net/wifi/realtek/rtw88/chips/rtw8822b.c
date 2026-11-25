// SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause
/* Copyright(c) 2018-2019  Realtek Corporation
 * RTL8822BE chip file for NoovexOS, ported from Linux rtw88 rtw8822b.c / rtw8822be.c (+ the parts of
 * mac.c, phy.c, coex.c and bf.c the chip relies on).
 */

#include "include/nvx/base.h"
#include "include/nvx/drivers/net/wifi/rtw_chip.h"
#include "include/nvx/drivers/net/wifi/rtw_efuse.h"
#include "drivers/net/wifi/realtek/rtw88/chips/rtw8822b_tables.h"

extern const unsigned char _binary_rtw8822b_fw_bin_start[], _binary_rtw8822b_fw_bin_end[];

#define BIT(n) (1u<<(n))
enum { PATH_A = 1, PATH_B = 2, PATH_AB = 3 };
enum { FEM_IFEM = 0, FEM_IFEM2G_EFEM5G = 1 };

static u8 g8b_cut, g8b_tx = PATH_AB, g8b_rx = PATH_AB, g8b_ch = 1, g8b_bw = RTW_BW_20, g8b_share_ant;
static u8 g8b_txpwr[2][42];
static u16 g8b_scbd;
u32 rtw8822b_iqk_rf08, rtw8822b_iqk_fail, rtw8822b_iqk_reload;

static u32 sh_of(u32 mask){ u32 s=0; while(s<31&&!((mask>>s)&1u))s++; return s; }
static void w32m(u32 m,u32 r,u32 mask,u32 v){ u32 s=sh_of(mask); rtw_w32(m,r,(rtw_r32(m,r)&~mask)|((v<<s)&mask)); }
static u32 r32m(u32 m,u32 r,u32 mask){ return (rtw_r32(m,r)&mask)>>sh_of(mask); }
static void w8m(u32 m,u32 r,u8 mask,u8 v){ u32 s=sh_of(mask); rtw_w8(m,r,(u8)((rtw_r8(m,r)&~mask)|((v<<s)&mask))); }
static void w32s(u32 m,u32 r,u32 mask,u32 v){ w32m(m,r,mask,v); w32m(m,r+0x200u,mask,v); }
static void w8set(u32 m,u32 r,u8 b){ rtw_w8(m,r,(u8)(rtw_r8(m,r)|b)); }
static void w8clr(u32 m,u32 r,u8 b){ rtw_w8(m,r,(u8)(rtw_r8(m,r)&~b)); }
static void w32set(u32 m,u32 r,u32 b){ rtw_w32(m,r,rtw_r32(m,r)|b); }
static void w32clr(u32 m,u32 r,u32 b){ rtw_w32(m,r,rtw_r32(m,r)&~b); }

static int is2g(u8 ch){ return ch<=14; }
static int b5_1(u8 ch){ return ch>=36&&ch<=48; }
static int b5_2(u8 ch){ return ch>=52&&ch<=64; }
static int b5_3(u8 ch){ return ch>=100&&ch<=144; }
static int b5_4(u8 ch){ return ch>=149&&ch<=177; }

static void hw_info(u32 m){
    u32 cfg=rtw_r32(m,0x00F0);
    g8b_cut=(u8)((cfg>>12)&0xFu);
    g8b_tx=g8b_rx=(cfg&BIT(27))?PATH_AB:PATH_A;
}

/* power sequences: card_enable_flow_8822b / card_disable_flow_8822b */
enum { PI_SDIO = 1, PI_USB = 2, PI_PCI = 4, PI_ALL = 0xF };
enum { PC_WRITE = 1, PC_POLL = 2, PC_DELAY = 3 };
typedef struct { u16 off; u8 cut, intf, sdio, cmd, mask, val; } pwr8b;
#define CUT_ALL 0xFF
#define CUT_C   0x08
static const pwr8b carddis_to_cardemu[] = {
    {0x0086,CUT_ALL,PI_SDIO,1,PC_WRITE,BIT(0),0},
    {0x0086,CUT_ALL,PI_SDIO,1,PC_POLL,BIT(1),BIT(1)},
    {0x004A,CUT_ALL,PI_USB,0,PC_WRITE,BIT(0),0},
    {0x0005,CUT_ALL,PI_ALL,0,PC_WRITE,BIT(3)|BIT(4)|BIT(7),0},
    {0x0300,CUT_ALL,PI_PCI,0,PC_WRITE,0xFF,0},
    {0x0301,CUT_ALL,PI_PCI,0,PC_WRITE,0xFF,0},
    {0,0,0,0,0,0,0} };
static const pwr8b cardemu_to_act[] = {
    {0x0012,CUT_ALL,PI_ALL,0,PC_WRITE,BIT(1),0},
    {0x0012,CUT_ALL,PI_ALL,0,PC_WRITE,BIT(0),BIT(0)},
    {0x0020,CUT_ALL,PI_USB|PI_SDIO,0,PC_WRITE,BIT(0),BIT(0)},
    {0x0001,CUT_ALL,PI_USB|PI_SDIO,0,PC_DELAY,1,1},
    {0x0000,CUT_ALL,PI_USB|PI_SDIO,0,PC_WRITE,BIT(5),0},
    {0x0005,CUT_ALL,PI_ALL,0,PC_WRITE,BIT(4)|BIT(3)|BIT(2),0},
    {0x0075,CUT_ALL,PI_PCI,0,PC_WRITE,BIT(0),BIT(0)},
    {0x0006,CUT_ALL,PI_ALL,0,PC_POLL,BIT(1),BIT(1)},
    {0x0075,CUT_ALL,PI_PCI,0,PC_WRITE,BIT(0),0},
    {0xFF1A,CUT_ALL,PI_USB,0,PC_WRITE,0xFF,0},
    {0x0006,CUT_ALL,PI_ALL,0,PC_WRITE,BIT(0),BIT(0)},
    {0x0005,CUT_ALL,PI_ALL,0,PC_WRITE,BIT(7),0},
    {0x0005,CUT_ALL,PI_ALL,0,PC_WRITE,BIT(4)|BIT(3),0},
    {0x10C3,CUT_ALL,PI_USB,0,PC_WRITE,BIT(0),BIT(0)},
    {0x0005,CUT_ALL,PI_ALL,0,PC_WRITE,BIT(0),BIT(0)},
    {0x0005,CUT_ALL,PI_ALL,0,PC_POLL,BIT(0),0},
    {0x0020,CUT_ALL,PI_ALL,0,PC_WRITE,BIT(3),BIT(3)},
    {0x10A8,CUT_C,PI_ALL,0,PC_WRITE,0xFF,0},
    {0x10A9,CUT_C,PI_ALL,0,PC_WRITE,0xFF,0xEF},
    {0x10AA,CUT_C,PI_ALL,0,PC_WRITE,0xFF,0x0C},
    {0x0068,CUT_C,PI_SDIO,0,PC_WRITE,BIT(4),BIT(4)},
    {0x0029,CUT_ALL,PI_ALL,0,PC_WRITE,0xFF,0xF9},
    {0x0024,CUT_ALL,PI_ALL,0,PC_WRITE,BIT(2),0},
    {0x0074,CUT_ALL,PI_PCI,0,PC_WRITE,BIT(5),BIT(5)},
    {0x00AF,CUT_ALL,PI_ALL,0,PC_WRITE,BIT(5),BIT(5)},
    {0,0,0,0,0,0,0} };
static const pwr8b act_to_cardemu[] = {
    {0x0003,CUT_ALL,PI_SDIO,0,PC_WRITE,BIT(2),0},
    {0x0093,CUT_ALL,PI_ALL,0,PC_WRITE,BIT(3),0},
    {0x001F,CUT_ALL,PI_ALL,0,PC_WRITE,0xFF,0},
    {0x00EF,CUT_ALL,PI_ALL,0,PC_WRITE,0xFF,0},
    {0xFF1A,CUT_ALL,PI_USB,0,PC_WRITE,0xFF,0x30},
    {0x0049,CUT_ALL,PI_ALL,0,PC_WRITE,BIT(1),0},
    {0x0006,CUT_ALL,PI_ALL,0,PC_WRITE,BIT(0),BIT(0)},
    {0x0002,CUT_ALL,PI_ALL,0,PC_WRITE,BIT(1),0},
    {0x10C3,CUT_ALL,PI_USB,0,PC_WRITE,BIT(0),0},
    {0x0005,CUT_ALL,PI_ALL,0,PC_WRITE,BIT(1),BIT(1)},
    {0x0005,CUT_ALL,PI_ALL,0,PC_POLL,BIT(1),0},
    {0x0020,CUT_ALL,PI_ALL,0,PC_WRITE,BIT(3),0},
    {0x0000,CUT_ALL,PI_USB|PI_SDIO,0,PC_WRITE,BIT(5),BIT(5)},
    {0,0,0,0,0,0,0} };
static const pwr8b cardemu_to_carddis[] = {
    {0x0005,CUT_ALL,PI_SDIO,0,PC_WRITE,BIT(7),BIT(7)},
    {0x0007,CUT_ALL,PI_USB|PI_SDIO,0,PC_WRITE,0xFF,0x20},
    {0x0067,CUT_ALL,PI_ALL,0,PC_WRITE,BIT(5),0},
    {0x0005,CUT_ALL,PI_PCI,0,PC_WRITE,BIT(2),BIT(2)},
    {0x004A,CUT_ALL,PI_USB,0,PC_WRITE,BIT(0),0},
    {0x0067,CUT_ALL,PI_SDIO,0,PC_WRITE,BIT(5),0},
    {0x0067,CUT_ALL,PI_SDIO,0,PC_WRITE,BIT(4),0},
    {0x004F,CUT_ALL,PI_SDIO,0,PC_WRITE,BIT(0),0},
    {0x0067,CUT_ALL,PI_SDIO,0,PC_WRITE,BIT(1),0},
    {0x0046,CUT_ALL,PI_SDIO,0,PC_WRITE,BIT(6),BIT(6)},
    {0x0067,CUT_ALL,PI_SDIO,0,PC_WRITE,BIT(2),0},
    {0x0046,CUT_ALL,PI_SDIO,0,PC_WRITE,BIT(7),BIT(7)},
    {0x0062,CUT_ALL,PI_SDIO,0,PC_WRITE,BIT(4),BIT(4)},
    {0x0081,CUT_ALL,PI_ALL,0,PC_WRITE,BIT(7)|BIT(6),0},
    {0x0005,CUT_ALL,PI_USB|PI_SDIO,0,PC_WRITE,BIT(3)|BIT(4),BIT(3)},
    {0x0086,CUT_ALL,PI_SDIO,1,PC_WRITE,BIT(0),BIT(0)},
    {0x0086,CUT_ALL,PI_SDIO,1,PC_POLL,BIT(1),0},
    {0x0090,CUT_ALL,PI_USB|PI_PCI,0,PC_WRITE,BIT(1),0},
    {0x0044,CUT_ALL,PI_SDIO,1,PC_WRITE,0xFF,0},
    {0x0040,CUT_ALL,PI_SDIO,1,PC_WRITE,0xFF,0x90},
    {0x0041,CUT_ALL,PI_SDIO,1,PC_WRITE,0xFF,0x00},
    {0x0042,CUT_ALL,PI_SDIO,1,PC_WRITE,0xFF,0x04},
    {0,0,0,0,0,0,0} };

static int pwr_poll(u32 m,u32 off,u8 mask,u8 val){
    for(int i=0;i<20000;i++){ if((rtw_r8(m,off)&mask)==(val&mask))return 1; rtw_delay_us(50); }
    return 0;
}
static int pwr_run(u32 m,const pwr8b* s){
    u8 intf=PI_PCI, cut=(u8)(1u<<(g8b_cut+1));
    for(;s->cmd;s++){
        if(!(s->intf&intf)||!(s->cut&cut))continue;
        u32 off=s->off|(s->sdio?0x10250000u:0);
        if(s->cmd==PC_WRITE){ rtw_w8(m,off,(u8)((rtw_r8(m,off)&~s->mask)|(s->val&s->mask))); continue; }
        if(s->cmd==PC_DELAY){ rtw_delay_us(s->val?(u32)s->off*1000u:s->off); continue; }
        if(pwr_poll(m,off,s->mask,s->val))continue;
        u8 v=rtw_r8(m,0x0004); rtw_w8(m,0x0004,(u8)(v|BIT(3))); rtw_w8(m,0x0004,(u8)(v&~BIT(3)));
        if(!pwr_poll(m,off,s->mask,s->val))return -1;
    }
    return 0;
}
static int pwr_switch(u32 m,int on){
    u8 rpwm=rtw_r8(m,0x03D9);
    if(rtw_r16(m,0x0080)==0xC078){ rpwm=(u8)((rpwm^0x80u)&0x80u); rtw_w8(m,0x03D9,rpwm); }
    int cur=rtw_r8(m,0x0100)!=0xEA;
    if(on==cur)return 1;
    if(on)return pwr_run(m,carddis_to_cardemu)||pwr_run(m,cardemu_to_act)?-1:0;
    return pwr_run(m,act_to_cardemu)||pwr_run(m,cardemu_to_carddis)?-1:0;
}
static int rtw8822b_power_on(u32 m){
    hw_info(m);
    int r=pwr_switch(m,1);
    if(r==1){ pwr_switch(m,0); r=pwr_switch(m,1); }
    return r;
}
static void rtw8822b_power_off(u32 m){ pwr_switch(m,0); }

/* efuse: struct rtw8822b_efuse, PCIe MAC at 0xD0 */
int rtw_mdio_write(u32 m,u8 addr,u16 data,int g1);
void ui_pump(void);
static const struct { u8 addr; u16 g1, g2; } rtw8822b_pcie_cut_c[]={
    {0x01,0xA841,0xA841}, {0x02,0x60C6,0x60C6}, {0x08,0x3596,0x3597}, {0x09,0x321C,0x321C}, {0x0A,0x9623,0x9623},
    {0x20,0x94FF,0x94FF}, {0x21,0xFFCF,0xFFCF}, {0x26,0xC006,0xC006}, {0x29,0xFF0E,0xFF0E}, {0x2A,0x1840,0x3040},
};
static int rtw8822b_pcie_phy(u32 m){
    if(((rtw_r32(m,0x00F0)>>12)&0xFu)!=2u)return 1;
    int r=0, n=(int)(sizeof(rtw8822b_pcie_cut_c)/sizeof(rtw8822b_pcie_cut_c[0]));
    for(int i=0;i<n;i++) if(rtw_mdio_write(m,rtw8822b_pcie_cut_c[i].addr,rtw8822b_pcie_cut_c[i].g1,1))r=-1;
    for(int i=0;i<n;i++) if(rtw_mdio_write(m,rtw8822b_pcie_cut_c[i].addr,rtw8822b_pcie_cut_c[i].g2,0))r=-1;
    return r;
}
static int rtw8822b_efuse_parse(const u8* map,int len,rtw_efuse_info* out){
    if(!map||!out||len<0xD6)return -1;
    for(int i=0;i<6;i++)out->mac[i]=map[0xD0+i];
    out->rfe_option=map[0xCA];
    out->crystal_cap=map[0xB9]==0xFF?0:map[0xB9];
    out->thermal_meter=map[0xBA];
    out->country_code[0]=map[0xCB]; out->country_code[1]=map[0xCC];
    for(int p=0;p<2;p++){
        const u8* t=map+0x10+42*p;
        for(int i=0;i<42;i++)g8b_txpwr[p][i]=t[i];
        for(int i=0;i<14;i++){ out->tx_pwr_idx_2g[p][i]=t[i]; out->tx_pwr_idx_5g[p][i]=t[18+i]; }
    }
    g8b_share_ant=map[0xC3]&1u;
    u8 r=out->rfe_option;
    return (r==2||r==3||r==5)?0:-2;
}

static int rtw8822b_mac_init(u32 m){
    w8clr(m,0x04BC,BIT(6));
    rtw_w8(m,0x0455,0x70);
    w8set(m,0x045E,BIT(2));
    rtw_w32(m,0x04C8,0xFFu|(0x08u<<8)|(0x20u<<16)|(0x20u<<24));
    rtw_w16(m,0x04CE,(u16)(0x01u|(0x08u<<8)));
    rtw_w8(m,0x1448,0x06); rtw_w8(m,0x144A,0x06);
    rtw_w8(m,0x144C,0x06); rtw_w8(m,0x144E,0x06);
    w8clr(m,0x05B4,BIT(4)|BIT(5)|BIT(6));
    rtw_w16(m,0x0522,0);
    rtw_w8(m,0x051B,0x09);
    rtw_w8(m,0x0512,0x19);
    rtw_w32(m,0x0514,0x0Au|(0x0Eu<<8)|(0x10u<<16)|(0x10u<<24));
    rtw_w16(m,0x0502,0x186);
    rtw_w16(m,0x0506,0x3BC);
    rtw_w32(m,0x0544,0x05u|(0x1Bu<<16));
    rtw_w16(m,0x055E,(u16)(0x30u|(0x30u<<8)));
    w8set(m,0x0550,BIT(3));
    rtw_w32(m,0x0540,0x04u|(0x064u<<8));
    rtw_w8(m,0x0558,0x04);
    rtw_w8(m,0x0559,0x02);
    w8clr(m,0x0521,BIT(12)>>8);
    rtw_w16(m,0x06A0,0xFFFF);
    rtw_w16(m,0x06A2,0x0FFF);
    rtw_w16(m,0x06A4,0xFFFF);
    rtw_w32(m,0x0608,0xE400220Eu);
    rtw_w8(m,0x060C,(u8)(12288u>>9));
    rtw_w8(m,0x0606,0x30);
    rtw_w8(m,0x0605,0x30);
    rtw_w32(m,0x07D8,0xB0810041u);
    rtw_w8(m,0x07D4,0x98);
    w8set(m,0x0718,BIT(6));
    return 0;
}

/* RFE / CCA */
typedef struct { u32 r82c[4], r830[4], r838[4]; } cca8b;
static const cca8b cca_ifem = {{0x75C97010,0x75C97010,0x75C97010,0x75C97010},{0x79a0eaaa,0x79A0EAAC,0x79a0eaaa,0x79a0eaaa},{0x87765541,0x87746341,0x87765541,0x87746341}};
static const cca8b cca_efem = {{0x75B86010,0x75B76010,0x75B86010,0x75B76010},{0x79A0EAA8,0x79A0EAAC,0x79A0EAA8,0x79a0eaaa},{0x87766451,0x87766431,0x87766451,0x87766431}};
static const cca8b cca_ifem_ext = {{0x75da8010,0x75da8010,0x75da8010,0x75da8010},{0x79a0eaaa,0x97A0EAAC,0x79a0eaaa,0x79a0eaaa},{0x87765541,0x86666341,0x87765561,0x86666361}};

static void rfe_efem(u32 m,u8 ch){
    if(is2g(ch)){ w32s(m,0xCB0,0xFFFFFF,0x705770); w32s(m,0xCB4,0xFF00,0x57); w32s(m,0xCB8,BIT(4),0); }
    else { w32s(m,0xCB0,0xFFFFFF,0x177517); w32s(m,0xCB4,0xFF00,0x75); w32s(m,0xCB8,BIT(5),0); }
    w32s(m,0xCBC,BIT(11)|BIT(10)|0x3F,0);
    if(g8b_rx==PATH_AB||g8b_tx==PATH_AB)w32s(m,0xCA0,0xFFFF,0xA501);
    else if(g8b_rx==g8b_tx)w32s(m,0xCA0,0xFFFF,0xA500);
    else w32s(m,0xCA0,0xFFFF,0xA005);
}
static void rfe_ifem(u32 m,u8 ch){
    if(is2g(ch)){ w32s(m,0xCB0,0xFFFFFF,0x745774); w32s(m,0xCB4,0xFF00,0x57); }
    else { w32s(m,0xCB0,0xFFFFFF,0x477547); w32s(m,0xCB4,0xFF00,0x75); }
    w32s(m,0xCBC,BIT(11)|BIT(10)|0x3F,0);
    if(is2g(ch)){
        if(g8b_rx==PATH_AB||g8b_tx==PATH_AB)w32s(m,0xCA0,0xFFFF,0xA501);
        else if(g8b_rx==g8b_tx)w32s(m,0xCA0,0xFFFF,0xA500);
        else w32s(m,0xCA0,0xFFFF,0xA005);
    } else w32s(m,0xCA0,0xFFFF,0xA5A5);
}
static int rfe_ok(void){ u8 r=g_rtw_efuse.rfe_option; return r==2||r==3||r==5; }
static void rfe_switch(u32 m,u8 ch){ if(!rfe_ok())return; if(g_rtw_efuse.rfe_option==2)rfe_efem(m,ch); else rfe_ifem(m,ch); }

static void set_cca(u32 m,u8 ch,u8 bw){
    if(!rfe_ok())return;
    int efem=g_rtw_efuse.rfe_option==2, fem=efem?FEM_IFEM2G_EFEM5G:FEM_IFEM, ext=!efem;
    const cca8b* c; int col, one=(g8b_rx==PATH_A||g8b_rx==PATH_B);
    if(is2g(ch)){ c=efem?&cca_ifem:&cca_ifem_ext; col=one?0:1; }
    else { c=efem?&cca_efem:&cca_ifem_ext; col=one?2:3; }
    u32 r82c=c->r82c[col], r830=c->r830[col], r838=c->r838[col];
    int is_ifem=0, is_efem=0;
    if(fem==FEM_IFEM)is_ifem=1; else if(is2g(ch))is_ifem=1; else is_efem=1;
    if(is_ifem){
        if((g8b_cut==1&&(col==1||col==3)&&bw==RTW_BW_40)||(!ext&&col==3&&bw==RTW_BW_40)||(g_rtw_efuse.rfe_option==5&&col==3))
            r830=0x79a0ea28;
    }
    rtw_w32(m,0x082C,r82c);
    rtw_w32(m,0x0830,r830);
    rtw_w32(m,0x0838,r838);
    if(is_efem&&g8b_cut!=1)rtw_w32(m,0x083C,0x9194b2b9);
    if(bw==RTW_BW_20&&(b5_2(ch)||b5_3(ch)))w32m(m,0x0838,0xF0,0x4);
}

static void toggle_igi(u32 m){
    u32 igi=r32m(m,0x0C50,0x7F);
    w32m(m,0x0C50,0x7F,igi-2); w32m(m,0x0C50,0x7F,igi);
    w32m(m,0x0E50,0x7F,igi-2); w32m(m,0x0E50,0x7F,igi);
    w32m(m,0x0808,0xFF,0);
    w32m(m,0x0808,0xFF,(u32)g8b_rx|((u32)g8b_rx<<4));
}

static void config_trx_mode(u32 m,u8 tx,u8 rx){
    if(!rfe_ok())return;
    w32m(m,0x0C08,0xFFFF,((tx|rx)&PATH_A)?0x3231:0x1111);
    w32m(m,0x0E08,0xFFFF,((tx|rx)&PATH_B)?0x3231:0x1111);
    w32m(m,0x093C,BIT(19)|BIT(18),0x3);
    w32m(m,0x080C,BIT(29)|BIT(28),0x1);
    w32m(m,0x080C,BIT(30),0x1);
    if(tx&PATH_A){ w32m(m,0x093C,0xFFF00000,0x001); w32m(m,0x0A04,0xF0000000,0x8); }
    else if(tx&PATH_B){ w32m(m,0x093C,0xFFF00000,0x002); w32m(m,0x0A04,0xF0000000,0x4); }
    w32m(m,0x0940,0xFFF0,(tx==PATH_A||tx==PATH_B)?0x01:0x43);
    w32m(m,0x080C,0xFF,((u32)tx<<4)|tx);
    w32m(m,0x0A2C,BIT(22),0);
    w32m(m,0x0A2C,BIT(18),0);
    if(rx&PATH_A)w32m(m,0x0A04,0x0F000000,0x0);
    else if(rx&PATH_B)w32m(m,0x0A04,0x0F000000,0x5);
    w32m(m,0x0808,0xFF,((u32)rx<<4)|rx);
    u32 one=(rx==PATH_A||rx==PATH_B)?0:1;
    w32m(m,0x1904,BIT(16),one); w32m(m,0x0800,BIT(28),one); w32m(m,0x0850,BIT(23),one);
    int c;
    for(c=100;c>0;c--){
        rtw_rf_write(m,0,0xEF,0xFFFFF,0x80000);
        rtw_rf_write(m,0,0x33,0xFFFFF,0x00001);
        rtw_delay_us(2);
        if(rtw_rf_read(m,0,0x33,0xFFFFF)==0x00001)break;
    }
    if(c<=0)return;
    rtw_rf_write(m,0,0xEF,0xFFFFF,0x80000);
    rtw_rf_write(m,0,0x33,0xFFFFF,0x00001);
    rtw_rf_write(m,0,0x3E,0xFFFFF,0x00034);
    rtw_rf_write(m,0,0x3F,0xFFFFF,0x4080C);
    rtw_rf_write(m,0,0xEF,0xFFFFF,0x00000);
    rtw_rf_write(m,0,0xEF,0xFFFFF,0x00000);
    toggle_igi(m);
    set_cca(m,1,RTW_BW_20);
    rfe_switch(m,g8b_ch);
}

/* BB/RF domain on before the tables (first part of rtw8822b_phy_set_param) */
static void rtw8822b_phy_init(u32 m){
    hw_info(m);
    w8set(m,0x0002,BIT(0)|BIT(1));
    w8set(m,0x001F,BIT(0)|BIT(1)|BIT(2));
    w32set(m,0x00EC,BIT(24)|BIT(25)|BIT(26));
    w32clr(m,0x0808,BIT(28)|BIT(29));
}

/* rest of rtw8822b_phy_set_param after rtw_phy_load_tables */
static void rtw8822b_phy_post(u32 m){
    u32 cap=g_rtw_efuse.crystal_cap&0x3Fu;
    w32m(m,0x0024,0x7E000000,cap);
    w32m(m,0x0028,0x7E,cap);
    w32set(m,0x0808,BIT(28)|BIT(29));
    config_trx_mode(m,g8b_tx,g8b_rx);
    w32m(m,0x08A4,0xFF,0x7F); w32m(m,0x08A4,0xFF00,0x7F);
    w32clr(m,0x0520,BIT(15));
    w32set(m,0x0524,BIT(11));
    w32m(m,0x0944,BIT(29)|BIT(28),1);
    w32m(m,0x08A0,0x3,0);
    w32set(m,0x08DC,BIT(5));
    w32m(m,0x0064,BIT(29)|BIT(28),0x3);
    w32m(m,0x004C,BIT(26)|BIT(25),0x0);
    w32m(m,0x0040,BIT(2),0x1);
    w32m(m,0x1990,0x3F,0x30);
    w32m(m,0x1990,BIT(11)|BIT(10),0x3);
    w32m(m,0x0974,0x3F,0x3F);
    w32m(m,0x0974,BIT(11)|BIT(10),0x3);
    u32 t=rtw_r32(m,0x14C0);
    t|=BIT(16); t&=~(0xFu<<12); t|=(0xAu<<12)&(0xFu<<12); t&=~BIT(7); t&=~0x3Fu;
    rtw_w32(m,0x14C0,t);
    rtw_w8(m,0x167C,(u8)((3u<<4)|BIT(6)));
    rtw_w16(m,0x1680,0);
    w32set(m,0x042C,BIT(30));
    rtw_w8(m,0x045F,0x10);
    w32m(m,0x06DC,0x3Fu<<24,0x04);
    rtw_w32(m,0x1C94,0xAFFFAFFF);
}

/* channel */
static const u8 low_band[15]={0x7,0x6,0x6,0x5,0x0,0x0,0x7,0xff,0x6,0x5,0x0,0x0,0x7,0x6,0x6};
static const u8 middle_band[23]={0x6,0x5,0x0,0x0,0x7,0x6,0x6,0xff,0x0,0x0,0x7,0x6,0x6,0x5,0x0,0xff,0x7,0x6,0x6,0x5,0x0,0x0,0x7};
static const u8 high_band[15]={0x5,0x5,0x0,0x7,0x7,0x6,0x5,0xff,0x0,0x7,0x7,0x6,0x5,0x5,0x0};

u32 rtw8822b_rf18(u32 rf18,u8 ch,u8 bw){
    rf18&=~((BIT(16)|BIT(9)|BIT(8))|0xFFu|(BIT(18)|BIT(17))|(BIT(11)|BIT(10)));
    rf18|=is2g(ch)?0:(BIT(16)|BIT(8));
    rf18|=ch;
    if(ch>144)rf18|=BIT(18); else if(ch>=80)rf18|=BIT(17);
    rf18|=bw==RTW_BW_40?BIT(11):bw==RTW_BW_80?BIT(10):(BIT(11)|BIT(10));
    return rf18;
}
static void set_channel_rf(u32 m,u8 ch,u8 bw){
    u32 rf18=rtw8822b_rf18(rtw_rf_read(m,0,0x18,0xFFFFF),ch,bw), be;
    if(is2g(ch))be=0;
    else if(b5_1(ch)||b5_2(ch))be=low_band[(ch-36)>>1];
    else if(b5_3(ch))be=middle_band[(ch-100)>>1];
    else if(b5_4(ch))be=high_band[(ch-149)>>1];
    else return;
    rtw_rf_write(m,0,0xBE,BIT(17)|BIT(16)|BIT(15),be);
    rtw_rf_write(m,0,0xDF,BIT(18),ch==144?1:0);
    rtw_rf_write(m,0,0x18,0xFFFFF,rf18);
    if(g8b_tx==PATH_AB||g8b_rx==PATH_AB)rtw_rf_write(m,1,0x18,0xFFFFF,rf18);
    rtw_rf_write(m,0,0xB8,BIT(19),0);
    rtw_rf_write(m,0,0xB8,BIT(19),1);
}
static void set_channel_rxdfir(u32 m,u8 bw){
    if(bw==RTW_BW_40){ w32m(m,0x0948,BIT(29)|BIT(28),0x1); w32m(m,0x094C,BIT(29)|BIT(28),0x0); w32s(m,0xC20,BIT(31),0x0); }
    else if(bw==RTW_BW_80){ w32m(m,0x0948,BIT(29)|BIT(28),0x2); w32m(m,0x094C,BIT(29)|BIT(28),0x1); w32s(m,0xC20,BIT(31),0x0); }
    else { w32m(m,0x0948,BIT(29)|BIT(28),0x2); w32m(m,0x094C,BIT(29)|BIT(28),0x2); w32s(m,0xC20,BIT(31),0x1); }
}
static void set_channel_bb(u32 m,u8 ch,u8 bw,u8 pidx){
    if(is2g(ch)){
        w32m(m,0x0808,BIT(28),0x1);
        w32m(m,0x0454,BIT(7),0x0);
        w32m(m,0x0A80,BIT(18),0x0);
        w32m(m,0x0814,0x0000FC00,15);
        w32m(m,0x0958,0x1F,0x0);
        w32m(m,0x0860,0x1FFE0000,0x96A);
        if(ch==14){ rtw_w32(m,0x0A24,0x00006577); w32m(m,0x0A28,0xFFFF,0x0000); }
        else { rtw_w32(m,0x0A24,0x384F6577); w32m(m,0x0A28,0xFFFF,0x1525); }
        w32m(m,0x0CBC,0x300,0x2);
    } else if(b5_1(ch)||b5_2(ch)||b5_3(ch)||b5_4(ch)){
        w32m(m,0x0A80,BIT(18),0x1);
        w32m(m,0x0454,BIT(7),0x1);
        w32m(m,0x0808,BIT(28),0x0);
        w32m(m,0x0814,0x0000FC00,34);
        if(b5_1(ch)||b5_2(ch))w32m(m,0x0958,0x1F,0x1);
        else if(b5_3(ch))w32m(m,0x0958,0x1F,0x2);
        else w32m(m,0x0958,0x1F,0x3);
        if(b5_1(ch))w32m(m,0x0860,0x1FFE0000,0x494);
        else if(b5_2(ch))w32m(m,0x0860,0x1FFE0000,0x453);
        else if(ch>=100&&ch<=116)w32m(m,0x0860,0x1FFE0000,0x452);
        else if(ch>=118&&ch<=177)w32m(m,0x0860,0x1FFE0000,0x412);
        w32m(m,0x0CBC,0x300,0x1);
    }
    u32 v=rtw_r32(m,0x08AC);
    if(bw==RTW_BW_40){
        if(pidx==1)w32set(m,0x0A00,BIT(4)); else w32clr(m,0x0A00,BIT(4));
        v=(v&0xFF3FF300u)|(((u32)(pidx&0xF))<<2)|RTW_BW_40; rtw_w32(m,0x08AC,v);
        w32m(m,0x08C4,BIT(30),0x1);
    } else if(bw==RTW_BW_80){
        v=(v&0xFCEFCF00u)|(((u32)(pidx&0xF))<<2)|RTW_BW_80; rtw_w32(m,0x08AC,v);
        w32m(m,0x08C4,BIT(30),0x1);
        u8 r=g_rtw_efuse.rfe_option;
        if(r==2||r==3){ w32m(m,0x0840,0x0000F000,0x6); w32m(m,0x08C8,BIT(10),0x1); }
    } else {
        v=(v&0xFFCFFC00u)|RTW_BW_20; rtw_w32(m,0x08AC,v);
        w32m(m,0x08C4,BIT(30),0x1);
    }
}
static void set_channel_mac(u32 m,u8 ch,u8 bw,u8 pidx){
    u8 sc40=0;
    if(bw==RTW_BW_80)sc40=(pidx==1||pidx==3)?9:10;
    rtw_w8(m,0x0483,(u8)((pidx&0xF)|((sc40&0xF)<<4)));
    u32 v=rtw_r32(m,0x0668)&~(BIT(7)|BIT(8));
    if(bw==RTW_BW_80)v|=BIT(8); else if(bw==RTW_BW_40)v|=BIT(7);
    rtw_w32(m,0x0668,v);
    rtw_w32(m,0x0024,rtw_r32(m,0x0024)&~(BIT(20)|BIT(21)));
    rtw_w8(m,0x055C,80);
    rtw_w8(m,0x0638,80);
    u8 c=(u8)(rtw_r8(m,0x0454)&~BIT(7));
    if(!is2g(ch))c|=BIT(7);
    rtw_w8(m,0x0454,c);
}

/* ch is the primary (control) channel; returns center and the rtw88 primary index (RTW_SC_*) */
void rtw8822b_chan_params(u8 ch,u8 bw,u8* center,u8* pidx){
    *center=ch; *pidx=0;
    if(bw==RTW_BW_40){
        int up;
        if(is2g(ch))up=ch>7; else if(ch>=149)up=((ch-149)/4)&1; else up=((ch-36)/4)&1;
        *center=(u8)(up?ch-2:ch+2); *pidx=up?1:2;
    } else if(bw==RTW_BW_80){
        int base=ch>=149?149:36, pos=((ch-base)/4)&3;
        *center=(u8)(ch-pos*4+6);
        *pidx=pos==0?4:pos==1?2:pos==2?1:3;
    }
}

static void rtw8822b_set_tx_power(u32 m,u8 ch);
static void rtw8822b_set_channel(u32 m,u8 ch,u8 bw){
    if(!rfe_ok())return;
    if(is2g(ch)&&bw>RTW_BW_40)bw=RTW_BW_40;
    u8 cc,pidx; rtw8822b_chan_params(ch,bw,&cc,&pidx);
    g8b_ch=cc; g8b_bw=bw;
    set_channel_bb(m,cc,bw,pidx);
    set_channel_mac(m,cc,bw,pidx);
    set_channel_rf(m,cc,bw);
    set_channel_rxdfir(m,bw);
    toggle_igi(m);
    set_cca(m,cc,bw);
    rfe_switch(m,cc);
    rtw8822b_set_tx_power(m,cc);
}
static void rtw8822b_rfe_setup(u32 m,int band5){ rfe_switch(m,band5?36:1); }

/* TX power: rtw88 base index per rate (efuse), by-rate and limit tables are not applied */
static u8 chan_group(u8 ch,u8 rate){
    static const u8 lim[]={2,5,8,11,13}, lim5[]={42,50,58,64,106,114,122,130,138,144,155,161,171};
    if(ch==14)return rate<=3?5:4;
    if(ch<14){ u8 g=0; while(g<4&&ch>lim[g])g++; return g; }
    u8 g=0; while(g<13&&ch>lim5[g])g++; return g;
}
static int lo4(u8 v){ return (v&8)?(int)(v&0xF)-16:(int)(v&0xF); }
static int hi4(u8 v){ return lo4((u8)(v>>4)); }
u8 rtw8822b_pwr_index(int path,u8 ch,u8 bw,u8 rate){
    const u8* t=g8b_txpwr[path&1];
    u8 g=chan_group(ch,rate), p;
    int mcs=(rate>=12&&rate<=43)||(rate>=44&&rate<=83);
    int ss2=(rate>=20&&rate<=43)||rate>=54;
    if(is2g(ch)){
        p=rate<=3?t[g]:t[6+g];
        if(rate>=4&&rate<=11)p=(u8)(p+lo4(t[11]));
        if(mcs){
            if(bw==RTW_BW_40){ if(ss2)p=(u8)(p+hi4(t[12])); }
            else { p=(u8)(p+hi4(t[11])); if(ss2)p=(u8)(p+lo4(t[12])); }
        }
    } else {
        const u8* f=t+18;
        p=f[g];
        if(!mcs)p=(u8)(p+lo4(f[14]));
        else if(bw==RTW_BW_80){ p=(u8)((f[g]+f[g+1])/2); p=(u8)(p+hi4(f[20])); if(ss2)p=(u8)(p+hi4(f[21])); }
        else if(bw==RTW_BW_40){ if(ss2)p=(u8)(p+hi4(f[15])); }
        else { p=(u8)(p+hi4(f[14])); if(ss2)p=(u8)(p+lo4(f[15])); }
    }
    return p>0x3F?0x3F:p;
}
static void rtw8822b_set_tx_power(u32 m,u8 ch){
    static const u8 sec[][2]={{0,3},{4,11},{12,19},{20,27},{44,53},{54,63}};
    int np=(g8b_tx==PATH_AB||g8b_rx==PATH_AB)?2:1;
    for(int p=0;p<np;p++){
        u32 base=p?0x1D80u:0x1D00u, w=0;
        for(int s=is2g(ch)?0:1;s<6;s++)
            for(int r=sec[s][0];r<=sec[s][1];r++){
                w|=(u32)rtw8822b_pwr_index(p,ch,g8b_bw,(u8)r)<<((r&3)*8);
                if((r&3)==3){ rtw_w32(m,base+(u32)(r&0xFC),w); w=0; }
            }
    }
}

/* IQK in firmware (rtw8822b_do_iqk via H2C_PKT_IQK) */
static void rtw8822b_calibrate(u32 m){
    u8 pkt[32];
    for(int i=0;i<32;i++)pkt[i]=0;
    pkt[0]=0x01; pkt[1]=0xFF; pkt[2]=0x0E; pkt[3]=0x00;
    pkt[4]=8+1; pkt[5]=0;
    g_rtw_iqk_last=-1;
    if(rtw_h2c_pkt_send(m,pkt,32)!=0)return;
    u32 v=0;
    for(int c=0;c<300;c++){ v=rtw_rf_read(m,0,0x08,0xFFFFF); if(v==0xABCDE)break; rtw_delay_us(20000); if((c&3)==3)ui_pump(); }
    rtw8822b_iqk_rf08=v;
    rtw_rf_write(m,0,0x08,0xFFFFF,0);
    rtw8822b_iqk_reload=r32m(m,0x1BF0,BIT(16));
    rtw8822b_iqk_fail=r32m(m,0x1BF0,0xFF);
    g_rtw_iqk_last=v==0xABCDE?(int)(1u|((rtw8822b_iqk_fail&0xFFu)<<8)):-2;
}

/* wifi-only coex: __rtw_coex_init_hw_config(wifi_only) with the 8822B coex ops */
static int lte_wait(u32 m){ for(int i=0;i<1000;i++){ if(rtw_r32(m,0x1700)&BIT(29))return 1; rtw_delay_us(10); } return 0; }
static u32 lte_read(u32 m,u16 a){ if(!lte_wait(m))return 0; rtw_w32(m,0x1700,0x800F0000u|a); return rtw_r32(m,0x1708); }
static void lte_write(u32 m,u16 a,u32 v){ if(!lte_wait(m))return; rtw_w32(m,0x1704,v); rtw_w32(m,0x1700,0xC00F0000u|a); }
static void lte_mask(u32 m,u16 a,u32 mask,u32 v){ u32 t=lte_read(m,a); lte_write(m,a,(t&~mask)|((v<<sh_of(mask))&mask)); }
static void scbd(u32 m,u16 bits,int set){
    u16 v=(u16)(0x2u|g8b_scbd);
    v=set?(u16)(v|bits):(u16)(v&~bits);
    if(v!=g8b_scbd){ g8b_scbd=v; rtw_w16(m,0x00AA,(u16)(v|BIT(15))); }
}
static void rtw8822b_coex_wifi_only(u32 m){
    u8 rfe=g_rtw_efuse.rfe_option;
    w8set(m,0x0550,BIT(3));
    scbd(m,BIT(8),rfe==2||rfe==4);
    rtw_w8(m,0x0974,0xFF);
    w8m(m,0x1991,0x3,0x0);
    w8m(m,0x0CBE,BIT(3),0x0);
    lte_mask(m,0x38,BIT(7),0);
    lte_mask(m,0xA0,0xFFFF,0xFFFF);
    lte_mask(m,0xA4,0xFFFF,0xFFFF);
    w8set(m,0x0550,BIT(3));
    w8m(m,0x0790,0x3F,0x5);
    rtw_w8(m,0x0778,0x1);
    w32set(m,0x0040,BIT(5));
    w32set(m,0x0040,BIT(9));
    w8set(m,0x04C6,BIT(4));
    w8clr(m,0x04C6,BIT(5));
    rtw_w16(m,0x0762,(u16)(rtw_r16(m,0x0762)|BIT(12)));
    w8m(m,0x06CC,BIT(3),1);
    w8m(m,0x06CC,BIT(4),1);
    w8m(m,0x06CF,BIT(3),1);
    lte_mask(m,0x38,0xC000,1); lte_mask(m,0x38,0x0C00,1);
    lte_mask(m,0x38,0x3000,3); lte_mask(m,0x38,0x0300,3);
    w8set(m,0x0073,BIT(26)>>24);
    if(!(rfe==0x12||rfe==0x15||rfe==0x16)){
        w8m(m,0x004E,BIT(23)>>16,0x0);
        w8m(m,0x004F,BIT(24)>>24,0x1);
        rtw_w8(m,0x0CB4,0x77);
        w8m(m,0x0CBD,0x3,0x2);
    }
    scbd(m,BIT(0)|BIT(1),1);
    if(g8b_share_ant){ rtw_w32(m,0x06C0,0x55555555); rtw_w32(m,0x06C4,0x55555555); rtw_w32(m,0x06C8,0xF0FFFFFF); }
}

const rtw_chip rtw8822b_chip = {
    .name = "RTL8822BE",
    .vendor = 0x10EC, .device = 0xB822,
    .bus = RTW_BUS_PCIE,
    .n_paths = 2,
    .wcpu = RTW_WCPU_3081,
    .txff_pages = 262144/128,
    .rxff_size = 24576,
    .txff_size = 262144,
    .rsvd_drv_pages = 8,
    .csi_pages = 0, .cond_mode = RTW_COND_FULL_RFE, .sys_func_en = 0xDC, .fw_rxff_size = 12288,
    .tx_desc_sz = 48, .rx_desc_sz = 24, .tx_buf_desc_sz = 16,
    .fw_start = _binary_rtw8822b_fw_bin_start,
    .fw_end = _binary_rtw8822b_fw_bin_end,
    .fw_name = "rtw8822b_fw.bin",
    .page_table = { 64, 64, 64, 64, 1 },
    .rqpn = { 2, 2, 1, 1, 0, 3 },
    .mac_tbl = { rtw8822b_cmac, sizeof(rtw8822b_cmac)/8, 1 },
    .bb_tbl = { rtw8822b_cbb, sizeof(rtw8822b_cbb)/8, 4 },
    .agc_tbl = { rtw8822b_cagc, sizeof(rtw8822b_cagc)/8, 4 },
    .agc_btg_tbl = { 0, 0, 0 },
    .rf_tbl = { { rtw8822b_crf_a, sizeof(rtw8822b_crf_a)/8, 5 }, { rtw8822b_crf_b, sizeof(rtw8822b_crf_b)/8, 5 } },
    .power_on = rtw8822b_power_on,
    .power_off = rtw8822b_power_off,
    .pcie_phy = rtw8822b_pcie_phy,
    .efuse_phy = 1024, .efuse_log = 768, .efuse_prot = 96,
    .efuse_parse = rtw8822b_efuse_parse,
    .mac_init = rtw8822b_mac_init,
    .phy_init = rtw8822b_phy_init,
    .set_channel = rtw8822b_set_channel,
    .set_tx_power = rtw8822b_set_tx_power,
    .calibrate = rtw8822b_calibrate,
    .coex_wifi_only = rtw8822b_coex_wifi_only,
    .rfe_setup = rtw8822b_rfe_setup,
    .phy_post = rtw8822b_phy_post,
    .rf_base = { 0x2800, 0x2C00 },
    .rf_sipi = { 0xC90, 0xE90 },
    .rf_mix = 0,
};
