// SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause
/* Copyright(c) 2018-2019  Realtek Corporation
 * Channel width: center channel and primary sub-channel index (rtw88 main.c rtw_update_channel), ADC clock / RF18 /
 * TXSC / RFMOD / RX DFIR values (rtw8821c.c set_channel_bb/rf/rxdfir, mac.c rtw_set_channel_mac), RA info BW and
 * VHT fields (fw.c rtw_fw_send_ra_info). Peer parsing, picking and fallback are NoovexOS code. */
#include "include/nvx/base.h"
#include "include/nvx/lib/base/fmt.h"
#include "include/nvx/drivers/net/wifi/rtw_chip.h"
#include "include/nvx/drivers/net/wifi/rtw_ra.h"
#include "include/nvx/drivers/net/wifi/rtw_bw.h"

int g_rtw_bw_mode=RTW_BWM_AUTO, g_rtw_bw_falls=0, g_rtw_bw_low=0, g_rtw_bw_dead=0;
void wifi_feat_fail(int f,const char* why); int wifi_feat_on(int f);
u8 g_rtw_bw_cur=RTW_BW_20, g_rtw_bw_pidx=0, g_rtw_bw_cch=0, g_rtw_bw_vc0=0, g_rtw_bw_bssid[6];
signed char g_rtw_bw_sec=0;
u8 g_rtw_bw_join=RTW_BW_20, g_rtw_bw_jch=0;
rtw_bw_peer g_rtw_bw_peers[RTW_BW_PEERS];
static int g_rtw_bw_np=0;
u8 g_rtw_bw_drop=0; int g_rtw_bw_live=0, g_rtw_bw_rejoin=0; static u64 g_rtw_bw_dt=0; static u32 g_rtw_bw_drx=0;

static int rtw_bw_c80ok(int c){ static const u8 C[7]={42,58,106,122,138,155,171}; for(int i=0;i<7;i++)if(C[i]==c)return 1; return 0; }
static int rtw_bw_5gok(int p){ return (p>=36&&p<=144&&(p-36)%4==0)||(p>=149&&p<=177&&(p-149)%4==0); }

int rtw_bw_calc(u8 pri,u8 bw,int sec,u8 vc0,rtw_bw_ch* o){
    o->cch=pri; o->pidx=RTW_SC_DONT_CARE; o->cch40=pri; o->bw=RTW_BW_20;
    if(bw==RTW_BW_20)return 0;
    if(bw==RTW_BW_40){
        if(pri>=1&&pri<=14){ if(pri==14)return -1; if(!sec)sec=pri>7?-1:1; int s=(int)pri+4*sec; if(s<1||s>13)return -1; }
        else { if(!rtw_bw_5gok(pri))return -1; int base=pri>=149?149:36, up=((pri-base)/4)&1, g=up?-1:1; if(sec&&sec!=g)return -1; sec=g; }
        o->cch=(u8)(pri+2*sec); o->cch40=o->cch; o->pidx=sec>0?RTW_SC_20_LOWER:RTW_SC_20_UPPER; o->bw=RTW_BW_40; return 0; }
    if(bw!=RTW_BW_80||!rtw_bw_5gok(pri))return -1;
    int c=vc0; if(!c){ int base=pri>=149?149:36, pos=((pri-base)/4)&3; c=pri-pos*4+6; }
    int d=(int)pri-c; if(!rtw_bw_c80ok(c)||(d!=-6&&d!=-2&&d!=2&&d!=6))return -1;
    o->cch=(u8)c; o->pidx=d==2?RTW_SC_20_UPPER:d==6?RTW_SC_20_UPMOST:d==-2?RTW_SC_20_LOWER:RTW_SC_20_LOWEST;
    o->cch40=(u8)(d>0?c+4:c-4); o->bw=RTW_BW_80; return 0; }

u32 rtw_bw_adcclk(u32 v,u8 bw,u8 pidx){
    if(bw==RTW_BW_40)return (v&0xff3ff300u)|0x20020000u|(((u32)pidx&0xFu)<<2)|RTW_BW_40;
    if(bw==RTW_BW_80)return (v&0xfcffcf00u)|0x40040000u|(((u32)pidx&0xFu)<<2)|RTW_BW_80;
    return (v&0xffcffc00u)|0x10010000u; }
u32 rtw_bw_rf18(u8 bw){ return bw==RTW_BW_40?(1u<<11):bw==RTW_BW_80?(1u<<10):((1u<<11)|(1u<<10)); }
u8 rtw_bw_data_sc(u8 bw,u8 pidx){ u8 s40=0; if(bw==RTW_BW_80)s40=(pidx==RTW_SC_20_UPPER||pidx==RTW_SC_20_UPMOST)?RTW_SC_40_UPPER:RTW_SC_40_LOWER; return (u8)((pidx&0xFu)|((s40&0xFu)<<4)); }
u32 rtw_bw_rfmod(u32 v,u8 bw){ v&=~((1u<<7)|(1u<<8)); return v|(bw==RTW_BW_80?(1u<<8):bw==RTW_BW_40?(1u<<7):0u); }
void rtw_bw_dfir(u8 bw,u32* a,u32* b,u32* t,u32* c){ *a=2; *b=bw==RTW_BW_80?1u:2u; *t=bw==RTW_BW_20?1u:0u; *c=bw==RTW_BW_80?1u:0u; }

