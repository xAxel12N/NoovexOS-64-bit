// SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause
/* Copyright(c) 2018-2019  Realtek Corporation */
#include "include/nvx/base.h"
#include "include/nvx/drivers/net/wifi/rtw_efuse.h"
#include "include/nvx/drivers/net/wifi/rtw8821ce.h"
#include "include/nvx/drivers/net/wifi/rtw_chip.h"
#include "include/nvx/drivers/net/wifi/rtw_txpwr.h"
#include "drivers/net/wifi/realtek/rtw88/rtw_tables_cond.h"
#include "include/nvx/drivers/net/wifi/rtw_bw.h"

extern const unsigned char _binary_rtw8821c_fw_bin_start[];
extern const unsigned char _binary_rtw8821c_fw_bin_end[];
extern u8 g_wifi_pwr[20];
void ui_pump(void);

static const rtw_pwr_cmd rtw8821c_pwr_on[]={
    {0x001C,0xFF,0x00,1}, {0x0075,0x01,0x01,1}, {0x0067,0x30,0x30,1}, {0x004F,0x06,0x00,1},
    {0x0040,0x04,0x04,1}, {0x0002,0x03,0x00,1}, {0x001F,0x07,0x00,1}, {0x00EF,0x07,0x00,1},
    {0x0005,0x98,0x00,1}, {0x0300,0xFF,0x00,1}, {0x0301,0xFF,0x00,1}, {0x0005,0x1C,0x00,1},
    {0x0075,0x01,0x01,1}, {0x0006,0x02,0x02,2}, {0x0075,0x01,0x00,1}, {0x0006,0x01,0x01,1},
    {0x0005,0x80,0x00,1}, {0x0005,0x18,0x00,1}, {0x0005,0x01,0x01,1}, {0x0005,0x01,0x00,2},
    {0x0020,0x08,0x08,1}, {0x0074,0x20,0x20,1}, {0x0022,0x02,0x00,1}, {0x0062,0xE0,0xE0,1},
    {0x0061,0xE0,0x00,1}, {0x007C,0x02,0x00,1},
};
static const rtw_pwr_cmd rtw8821c_pwr_off[]={
    {0x0093,0x08,0x00,1}, {0x001F,0xFF,0x00,1}, {0x0049,0x02,0x00,1}, {0x0006,0x01,0x01,1},
    {0x0002,0x02,0x00,1}, {0x0005,0x02,0x02,1}, {0x0005,0x02,0x00,2}, {0x0020,0x08,0x00,1},
};
static int rtw8821c_power_on(u32 m){ return rtw_pwr_seq_run(m,rtw8821c_pwr_on,(int)(sizeof(rtw8821c_pwr_on)/sizeof(rtw8821c_pwr_on[0]))); }
static void rtw8821c_power_off(u32 m){ rtw_pwr_seq_run(m,rtw8821c_pwr_off,(int)(sizeof(rtw8821c_pwr_off)/sizeof(rtw8821c_pwr_off[0]))); }

static int rtw8821c_pcie_phy(u32 m){ return rtw_mdio_write(m,0x09,0x6380,1); }

static int rtw8821c_efuse_parse(const u8* e,int len,rtw_efuse_info* o){
    if(len<0x100)return -1;
    for(int i=0;i<6;i++)o->mac[i]=e[0xD0+i];
    o->rfe_option=e[0xCA]; o->crystal_cap=e[0xB9]; o->thermal_meter=e[0xBA];
    o->country_code[0]=e[0xCB]; o->country_code[1]=e[0xCC];
    for(int p=0;p<2;p++)for(int i=0;i<14;i++){ o->tx_pwr_idx_2g[p][i]=0xFF; o->tx_pwr_idx_5g[p][i]=0xFF; }
    for(int i=0;i<6;i++)o->tx_pwr_idx_2g[0][i]=e[0x10+i];
    for(int i=0;i<5;i++)o->tx_pwr_idx_2g[1][i]=e[0x16+i];
    for(int i=0;i<14;i++)o->tx_pwr_idx_5g[0][i]=e[0x22+i];
    return 0;
}

