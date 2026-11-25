// SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause
/* Copyright(c) 2018-2019  Realtek Corporation */
#include "include/nvx/base.h"
#include "include/nvx/core/klog.h"
#include "include/nvx/drivers/net/wifi/rtw_efuse.h"
#include "include/nvx/drivers/net/wifi/rtw8821ce.h"
#include "include/nvx/drivers/net/wifi/rtw_txpwr.h"
#include "drivers/net/wifi/realtek/rtw88/chips/rtw8821c_txpwr_tbl.h"

#define TP_MAX 63
u32 rtw_find_mmio(void);
const char* regdb_country(void) __attribute__((weak));
int regdb_max_dbm(u8 ch) __attribute__((weak));
int g_rtw_txpwr_tbl=1, g_rtw_txpwr_dead=0, g_rtw_txpwr_regd=-1, g_rtw_txpwr_fails=0, g_rtw_txpwr_used=0;
u8 g_rtw_txpwr_idx[RTW_TP_NRATE];
int g_rtw_txpwr_bw=0; u8 g_rtw_txpwr_pri=0, g_rtw_txpwr_c40=0;
static signed char tp_off[2][RTW_TP_NRATE]; static signed char tp_base[2][RTW_TP_NRS]; static int tp_ready=0;
static const u8 tp_rs_lo[RTW_TP_NRS]={0,4,12,20,44,54}, tp_rs_n[RTW_TP_NRS]={4,8,8,8,10,10};
static const char* const tp_regd_name[RTW_TP_NREGD]={"fcc","mkk","etsi","ic","kcc","acma","chile","ukraine","mexico","cn","qatar","uk","ww"};

static int tp_bcd(u32 v,int i){ v>>=i*8; return (int)((v>>4)&0xF)*10+(int)(v&0xF); }
static int tp_rate0(u32 a){ switch(a&0xFFu){ case 0x20:return 0; case 0x24:return 4; case 0x28:return 8; case 0x2C:return 12; case 0x30:return 16; case 0x34:return 20; case 0x38:return 24; case 0x3C:return 44; case 0x40:return 48; case 0x44:return 52; case 0x48:return 56; case 0x4C:return 60; } return -1; }
static int tp_rs_of(int r){ for(int s=0;s<RTW_TP_NRS;s++) if(r>=tp_rs_lo[s]&&r<tp_rs_lo[s]+tp_rs_n[s])return s; return -1; }

void rtw_txpwr_tables_init(void){
    for(int b=0;b<2;b++)for(int r=0;r<RTW_TP_NRATE;r++)tp_off[b][r]=0;
    for(int i=0;i<(int)(sizeof(rtw8821c_bb_pg_type0)/sizeof(rtw8821c_bb_pg_type0[0]));i++){ const u32* e=rtw8821c_bb_pg_type0[i]; int r0=tp_rate0(e[3]); if(e[0]>1||e[1]||r0<0)continue;
        for(int k=0;k<4&&r0+k<RTW_TP_NRATE;k++)tp_off[e[0]][r0+k]=(signed char)tp_bcd(e[5],k); }
    for(int b=0;b<2;b++)for(int s=0;s<RTW_TP_NRS;s++){ int bi=tp_rs_lo[s]+tp_rs_n[s]-(tp_rs_n[s]==10?3:1); signed char base=tp_off[b][bi]; tp_base[b][s]=base; for(int k=0;k<tp_rs_n[s];k++)tp_off[b][tp_rs_lo[s]+k]=(signed char)(tp_off[b][tp_rs_lo[s]+k]-base); }
    tp_ready=1;
}

