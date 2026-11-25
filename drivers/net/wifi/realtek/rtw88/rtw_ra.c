// SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause
/* Copyright(c) 2018-2019  Realtek Corporation
 * Rate mask / rate id selection and the C2H RA report decoding follow rtw88 main.c rtw_update_sta_info,
 * get_rate_id and fw.c rtw_fw_send_ra_info / rtw_fw_ra_report_iter. */
#include "include/nvx/base.h"
#include "include/nvx/lib/base/fmt.h"
#include "include/nvx/drivers/net/wifi/rtw8821ce.h"
#include "include/nvx/drivers/net/wifi/rtw_ra.h"
#include "include/nvx/drivers/net/wifi/rtw_chip.h"
#include "include/nvx/drivers/net/wifi/rtw_bw.h"
#include "include/nvx/drivers/net/wifi/rtw_coex.h"

int g_rtw_ra_ies_n=0, g_rtw_ra_fail=0, g_rtw_ra_dead=0;
u8 g_rtw_ra_ies[96];
static u8 g_rtw_ra_ies_last[96]; static int g_rtw_ra_ies_last_n=0;
void rtw_ra_ies_restore(void){ for(int i=0;i<g_rtw_ra_ies_last_n;i++)g_rtw_ra_ies[i]=g_rtw_ra_ies_last[i]; g_rtw_ra_ies_n=g_rtw_ra_ies_last_n; }
rtw_ra_rpt g_rtw_ra_rpt;
u32 g_rtw_ra_sent_mask=0; u8 g_rtw_ra_sent_rid=0, g_rtw_ra_sent_sgi=0;

void rtw_ra_peer_ies(const u8* ies,int len){
    int n=0;
    for(int o=0;o+2<=len;){ int t=ies[o], l=ies[o+1]; if(o+2+l>len)break;
        if((t==1||t==50||t==45||t==191)&&n+2+l<=(int)sizeof g_rtw_ra_ies){ for(int i=0;i<2+l;i++)g_rtw_ra_ies[n+i]=ies[o+i]; n+=2+l; }
        o+=2+l; }
    g_rtw_ra_ies_n=n; }

static u32 rtw_ra_legacy_bits(const u8* r,int nr){
    static const u8 RV[12]={2,4,11,22,12,18,24,36,48,72,96,108}; u32 b=0;
    for(int i=0;i<nr;i++){ u8 v=r[i]&0x7F; for(int k=0;k<12;k++) if(RV[k]==v)b|=1u<<k; }
    return b; }

int rtw_ra_build(int band5,int ht,const u8* rates,int nr,const u8* ies,int il,u32* mask_out,u8* sgi_out){
    u32 leg=0, mcs=0; int peer_ht=0, sgi=0, have=0;
    for(int o=0;ies&&o+2<=il;){ int t=ies[o], l=ies[o+1]; if(o+2+l>il)break; const u8* v=ies+o+2;
        if(t==1||t==50){ leg|=rtw_ra_legacy_bits(v,l); have=1; }
        if(t==45&&l>=4){ peer_ht=1; u16 cap=(u16)(v[0]|(v[1]<<8)); sgi=(cap&(1u<<5))?1:0; mcs=v[3]; }
        o+=2+l; }
    if(!have)leg=rtw_ra_legacy_bits(rates,nr);
    if(band5)leg&=0xFF0u;
    int h=ht&&(peer_ht||!il); if(h&&!peer_ht)mcs=0xFF;
    u32 m=h?(mcs<<12)&0xFF000u:0; m|=leg; u32 bak=m; u8 rid;
    if(band5){ if(h){ m&=0xFF000u|0x30u; rid=RTW_RATEID_GN_N1SS; } else rid=RTW_RATEID_G; }
    else if(h){ m&=0xFF000u|0x5u|0x10u; rid=RTW_RATEID_BGN_20M_1SS; }
    else if(leg<=0xFu){ rid=RTW_RATEID_B_20M; }
    else { m&=0xFF0u|0x5u; rid=RTW_RATEID_BG; }
    if(!(m&~0xFFFu))m|=bak&~0xFFFu;
    if(!m)m|=bak&0xFFFu;
    if(!m)m=band5?0xFF0u:0xFFFu;
    *mask_out=m; *sgi_out=(u8)(h&&sgi); return rid; }

int rtw_ra_fw_setup(u32 m,int ht,const u8* rates,int nr,u32* mask_out){
    u32 mask; u8 sgi; int rid;
    if(g_rtw_ra_dead){ rid=rtw_ra_build(g_wifi_band5,ht,rates,nr,0,0,&mask,&sgi); sgi=(ht&2)?1:0; }
    else rid=rtw_ra_build(g_wifi_band5,ht,rates,nr,g_rtw_ra_ies,g_rtw_ra_ies_n,&mask,&sgi);
    if(!g_rtw_ra_dead&&g_rtw_ra_ies_n){ for(int i=0;i<g_rtw_ra_ies_n;i++)g_rtw_ra_ies_last[i]=g_rtw_ra_ies[i]; g_rtw_ra_ies_last_n=g_rtw_ra_ies_n; }
    if(!g_rtw_ra_dead&&g_rtw_bw_cur!=RTW_BW_20&&(mask&0xFF000u))sgi=rtw_bw_sgi(g_rtw_bw_cur,g_rtw_ra_ies,g_rtw_ra_ies_n,sgi);
    g_rtw_ra_ies_n=0; for(u32 i=0;i<sizeof g_rtw_ra_rpt;i++)((u8*)&g_rtw_ra_rpt)[i]=0;
    u32 w0=H2C_RA_INFO|((u32)RTW_RA_MACID<<8)|((u32)rid<<16)|(1u<<21)|(sgi?(1u<<23):0)|(1u<<30);
    { u8 r8=(u8)rid; rtw_bw_ra(&r8,&mask,&w0); rid=r8; }
    if(rtw_h2c_cmd(m,H2C_MEDIA_STATUS|(1u<<8)|((u32)RTW_RA_MACID<<16),0)<0||rtw_h2c_cmd(m,w0,mask)<0){
        if(++g_rtw_ra_fail>=3&&g_wifi_ra){ g_wifi_ra=0; rtw_jlog("  firmware rates: commands keep failing, using fixed rate for this session","",g_rtw_ra_fail); }
        return -1; }
    g_rtw_ra_fail=0; g_rtw_ra_sent_mask=mask; g_rtw_ra_sent_rid=(u8)rid; g_rtw_ra_sent_sgi=sgi;
    if(g_wifi_ra)rtw_jlog("  firmware rates: rate mask 0x","",(int)mask);
    *mask_out=mask; return rid; }