static int rtw8821c_mac_init(u32 m){
    rtw_w8(m,0x0455,0x70);
    rtw_w8(m,0x045E,(u8)(rtw_r8(m,0x045E)|(1u<<2)));
    u16 pre=(u16)(0x1E4u|(1u<<11));
    rtw_w8(m,0x04E5,(u8)(pre&0xFF)); rtw_w8(m,0x04E6,(u8)(pre>>8));
    rtw_w32(m,0x04C8,0xFFu|(0x08u<<8)|(0x20u<<16)|(0x20u<<24));
    rtw_w16(m,0x04CE,(u16)(0x01u|(0x08u<<8)));
    rtw_w8(m,0x1448,0x06); rtw_w8(m,0x144A,0x06);
    rtw_w8(m,0x144C,0x06); rtw_w8(m,0x144E,0x06);
    rtw_w8(m,0x0480,(u8)(rtw_r8(m,0x0480)|(1u<<5)));
    rtw_w8(m,0x05B4,(u8)(rtw_r8(m,0x05B4)&~((1u<<4)|(1u<<5)|(1u<<6))));
    rtw_w16(m,0x0522,0);
    rtw_w8(m,0x051B,0x09);
    rtw_w8(m,0x0512,0x19);
    rtw_w32(m,0x0514,(0x0Au|(0x0Eu<<8)|(0x10u<<16)|(0x10u<<24)));
    rtw_w16(m,0x0502,0x186);
    rtw_w16(m,0x0506,0x3BC);
    rtw_w32(m,0x0544,(0x05u|(0x1Bu<<16)));
    rtw_w16(m,0x055E,(u16)(0x30u|(0x30u<<8)));
    rtw_w8(m,0x0550,(u8)(rtw_r8(m,0x0550)|(1u<<3)));
    rtw_w32(m,0x0540,(0x04u|(0x064u<<8)));
    rtw_w8(m,0x0558,0x04);
    rtw_w8(m,0x0559,0x02);
    rtw_w8(m,0x0521,(u8)(rtw_r8(m,0x0521)&~((1u<<12)>>8)));
    rtw_w16(m,0x06A0,0xFFFF);
    rtw_w16(m,0x06A2,0x0FFF);
    rtw_w16(m,0x06A4,0xFFFF);
    rtw_w32(m,0x0608,0xE400220Eu);
    rtw_w8(m,0x060C,(u8)(12288u>>9));
    rtw_w8(m,0x0606,0x30);
    rtw_w8(m,0x0605,0x30);
    rtw_w8(m,0x0639,0x40);
    rtw_w8(m,0x066C,(u8)(rtw_r8(m,0x066C)|(1u<<1)));
    rtw_w8(m,0x0718,(u8)(rtw_r8(m,0x0718)|(1u<<6)));
    rtw_w32(m,0x07D8,0xb0810041u);
    rtw_w8(m,0x07D4,0x98);
    rtw_w8(m,0x060F,4);
    rtw_w8(m,0x060C,15);
    rtw_w32(m,0x0608,rtw_r32(m,0x0608)|(1u<<28));
    return 0;
}

static void rtw8821c_phy_init(u32 m){
    u8 v=rtw_r8(m,0x0002);
    v|=(u8)(1u<<6); rtw_w8(m,0x0002,v);
    v|=(u8)((1u<<0)|(1u<<1)); rtw_w8(m,0x0002,v);
    v&=(u8)~((1u<<0)|(1u<<1)); rtw_w8(m,0x0002,v);
    v|=(u8)((1u<<0)|(1u<<1)); rtw_w8(m,0x0002,v);
    rtw_w8(m,0x001F,(u8)((1u<<0)|(1u<<1)|(1u<<2)));
    rtw_delay_us(20);
    rtw_w8(m,0x00EF,(u8)((1u<<0)|(1u<<1)|(1u<<2)));
    rtw_delay_us(20);
    rtw_w32(m,0x0808,rtw_r32(m,0x0808)&~((1u<<28)|(1u<<29)));
}