static int rtw_bw_eq(const u8* a,const u8* b){ for(int i=0;i<6;i++)if(a[i]!=b[i])return 0; return 1; }
rtw_bw_peer* rtw_bw_peer_of(const u8* bssid){ for(int i=0;i<g_rtw_bw_np;i++)if(rtw_bw_eq(g_rtw_bw_peers[i].bssid,bssid))return &g_rtw_bw_peers[i]; return 0; }

void rtw_bw_note(const u8* bssid,const u8* ies,int len){
    u8 htc=0, hop=0, sco=0, vht=0, vw=0, vc0=0;
    for(int o=0;o+2<=len;){ int t=ies[o], l=ies[o+1]; if(o+2+l>len)break; const u8* v=ies+o+2;
        if(t==45&&l>=2)htc=(v[0]&2)?1:0;
        else if(t==61&&l>=2){ sco=v[1]&3; hop=(v[1]&4)?1:0; }
        else if(t==191&&l>=12)vht=1;
        else if(t==192&&l>=3){ vw=v[0]; vc0=v[1]; }
        o+=2+l; }
    rtw_bw_peer* p=rtw_bw_peer_of(bssid);
    if(!p){ if(g_rtw_bw_np<RTW_BW_PEERS)p=&g_rtw_bw_peers[g_rtw_bw_np++]; else { for(int i=1;i<RTW_BW_PEERS;i++)g_rtw_bw_peers[i-1]=g_rtw_bw_peers[i]; p=&g_rtw_bw_peers[RTW_BW_PEERS-1]; }
        p->bad=0; for(int i=0;i<6;i++)p->bssid[i]=bssid[i]; }
    p->ht40=(u8)(htc&&hop&&(sco==1||sco==3)); p->sco=sco; p->vht=vht; p->vw=vw; p->vc0=vc0; }

u8 rtw_bw_pick(const u8* bssid,u8 pri,int wide){
    g_rtw_bw_sec=0; g_rtw_bw_vc0=0; for(int i=0;i<6;i++)g_rtw_bw_bssid[i]=bssid[i]; g_rtw_bw_low=0;
    if(!wide||g_rtw_bw_mode==RTW_BWM_20||(g_rtw_bw_dead&&g_rtw_bw_mode==RTW_BWM_AUTO))return RTW_BW_20;
    rtw_bw_peer* p=rtw_bw_peer_of(bssid); if(!p||!p->ht40||(p->bad&&g_rtw_bw_mode==RTW_BWM_AUTO))return RTW_BW_20;
    rtw_bw_ch c;
    if(g_rtw_bw_mode!=RTW_BWM_40&&pri>14&&p->vht&&p->vw>=1&&rtw_bw_calc(pri,RTW_BW_80,0,p->vc0,&c)==0){ g_rtw_bw_vc0=p->vc0; return RTW_BW_80; }
    int sec=p->sco==1?1:-1;
    if(rtw_bw_calc(pri,RTW_BW_40,sec,0,&c)==0){ g_rtw_bw_sec=(signed char)sec; return RTW_BW_40; }
    return RTW_BW_20; }

void rtw_bw_join_start(const u8* bssid,u8 pri,int wide){ if(wide&&!wifi_feat_on(13))g_rtw_bw_dead=1; g_rtw_bw_join=rtw_bw_pick(bssid,pri,wide); g_rtw_bw_jch=pri; }
u8 rtw_bw_chan(u8 ch){ return (g_rtw_bw_jch&&ch==g_rtw_bw_jch)?g_rtw_bw_join:RTW_BW_20; }
void rtw_bw_leave(void){ g_rtw_bw_jch=0; g_rtw_bw_join=RTW_BW_20; g_rtw_bw_low=0; g_rtw_bw_drop=0; }
void rtw_bw_join_done(const u8* bssid,int rc){
    if(!rc)return;
    int wide=g_rtw_bw_join!=RTW_BW_20; rtw_bw_leave(); if(!wide)return;
    rtw_bw_peer* p=rtw_bw_peer_of(bssid); if(p&&!p->bad){ p->bad=1; g_rtw_bw_falls++; } wifi_feat_fail(13,"join on a wide channel failed"); }

int rtw_bw_ht_cap(u8* lo){ if(g_rtw_bw_cur==RTW_BW_20)return 0; *lo|=(u8)((1u<<1)|(1u<<6)); return 1; }
int rtw_bw_vht_ie(u8* f){
    if(g_rtw_bw_cur!=RTW_BW_80)return 0;
    u32 cap=(1u<<5)|(3u<<23); int n=0; f[n++]=191; f[n++]=12;
    for(int i=0;i<4;i++)f[n++]=(u8)(cap>>(8*i));
    f[n++]=0xFE; f[n++]=0xFF; f[n++]=0; f[n++]=0; f[n++]=0xFE; f[n++]=0xFF; f[n++]=0; f[n++]=0; return n; }