static int tp_lmt_raw(int regd,int band,int bw,int rs,int ch){
    int v=TP_MAX;
    for(int i=0;i<(int)(sizeof(rtw8821c_txpwr_lmt_type0)/sizeof(rtw8821c_txpwr_lmt_type0[0]));i++){ const u8* e=rtw8821c_txpwr_lmt_type0[i];
        if(e[1]!=band||e[2]!=bw||e[3]!=rs||e[4]!=ch)continue;
        if(regd==RTW_TP_REGD_WW||e[0]==regd){ int l=e[5]>TP_MAX?TP_MAX:e[5]; if(regd!=RTW_TP_REGD_WW)return l; if(l<v)v=l; } }
    return v;
}
static int tp_lmt_abs(int regd,int band,int bw,int rs,int ch){
    int l=tp_lmt_raw(regd,band,bw,rs,ch);
    if(band==1&&l==TP_MAX&&(rs==2||rs==4))l=tp_lmt_raw(regd,band,bw,rs==2?4:2,ch);
    if(band==1&&l==TP_MAX&&(rs==3||rs==5))l=tp_lmt_raw(regd,band,bw,rs==3?5:3,ch);
    return l;
}
static u8 tp_cch(u8 ch,int w){ if(!g_rtw_txpwr_bw||!g_rtw_txpwr_pri||w>=g_rtw_txpwr_bw)return ch; if(!w)return g_rtw_txpwr_pri; return g_rtw_txpwr_c40?g_rtw_txpwr_c40:ch; }
int rtw_txpwr_limit(int regd,u8 ch,int bw,int rs){
    int band=ch>14; if(regd<0||regd>=RTW_TP_NREGD)return TP_MAX;
    if(rs<=1)bw=0; else if(rs<=3&&bw>1)bw=1;
    int v=TP_MAX; for(int w=0;w<=bw;w++){ int l=tp_lmt_abs(regd,band,w,rs,tp_cch(ch,w)); if(l<v)v=l; }
    return v;
}

static int tp_s4(int v){ v&=0xF; return (v&8)?v-16:v; }
static int tp_clp(int v){ if(v==0xFF||v<0)return 0x20; return v>63?63:v; }
static int tp_pg2(u8 c){ return c<=2?0:c<=5?1:c<=8?2:c<=11?3:c<=13?4:5; }
static int tp_pg5(u8 c){ static const u8 E[13]={42,50,58,64,106,114,122,130,138,144,155,161,171}; for(int i=0;i<13;i++)if(c<=E[i])return i; return 13; }
void rtw_txpwr_bases(const u8* e,u8 cc,int bw,int* cck,int* of,int* ht){
    int b40, d; *cck=0x20;
    if(cc<=14){ int g=tp_pg2(cc); *cck=tp_clp(e[g]); b40=tp_clp(e[6+(g>4?4:g)]); d=e[11]; }
    else { b40=tp_clp(e[18+tp_pg5(cc)]); d=e[32]; }
    if(d==0xFF)d=0;
    *of=b40+tp_s4(d);
    if(bw==RTW_TP_BW40){ *ht=b40; return; }
    if(bw==RTW_TP_BW80&&cc>14){ int g=tp_pg5(cc), up=g<13?g+1:g, v=e[38]; if(v==0xFF)v=0; *ht=(b40+tp_clp(e[18+up]))/2+tp_s4(v>>4); return; }
    *ht=b40+tp_s4(d>>4); }
static int tp_efuse_base(int r,int cck,int ofdm,int b20){ return r<4?cck:r<12?ofdm:b20; }
static const char tp_cc[]="US0PR0GU0TW0JP1CA3KR4AU5NZ5CL6UA7MX8CN9QA:GB;00<";
int rtw_txpwr_regd_now(void){
    if(g_rtw_txpwr_regd>=0)return g_rtw_txpwr_regd;
    const char* c=regdb_country?regdb_country():0; if(!c||!c[0]||!c[1])return RTW_TP_REGD_ETSI;
    for(int i=0;tp_cc[i];i+=3) if(tp_cc[i]==c[0]&&tp_cc[i+1]==c[1])return tp_cc[i+2]-'0';
    return RTW_TP_REGD_ETSI;
}
int rtw_txpwr_index(u8 ch,int r,int cck,int ofdm,int b20){
    int band=ch>14, rs=tp_rs_of(r); if(rs<0)return 0;
    int off=tp_off[band][r], la=rtw_txpwr_limit(rtw_txpwr_regd_now(),ch,g_rtw_txpwr_bw,rs);
    if(regdb_max_dbm){ int d=regdb_max_dbm(g_rtw_txpwr_bw&&g_rtw_txpwr_pri?g_rtw_txpwr_pri:ch); if(d>0&&d*2<la)la=d*2; }
    int lim=la-tp_base[band][rs];
    if(lim<off)off=lim;
    int v=tp_efuse_base(r,cck,ofdm,b20)+off;
    return v<0?0:v>TP_MAX?TP_MAX:v;
}