static u8 rtw8821c_cckf=0, rtw8821c_cckp_ok=0; static u32 rtw8821c_cckp[3];
static void rtw8821c_phy_post(u32 m){
    rtw8821c_cckf=0;
    u8 xtal=g_rtw_efuse.crystal_cap;
    if(rtw_efuse_valid&&xtal!=0xFF){
        u32 cap=(u32)(xtal&0x3Fu);
        u32 a=rtw_r32(m,0x0024); a=(a&~0x7E000000u)|((cap<<25)&0x7E000000u); rtw_w32(m,0x0024,a);
        u32 b2=rtw_r32(m,0x0028); b2=(b2&~0x0000007Eu)|((cap<<1)&0x0000007Eu); rtw_w32(m,0x0028,b2);
    }
    rtw_w32(m,0x0A2C,rtw_r32(m,0x0A2C)&~((1u<<18)|(1u<<22)));
    rtw_w32(m,0x0808,rtw_r32(m,0x0808)|((1u<<28)|(1u<<29)));
}

static void rtw8821c_coex_wifi_only(u32 m){
    int rfe=rtw_rfe_type();
    int at_btg=(rfe==2||rfe==10||rfe==7||rfe==15||rfe==4||rfe==12);
    int pol=(rfe==3||rfe==11||rfe==4||rfe==12);
    rtw_w8(m,0x0002,(u8)(rtw_r8(m,0x0002)|0x03u));
    rtw_w8(m,0x0550,(u8)(rtw_r8(m,0x0550)|(1u<<3)));
    rtw_w8(m,0x0790,(u8)((rtw_r8(m,0x0790)&~0x3Fu)|0x05u));
    rtw_w8(m,0x0778,0x01);
    rtw_w32(m,0x0040,rtw_r32(m,0x0040)|(1u<<5)|(1u<<9));
    rtw_w8(m,0x04C6,(u8)((rtw_r8(m,0x04C6)|(1u<<4))&~(1u<<5)));
    rtw_w16(m,0x0762,(u16)(rtw_r16(m,0x0762)|(1u<<12)));
    rtw_w8(m,0x06CF,(u8)(rtw_r8(m,0x06CF)|(1u<<3)));
    int ok=1;
    ok&=rtw_lte_mask(m,0x38,0xC000u,1); ok&=rtw_lte_mask(m,0x38,0x0C00u,1);
    ok&=rtw_lte_mask(m,0x38,0x3000u,3); ok&=rtw_lte_mask(m,0x38,0x0300u,3);
    rtw_w8(m,0x0073,(u8)(rtw_r8(m,0x0073)|0x04u));
    u32 rv;
    if(at_btg){ if(pol)rv=2; else rv=(rfe!=4&&rfe!=2)?3:2; }
    else rv=pol?1:2;
    int sw=!(rfe==5||rfe==6||rfe==13||rfe==14);
    if(sw){
        rtw_w32(m,0x004C,(rtw_r32(m,0x004C)&~(1u<<23))|(1u<<24));
        rtw_w8(m,0x0CB4,0x77);
        rtw_w32(m,0x0CB4,(rtw_r32(m,0x0CB4)&~0xF0000000u)|(rv<<28));
        rtw_w8(m,0x0067,(u8)(rtw_r8(m,0x0067)|(1u<<5)|(1u<<4)));
    }
    rtw_w32(m,0x06C0,0x55555555u); rtw_w32(m,0x06C4,0x55555555u); rtw_w32(m,0x06C8,0xF0FFFFFFu);
    rtw_w16(m,0x00AA,(u16)(0x8000u|0x0003u));
    rtw_w8(m,0xFF1A,0);
    g_rtw_coex_ok=ok;
}