void rtw_bw_ra(u8* rid,u32* mask,u32* w0){
    u8 bw=g_rtw_bw_cur;
    if(bw==RTW_BW_40&&*rid==RTW_RATEID_BGN_20M_1SS)*rid=RTW_RATEID_BGN_40M_1SS;
    if(bw==RTW_BW_80){ *rid=RTW_RATEID_ARFR1_AC_1SS; *mask=(*mask&0xFF0u)|(0x3FFu<<12); *w0=(*w0&~(3u<<28))|(1u<<28); }
    *w0=(*w0&~((0x1Fu<<16)|(3u<<24)))|((u32)(*rid&0x1F)<<16)|((u32)(bw&3)<<24); }

void rtw_bw_ra_report(u8 rate){
    if(g_rtw_bw_cur==RTW_BW_20)return;
    if(rate<=0x0C||rate==0x2C)g_rtw_bw_low++; else g_rtw_bw_low=0;
    if(g_rtw_bw_low<RTW_BW_LOW_RPTS)return;
    g_rtw_bw_low=0; rtw_bw_peer* p=rtw_bw_peer_of(g_rtw_bw_bssid); if(p&&!p->bad){ p->bad=1; g_rtw_bw_falls++; } wifi_feat_fail(13,"only low rates on a wide channel");
    if(g_rtw_bw_jch&&!g_rtw_bw_drop&&g_rtw_bw_mode==RTW_BWM_AUTO)g_rtw_bw_drop=1; }

int rtw_bw_action(u8* f,const u8* bssid,const u8* sa,int vht){
    f[0]=0xD0; f[1]=0; f[2]=0; f[3]=0;
    for(int i=0;i<6;i++){ f[4+i]=bssid[i]; f[10+i]=sa[i]; f[16+i]=bssid[i]; } f[22]=0; f[23]=0;
    if(vht){ f[24]=21; f[25]=2; f[26]=0; } else { f[24]=7; f[25]=0; f[26]=0; }
    return 27; }

int rtw_bw_drop_step(u64 now,u32 rx){
    if(g_rtw_bw_drop==1)return g_rtw_bw_cur==RTW_BW_20?(g_rtw_bw_drop=0,RTW_BWD_NONE):RTW_BWD_NARROW;
    if(g_rtw_bw_drop!=2)return RTW_BWD_NONE;
    if(rx-g_rtw_bw_drx>=RTW_BW_VERIFY_RX){ g_rtw_bw_drop=0; g_rtw_bw_live++; return RTW_BWD_OK; }
    if(now-g_rtw_bw_dt>=RTW_BW_VERIFY_T){ g_rtw_bw_drop=0; g_rtw_bw_rejoin++; return RTW_BWD_REJOIN; }
    return RTW_BWD_NONE; }
void rtw_bw_narrowed(u64 now,u32 rx){ g_rtw_bw_join=RTW_BW_20; g_rtw_bw_drop=2; g_rtw_bw_dt=now; g_rtw_bw_drx=rx; }
u8 rtw_bw_sgi(u8 bw,const u8* ies,int il,u8 sgi20){
    if(bw==RTW_BW_20)return sgi20;
    u8 s40=0, s80=0, vht=0;
    for(int o=0;ies&&o+2<=il;){ int t=ies[o], l=ies[o+1]; if(o+2+l>il)break; const u8* v=ies+o+2;
        if(t==45&&l>=2)s40=(v[0]&(1u<<6))?1:0;
        if(t==191&&l>=4){ vht=1; s80=(v[0]&(1u<<5))?1:0; }
        o+=2+l; }
    if(!il)return sgi20;
    return bw==RTW_BW_80?(u8)(vht&&s80):s40; }

const char* rtw_bw_name(u8 bw){ return bw==RTW_BW_80?"80 MHz":bw==RTW_BW_40?"40 MHz":"20 MHz"; }
char* rtw_bw_status(char* p){
    static const char* M[4]={"auto","20","40","80"};
    p=scopy(p,"  width: "); p=scopy(p,M[g_rtw_bw_mode&3]); p=scopy(p,", now "); p=scopy(p,rtw_bw_name(g_rtw_bw_cur));
    if(g_rtw_bw_cur!=RTW_BW_20){ p=scopy(p,", center channel "); p=udec(p,g_rtw_bw_cch); p=scopy(p,", primary index "); p=udec(p,g_rtw_bw_pidx); }
    rtw_bw_peer* q=rtw_bw_peer_of(g_rtw_bw_bssid);
    if(q&&q->bad)p=scopy(p,", 20 MHz for this AP after a failed join or low rates");
    p=scopy(p,", fallbacks "); p=udec(p,(u32)g_rtw_bw_falls);
    if(g_rtw_bw_drop)p=scopy(p,g_rtw_bw_drop==1?", narrowing to 20 MHz":", checking 20 MHz in session");
    if(g_rtw_bw_live||g_rtw_bw_rejoin){ p=scopy(p,", narrowed in session "); p=udec(p,(u32)g_rtw_bw_live); p=scopy(p,", reconnects "); p=udec(p,(u32)g_rtw_bw_rejoin); }
    return p; }