static const u16 RTW_RA_LEG[12]={10,20,55,110,60,90,120,180,240,360,480,540};
static const u16 RTW_RA_HT20[10]={65,130,195,260,390,520,585,650,780,867};
static const u16 RTW_RA_HT40[10]={135,270,405,540,810,1080,1215,1350,1620,1800};
static const u16 RTW_RA_VHT80[10]={293,585,878,1170,1755,2340,2633,2925,3510,3900};

u32 rtw_ra_desc_rate100k(u8 rate,int sgi,int bw){
    if(rate<12)return RTW_RA_LEG[rate];
    u32 mcs, nss;
    if(rate<0x2C){ mcs=(u32)(rate-12)%8u; nss=(u32)(rate-12)/8u+1u; }
    else if(rate<0x54){ mcs=(u32)(rate-0x2C)%10u; nss=(u32)(rate-0x2C)/10u+1u; }
    else return 0;
    const u16* t=bw>=2?RTW_RA_VHT80:bw==1?RTW_RA_HT40:RTW_RA_HT20; u32 r=(u32)t[mcs]*nss;
    return sgi?(r*10u+4u)/9u:r; }

void rtw_c2h_rx(const u8* c,int len){
    if(len<3)return;
    g_rtw_ra_rpt.c2h_n++;
    if(c[0]==C2H_BT_INFO){ rtw_coex_c2h(c+2,len-2); return; }
    if(c[0]!=C2H_RA_RPT||len-2<RTW_C2H_RA_RPT_SIZE)return;
    const u8* p=c+2; if(p[1]!=RTW_RA_MACID)return;
    if(g_rtw_ra_rpt.n&&g_rtw_ra_rpt.seq==c[1]&&g_rtw_ra_rpt.t==g_ticks)return;
    g_rtw_ra_rpt.seq=c[1]; g_rtw_ra_rpt.rate=p[0]&0x7F; g_rtw_ra_rpt.sgi=(p[0]>>7)&1; g_rtw_ra_rpt.ratio=p[5]; g_rtw_ra_rpt.bw=p[6];
    g_rtw_ra_rpt.rate100k=rtw_ra_desc_rate100k(g_rtw_ra_rpt.rate,g_rtw_ra_rpt.sgi,g_rtw_ra_rpt.bw); g_rtw_ra_rpt.t=g_ticks; g_rtw_ra_rpt.n++; rtw_bw_ra_report(g_rtw_ra_rpt.rate); }

int rtw_ra_fw_mbps(void){ if(!g_wifi_ra||!g_rtw_ra_rpt.n||g_ticks-g_rtw_ra_rpt.t>RTW_RA_RPT_STALE)return -1; return (int)((g_rtw_ra_rpt.rate100k+5u)/10u); }

char* rtw_ra_rate_name(char* p,u8 rate){
    static const char* L[12]={"1M","2M","5.5M","11M","6M","9M","12M","18M","24M","36M","48M","54M"};
    if(rate<12)return scopy(p,L[rate]);
    if(rate<0x2C){ p=scopy(p,"MCS"); return udec(p,(u32)(rate-12)); }
    p=scopy(p,"VHT "); p=udec(p,(u32)(rate-0x2C)/10u+1u); p=scopy(p,"SS MCS"); return udec(p,(u32)(rate-0x2C)%10u); }

char* rtw_ra_status(char* p){
    p=scopy(p,g_wifi_ra?"fw":"fixed"); if(g_rtw_ra_dead)p=scopy(p," (AP rate list off)");
    p=scopy(p,", rate id "); p=udec(p,g_rtw_ra_sent_rid); p=scopy(p,", mask 0x"); p=hx(p,g_rtw_ra_sent_mask,5); if(g_rtw_ra_sent_sgi)p=scopy(p,", SGI");
    if(!g_rtw_ra_rpt.n)return scopy(p,", no firmware rate report yet");
    p=scopy(p,", firmware rate "); p=rtw_ra_rate_name(p,g_rtw_ra_rpt.rate); if(g_rtw_ra_rpt.sgi)p=scopy(p," SGI");
    p=scopy(p,g_rtw_ra_rpt.bw>=2?" 80 MHz ":g_rtw_ra_rpt.bw==1?" 40 MHz ":" 20 MHz ");
    p=udec(p,g_rtw_ra_rpt.rate100k/10u); *p++='.'; p=udec(p,g_rtw_ra_rpt.rate100k%10u); p=scopy(p," Mbit/s, ratio "); p=udec(p,g_rtw_ra_rpt.ratio);
    p=scopy(p,", reports "); p=udec(p,g_rtw_ra_rpt.n); if(g_ticks-g_rtw_ra_rpt.t>RTW_RA_RPT_STALE)p=scopy(p," (stale)");
    return p; }