static void rtw8821c_switch_to_wlg(u32 m){
    rtw_w32(m,0x1080,rtw_r32(m,0x1080)|(1u<<16));
    rtw_w32(m,0x0000,rtw_r32(m,0x0000)|(1u<<26));
    u32 reg=rtw_r32(m,0x0cb8);
    reg|=(1u<<20)|(1u<<22)|(1u<<21);
    reg&=~((1u<<16)|(1u<<18)|(1u<<23));
    u32 a84=rtw_r32(m,0x0a84); a84=(a84&~0x00FF0000u)|(0x12u<<16); rtw_w32(m,0x0a84,a84);
    u32 a80=rtw_r32(m,0x0a80); a80=(a80&~0x0000FFFFu)|0x7532u; rtw_w32(m,0x0a80,a80);
    rtw_w32(m,0x0cb8,reg);
}
static void rtw8821c_switch_to_wla(u32 m){
    rtw_w32(m,0x1080,rtw_r32(m,0x1080)|(1u<<16));
    rtw_w32(m,0x0000,rtw_r32(m,0x0000)|(1u<<26));
    u32 reg=rtw_r32(m,0x0cb8); reg|=(1u<<20)|(1u<<22)|(1u<<23); reg&=~((1u<<16)|(1u<<18)|(1u<<21)); rtw_w32(m,0x0cb8,reg);
}
static void rtw8821c_switch_to_btg(u32 m){
    rtw_w32(m,0x1080,rtw_r32(m,0x1080)|(1u<<16));
    rtw_w32(m,0x0000,rtw_r32(m,0x0000)|(1u<<26));
    u32 reg=rtw_r32(m,0x0cb8); reg|=(1u<<16); reg&=~((1u<<18)|(1u<<20)|(1u<<21)|(1u<<22)|(1u<<23));
    u32 a84=rtw_r32(m,0x0a84); a84=(a84&~0x00FF0000u)|(0x0Eu<<16); rtw_w32(m,0x0a84,a84);
    u32 a80=rtw_r32(m,0x0a80); a80=(a80&~0x0000FFFFu)|0xFC84u; rtw_w32(m,0x0a80,a80);
    rtw_w32(m,0x0cb8,reg);
}
static int rtw8821c_rfe_btg(void){ int r=rtw_rfe_type(); return r==2||r==4||r==7||r==0xa||r==0xc||r==0xf; }
static void rtw8821c_ant_band(u32 m,int g5){
    int rfe=rtw_rfe_type();
    if(rfe==5||rfe==6||rfe==13||rfe==14)return;
    if(rfe==2||rfe==10||rfe==7||rfe==15||rfe==4||rfe==12)return;
    int pol=(rfe==3||rfe==11);
    u32 rv=g5?(pol?2u:1u):(pol?1u:2u);
    rtw_w32(m,0x0CB4,(rtw_r32(m,0x0CB4)&~0xF0000000u)|(rv<<28));
}
static void rtw8821c_rfe_setup(u32 m,int g5){
    if(g5)rtw8821c_switch_to_wla(m); else if(rtw8821c_rfe_btg())rtw8821c_switch_to_btg(m); else rtw8821c_switch_to_wlg(m);
    if(g_rtw_coex_done)rtw8821c_ant_band(m,g5);
}