int rtw_txpwr_on(void){ return g_rtw_txpwr_tbl&&!g_rtw_txpwr_dead; }

int rtw_txpwr_compute(u8 ch,int cck,int ofdm,int b20){
    if(!tp_ready)rtw_txpwr_tables_init();
    int lo=ch<=14?0:4, any=0;
    for(int r=0;r<RTW_TP_NRATE;r++)g_rtw_txpwr_idx[r]=0;
    for(int r=lo;r<20;r++){ int v=rtw_txpwr_index(ch,r,cck,ofdm,b20); g_rtw_txpwr_idx[r]=(u8)v; if(v)any=1; }
    for(int r=44;r<54;r++)g_rtw_txpwr_idx[r]=(u8)rtw_txpwr_index(ch,r,cck,ofdm,b20);
    if(!any||g_rtw_txpwr_idx[ch<=14?3:11]<8){ g_rtw_txpwr_fails++; if(g_rtw_txpwr_fails>=RTW_TP_MAXFAIL)rtw_txpwr_kill("computed power looks wrong"); return -1; }
    return 0;
}

void rtw_txpwr_write(u32 m,u8 ch){
    int lo=ch<=14?0:4;
    for(int r=lo;r<20;r+=4) rtw_w32(m,0x1d00u+(u32)r,(u32)g_rtw_txpwr_idx[r]|((u32)g_rtw_txpwr_idx[r+1]<<8)|((u32)g_rtw_txpwr_idx[r+2]<<16)|((u32)g_rtw_txpwr_idx[r+3]<<24));
    for(int r=44;r<56;r+=4){ u32 v=0; for(int k=0;k<4&&r+k<54;k++)v|=(u32)g_rtw_txpwr_idx[r+k]<<(k*8); rtw_w32(m,0x1d00u+(u32)r,v); }
    g_rtw_txpwr_used=ch;
}

void rtw_txpwr_kill(const char* why){
    if(g_rtw_txpwr_dead)return;
    g_rtw_txpwr_dead=1;
    char b[112],*p=b; const char* s="wifi: per-rate TX power off for this session ("; while(*s)*p++=*s++; while(*why&&p<b+100)*p++=*why++; *p++=')'; *p=0; klog(b);
    u32 m=rtw_find_mmio(); if(m&&g_wifi_curch)rtw_set_txpwr(m,g_wifi_curch);
}

void rtw_txpwr_join(int r){
    if(!g_rtw_txpwr_used||!rtw_txpwr_on())return;
    if(!r){ g_rtw_txpwr_fails=0; return; }
    if(r>=6&&r<=11)return;
    if(++g_rtw_txpwr_fails>=RTW_TP_MAXFAIL)rtw_txpwr_kill("joins failed with it");
}

void rtw_txpwr_set_mode(int table){ g_rtw_txpwr_tbl=table?1:0; if(table){ g_rtw_txpwr_dead=0; g_rtw_txpwr_fails=0; } g_rtw_txpwr_used=0; u32 m=rtw_find_mmio(); if(m&&g_wifi_curch)rtw_set_txpwr(m,g_wifi_curch); }
int rtw_txpwr_set_regd(const char* n){ if(n[0]=='a'&&n[1]=='u'&&n[2]=='t'&&n[3]=='o'){ g_rtw_txpwr_regd=-1; rtw_txpwr_set_mode(g_rtw_txpwr_tbl); return 0; } for(int i=0;i<RTW_TP_NREGD;i++){ const char* a=tp_regd_name[i]; const char* q=n; while(*a&&*a==*q){ a++; q++; } if(!*a&&!*q){ g_rtw_txpwr_regd=i; rtw_txpwr_set_mode(g_rtw_txpwr_tbl); return 0; } } return -1; }
const char* rtw_txpwr_regd_name(void){ int r=rtw_txpwr_regd_now(); return r>=0&&r<RTW_TP_NREGD?tp_regd_name[r]:"?"; }