static void rtw8821c_bw20(u32 m,u8 ch){
    u32 v=rtw_r32(m,0x08AC); v=(v&0xffcffc00u)|0x10010000u; rtw_w32(m,0x08AC,v);
    rtw_bb_write_mask(m,0x08C4,1u<<30,1);
    rtw_bb_write_mask(m,0x0948,3u<<28,2); rtw_bb_write_mask(m,0x094C,3u<<28,2); rtw_bb_write_mask(m,0x0C20,1u<<31,1); rtw_bb_write_mask(m,0x08F0,1u<<31,0);
    static const u32 SW[4]={0x200,0x16a,0x101,0x0b6}; u8 sv=rtw_efuse_valid?rtw_logmap[ch<=14?0xC6:0xC7]:0; if(sv>9)sv=0; rtw_bb_write_mask(m,0x0c1c,0xFFE00000u,SW[sv/3]);
}
static void rtw8821c_bwx(u32 m,u8 cc,u8 bw,u8 pidx){
    if(bw==RTW_BW_40)rtw_bb_write_mask(m,0x0A00,1u<<4,pidx==RTW_SC_20_UPPER?1u:0u);
    rtw_w32(m,0x08AC,rtw_bw_adcclk(rtw_r32(m,0x08AC),bw,pidx));
    rtw_bb_write_mask(m,0x08C4,1u<<30,1);
    u32 a,b,t,c; rtw_bw_dfir(bw,&a,&b,&t,&c);
    rtw_bb_write_mask(m,0x0948,3u<<28,a); rtw_bb_write_mask(m,0x094C,3u<<28,b); rtw_bb_write_mask(m,0x0C20,1u<<31,t); rtw_bb_write_mask(m,0x08F0,1u<<31,c);
    static const u32 SW[4]={0x200,0x16a,0x101,0x0b6}; u8 sv=rtw_efuse_valid?rtw_logmap[cc<=14?0xC6:0xC7]:0; if(sv>9)sv=0; rtw_bb_write_mask(m,0x0c1c,0xFFE00000u,SW[sv/3]);
}
static void rtw8821c_bb_band(u32 m,u8 ch){
    if(ch<=14){ rtw_bb_write_mask(m,0x0808,1u<<28,1); rtw_bb_write_mask(m,0x0454,1u<<7,0); rtw_bb_write_mask(m,0x0a80,1u<<18,0); rtw_bb_write_mask(m,0x0814,0x0000FC00u,15); rtw_bb_write_mask(m,0x0c1c,0xF00u,0); rtw_bb_write_mask(m,0x0860,0x1FFE0000u,0x96A); return; }
    rtw_bb_write_mask(m,0x0a80,1u<<18,1); rtw_bb_write_mask(m,0x0454,1u<<7,1); rtw_bb_write_mask(m,0x0808,1u<<28,0); rtw_bb_write_mask(m,0x0814,0x0000FC00u,15);
    rtw_bb_write_mask(m,0x0c1c,0xF00u,ch<=64?1u:ch<=144?2u:3u);
    rtw_bb_write_mask(m,0x0860,0x1FFE0000u,ch<=48?0x494u:ch<=64?0x453u:ch<=116?0x452u:0x412u);
}

static void rtw8821c_cck_lut(u32 m,u32 a,u32 b,u32 c,u32 d){
    static const u8 A[4]={0x1e,0x1c,0x0e,0x0c}; const u32 V[4]={a,b,c,d};
    rtw_rf_write(m,0,0xEE,0x000FFFFFu,2); for(int i=0;i<4;i++){ rtw_rf_write(m,0,0x33,0x000FFFFFu,A[i]); rtw_rf_write(m,0,0x3F,0x000FFFFFu,V[i]); } rtw_rf_write(m,0,0xEE,0x000FFFFFu,0); }
static void rtw8821c_cck_txsf(u32 m,u32 sf2,u32 sf6,u32 flt){ rtw_w32(m,0x0A24,sf2); rtw_bb_write_mask(m,0x0A28,0xFFFFu,sf6&0xFFFFu); rtw_w32(m,0x0AAC,flt); }
u8 rtw8821c_cck_state(u8 cc,u8 bw,int srrc){ if(cc>14)return 0; if(!srrc)return cc==14?1:0; return cc==14?3:(cc==13||(cc==11&&bw==RTW_BW_40))?4:5; }
static void rtw8821c_cck_filter(u32 m,u8 cc,u8 bw){
    if(cc>14)return;
    u8 w=rtw8821c_cck_state(cc,bw,rtw_txpwr_regd_now()==RTW_TP_REGD_CN);
    if(w==rtw8821c_cckf)return;
    if(!rtw8821c_cckp_ok){ rtw8821c_cckp[0]=rtw_r32(m,0x0A24); rtw8821c_cckp[1]=rtw_r32(m,0x0A28); rtw8821c_cckp[2]=rtw_r32(m,0x0AAC); rtw8821c_cckp_ok=1; }
    if(w<3&&rtw8821c_cckf>=3){ rtw_bb_write_mask(m,0x0A20,0xFFFF0000u,0xe82c); rtw8821c_cck_lut(m,0,0,0,0); }
    if(w==1||w==3)rtw8821c_cck_txsf(m,0x0000b81c,0,0x00003667); else if(w==4)rtw8821c_cck_txsf(m,0x64b80c1c,0x8810,0x01235667); else rtw8821c_cck_txsf(m,rtw8821c_cckp[0],rtw8821c_cckp[1],rtw8821c_cckp[2]);
    if(w>=3){ rtw_bb_write_mask(m,0x0A20,0xFFFF0000u,w==4?0xf8fe:0xe82c); if(w==4)rtw8821c_cck_lut(m,0x27,0x27,0x29,0x26); else rtw8821c_cck_lut(m,0,0,0,0); }
    rtw8821c_cckf=w; }
static void rtw8821c_set_tx_power(u32 m,u8 ch){
    if(!g_wifi_txpwr||!rtw_efuse_valid)return;
    int bw=g_rtw_bw_cur; if(bw!=RTW_BW_20&&g_rtw_bw_cch&&ch==g_wifi_curch&&ch!=g_rtw_bw_cch)ch=g_rtw_bw_cch; else if(bw!=RTW_BW_20&&ch!=g_rtw_bw_cch)bw=RTW_BW_20;
    int cck,of,b20; rtw_txpwr_bases(rtw_logmap+0x10,ch,bw,&cck,&of,&b20); g_rtw_txpwr_bw=bw; g_rtw_txpwr_pri=g_wifi_curch; g_rtw_txpwr_c40=bw==RTW_BW_80?(u8)(g_wifi_curch>ch?ch+4:ch-4):ch;
    if(rtw_txpwr_on()&&rtw_txpwr_compute(ch,cck,of,b20)==0){ for(int r=0;r<20;r++)g_wifi_pwr[r]=g_rtw_txpwr_idx[r]; rtw_txpwr_write(m,ch); return; }
    g_rtw_txpwr_used=0;
    for(int r=0;r<20;r++){ int v=r<4?cck:r<12?of:b20; if(v<0)v=0; if(v>63)v=63; g_wifi_pwr[r]=(u8)v; }
    for(int r=(ch<=14?0:4);r<20;r+=4) rtw_w32(m,0x1d00u+(u32)r,(u32)g_wifi_pwr[r]|((u32)g_wifi_pwr[r+1]<<8)|((u32)g_wifi_pwr[r+2]<<16)|((u32)g_wifi_pwr[r+3]<<24));
}

static void rtw8821c_set_channel(u32 m,u8 ch,u8 bw){
    rtw_bw_ch bc; if(bw==RTW_BW_20||rtw_bw_calc(ch,bw,g_rtw_bw_sec,g_rtw_bw_vc0,&bc)!=0)rtw_bw_calc(ch,RTW_BW_20,0,0,&bc);
    u8 cc=bc.cch; bw=bc.bw;
    int g5=ch>14;
    rtw_chmark("    ch.a read RF18 (0x2860)");
    u32 rf18=rtw_rf_read(m,0,0x18,0x000FFFFFu);
    rf18&=~(((1u<<16)|(1u<<9)|(1u<<8)) | 0xFFu | ((1u<<18)|(1u<<17)) | ((1u<<11)|(1u<<10)));
    rf18|=(cc&0xFFu);
    if(g5){ rf18|=(1u<<16)|(1u<<8); if(cc>=100&&cc<=140)rf18|=(1u<<17); else if(cc>140)rf18|=(1u<<18); }
    rf18|=rtw_bw_rf18(bw);
    rtw_chmark("    ch.b antenna switch (0xcb8/0xa80/0xa84)");
    rtw8821c_rfe_setup(m,g5);
    rtw_chmark("    ch.c RF LUTDBG/0x64");
    if(g5)rtw_rf_write(m,0,0xDF,(1u<<6),0);
    else { rtw_rf_write(m,0,0xDF,(1u<<6),1); rtw_rf_write(m,0,0x64,0xFu,0xF); }
    rtw_chmark("    ch.d RF18 write (the channel)");
    rtw_rf_write(m,0,0x18,0x000FFFFFu,rf18);
    rtw_delay_us(100);
    rtw_chmark("    ch.e RF XTALX2 toggle");
    rtw_rf_write(m,0,0xB8,(1u<<19),0);
    rtw_rf_write(m,0,0xB8,(1u<<19),1);
    rtw_chmark("    ch.f MAC channel regs (0x483/0x668)");
    rtw_w8(m,0x0483,rtw_bw_data_sc(bw,bc.pidx));
    rtw_w32(m,0x0024,rtw_r32(m,0x0024)&~(3u<<20));
    rtw_w32(m,0x0668,rtw_bw_rfmod(rtw_r32(m,0x0668),bw));
    if(g5||g_wifi_band5)rtw8821c_bb_band(m,cc);
    if(bw==RTW_BW_20)rtw8821c_bw20(m,ch); else rtw8821c_bwx(m,cc,bw,bc.pidx);
    rtw8821c_cck_filter(m,cc,bw);
    g_rtw_bw_cur=bw; g_rtw_bw_pidx=bc.pidx; g_rtw_bw_cch=cc;
    g_wifi_band5=g5; g_wifi_curch=ch;
    rtw8821c_set_tx_power(m,cc);
    rtw_delay_us(2000);
}

static void rtw8821c_calibrate(u32 m){
    u8 p[32]; for(int i=0;i<32;i++)p[i]=0;
    p[0]=0x01; p[1]=0xFF; p[2]=0x0E; p[4]=9;
    if(rtw_get_nettype(m)==2)p[8]|=2;
    g_rtw_iqk_last=-1;
    if(rtw_h2c_pkt_send(m,p,32)!=0)return;
    int ok=0;
    for(int c=0;c<300;c++){ if(rtw_rf_read(m,0,0x08,0x000FFFFFu)==0xabcdeu){ ok=1; break; } rtw_delay_us(20000); if((c&3)==3)ui_pump(); }
    rtw_rf_write(m,0,0x08,0x000FFFFFu,0);
    g_rtw_iqk_last=ok?(int)(1u|((rtw_r32(m,0x1BF0)&0xFFu)<<8)):-2;
}

const rtw_chip rtw8821c_chip={
    .name="RTL8821CE", .vendor=0x10EC, .device=0xC821, .bus=RTW_BUS_PCIE, .n_paths=1, .wcpu=RTW_WCPU_3081,
    .txff_pages=512, .rxff_size=16384, .txff_size=65536, .rsvd_drv_pages=8, .csi_pages=0,
    .tx_desc_sz=48, .rx_desc_sz=24, .tx_buf_desc_sz=16,
    .fw_start=_binary_rtw8821c_fw_bin_start, .fw_end=_binary_rtw8821c_fw_bin_end, .fw_name="rtw8821c_fw.bin",
    .page_table={16,16,16,14,1},
    .rqpn={2,2,1,1,0,3},
    .mac_tbl={rtw_cmac,(int)(sizeof(rtw_cmac)/sizeof(rtw_cmac[0])/2),1},
    .bb_tbl={rtw_cbb,(int)(sizeof(rtw_cbb)/sizeof(rtw_cbb[0])/2),4},
    .agc_tbl={rtw_cagc,(int)(sizeof(rtw_cagc)/sizeof(rtw_cagc[0])/2),4},
    .agc_btg_tbl={rtw_cagc_btg2,(int)(sizeof(rtw_cagc_btg2)/sizeof(rtw_cagc_btg2[0])/2),4},
    .rf_tbl={{rtw_crf,(int)(sizeof(rtw_crf)/sizeof(rtw_crf[0])/2),5},{0,0,0}},
    .power_on=rtw8821c_power_on, .power_off=rtw8821c_power_off, .efuse_parse=rtw8821c_efuse_parse,
    .mac_init=rtw8821c_mac_init, .phy_init=rtw8821c_phy_init, .set_channel=rtw8821c_set_channel,
    .set_tx_power=rtw8821c_set_tx_power, .calibrate=rtw8821c_calibrate, .coex_wifi_only=rtw8821c_coex_wifi_only,
    .rfe_setup=rtw8821c_rfe_setup, .phy_post=rtw8821c_phy_post, .pcie_phy=rtw8821c_pcie_phy, .efuse_phy=512, .efuse_log=512, .efuse_prot=96,
    .rf_base={0x2800,0x2C00}, .rf_sipi={0xC90,0xE90}, .rf_mix=0,
};
