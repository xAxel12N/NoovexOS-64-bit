// SPDX-License-Identifier: BSD-3-Clause
/* Copyright(c) 2018-2019  Realtek Corporation
 * Coex run loop, H2C 0x60/0x61/0x69 layouts, scoreboard and PTA table writes follow rtw88 coex.c and fw.c;
 * RTL8723D uses its own table/TDMA sets from rtw8723d.c
 * (dual GPL-2.0 OR BSD-3-Clause; BSD-3 taken). */
#include "include/nvx/base.h"
#include "include/nvx/core/klog.h"
#include "include/nvx/lib/base/fmt.h"
#include "include/nvx/drivers/net/wifi/rtw_efuse.h"
#include "include/nvx/drivers/net/wifi/rtw8821ce.h"
#include "include/nvx/drivers/net/wifi/rtw_chip.h"
#include "include/nvx/drivers/net/wifi/rtw_coex.h"
#include "include/nvx/drivers/net/wifi/rtw_bw.h"
#include "drivers/net/wifi/realtek/rtw88/rtw_coex_core.h"

extern volatile u64 g_ticks;
int bt_hci_up(void);
extern int g_wm_lps; extern u32 g_wnd_bcn_int, g_wnd_rx_uc, g_wnd_tx_frames;
static rtw_cx_st cx;
static struct { int active, disabled, pending, cs, runs, fails, c2h, q, slot, lpsw, inqw; u8 tab, ci[3]; u16 tdma; u64 tq, tb, tinq, td; u32 scbd, ltx, lrx; } cxd={0,0,0,-1,0,0,0,0,-1,0,0,0xFF,{0xFF,0xFF,0xFF},0xFFFF,0,0,0,0,0,0,0};
int g_rtw_coex_nolps=0, g_rtw_coex_en=1;
static int cx_23(void){ return g_rtw_chip&&g_rtw_chip->device==0xD723; }
static int cx_chip_ok(void){ const char* n=g_rtw_chip?g_rtw_chip->name:0; return cx_23()||(n&&n[0]=='R'&&n[3]=='8'&&n[4]=='8'&&n[5]=='2'&&n[6]=='1'); }
static const u32* cx_tab(u8 t){ return cx_23()?rtw_cx_tab_8723d[t]:rtw_cx_tab_8821c[t]; }
static int cx_h2c(u32 m,u8 id,u8 a,u8 b,u8 c,u8 d,u8 e){ return rtw_h2c_cmd(m,(u32)id|((u32)a<<8)|((u32)b<<16)|((u32)c<<24),(u32)d|((u32)e<<8)); }
static void cx_scbd(u32 m,u32 bits,int set){ u32 v=cxd.scbd|0x2u; v=set?(v|bits):(v&~bits); if(v!=cxd.scbd){ cxd.scbd=v; rtw_w16(m,0x00AA,(u16)(v|0x8000u)); } }
void wifi_feat_fail(int f,const char* why); int wifi_feat_on(int f); int wifi_feat_begin(int f); void wifi_feat_ok(int f);
static int cx_fg=0;
static void cx_fail(u32 m,const char* why){
    wifi_feat_fail(14,why); if(cx_fg==1)cx_fg=0;
    cxd.fails++; cxd.disabled=1; cxd.active=0; g_rtw_coex_nolps=0; cx_h2c(m,0x60,0,0,0,0,0);
    int ok=rtw_coex_bt_share(m,0,0); if(!ok)rtw_coex_wifi_only(m);
    klog2("wifi coex: falling back to the Wi-Fi-only antenna setup: ",why); }
static int cx_apply(u32 m,u8 t,u16 d){
    if(t<38&&t!=cxd.tab){ rtw_w32(m,0x06C0,cx_tab(t)[0]); rtw_w32(m,0x06C4,cx_tab(t)[1]); rtw_w32(m,0x06C8,0xF0FFFFFFu); cxd.tab=t; }
    if(d==cxd.tdma)return 1;
    int s4=(d&RTW_CX_4SLOT)?1:0;
    if(s4!=cxd.slot){ if(cx_h2c(m,0x69,0x0B,rtw_cx_slot_byte(s4,g_wnd_bcn_int),0,0,0)<0)return 0; cxd.slot=s4; }
    u8 n=(u8)(d&0xFF); if(n>=28)n=0; const u8* p=cx_23()?rtw_cx_tdma_8723d[n]:rtw_cx_tdma_8821c[n];
    cx_scbd(m,1u<<9,rtw_cx_tdma_scbd(&cx));
    if(rtw_cx_nolps(p)){ g_rtw_coex_nolps=1; if(g_wm_lps&&cxd.lpsw<3){ cxd.lpsw++; if(cx_h2c(m,0x60,0,0,0,0,0)<0)return 0; cxd.pending=1; return 1; } } else g_rtw_coex_nolps=0;
    cxd.lpsw=0;
    if(cx_h2c(m,0x60,p[0],p[1],p[2],p[3],p[4])<0)return 0;
    cxd.tdma=d; return 1; }
static void cx_run(u32 m){
    u8 t; u16 d; int c=rtw_cx_decide(&cx,&t,&d); cxd.runs++;
    if(!cx_apply(m,t,d)){ cx_fail(m,"firmware did not take the TDMA command"); return; }
    if(cx_fg==1){ cx_fg=2; wifi_feat_ok(14); }
    if(c!=cxd.cs){ cxd.cs=c; klog2("wifi coex: ",rtw_cx_names[c]); } }
static void cx_enter(u32 m,int conn){
    if(!cx_fg&&wifi_feat_begin(14))cx_fg=1;
    if(!rtw_coex_bt_share(m,1,conn)){ cx_fail(m,"coex register did not answer"); return; }
    cxd.active=1; cxd.tab=0xFF; cxd.tdma=0xFFFF; cxd.slot=-1; cxd.cs=-1; cxd.scbd=0; cxd.lpsw=0; cxd.inqw=0; cxd.ci[0]=cxd.ci[1]=cxd.ci[2]=0xFF; cxd.tb=g_ticks; cxd.ltx=g_wnd_tx_frames; cxd.lrx=g_wnd_rx_uc;
    cx.have_info=0; cx.lb2=0; cx.inq_page=0; cx.inq_remain=0; cx.inq_fall=0; cx.reinit=0; cx.ign_off=0; cx.wl_busy=0; rtw_cx_link(&cx);
    cx_scbd(m,0x3u,1);
    if(cx_h2c(m,0x61,1,0,0,0,0)<0){ cx_fail(m,"firmware did not take the BT info query"); return; }
    cxd.tq=g_ticks; klog("wifi coex: Bluetooth is up, antenna shared (TDMA coex)"); }
static void cx_leave(u32 m){
    cx_h2c(m,0x60,0,0,0,0,0); g_rtw_coex_nolps=0; cxd.active=0; cxd.cs=-1;
    int ok=rtw_coex_bt_share(m,0,0); klog(ok?"wifi coex: Bluetooth is down, antenna back to Wi-Fi only":"wifi coex: restore failed, Wi-Fi-only setup re-applied"); }
void rtw_coex_c2h(const u8* p,int len){ cxd.c2h++; if(rtw_cx_bt_info(&cx,p,len))cxd.pending=1; }
void rtw_coex_bt_scan(int on){ cx.host_inq=on?1:0; cxd.pending=1; }
int rtw_coex_active(void){ return cxd.active; }
void rtw_coex_reset(void){ cxd.active=0; cxd.disabled=0; cxd.pending=0; cxd.cs=-1; g_rtw_coex_nolps=0; }
void rtw_coex_tick(u32 m,int conn,int connecting,int scanning,u8 ch){
    if(!g_rtw_coex_done){ if(cxd.active){ cxd.active=0; g_rtw_coex_nolps=0; } return; }
    if(!m||cxd.disabled||!cx_chip_ok())return;
    int bt=g_rtw_coex_en&&wifi_feat_on(14)?bt_hci_up():0;
    if(!bt){ if(cxd.active)cx_leave(m); return; }
    if(!cxd.active){ cx_enter(m,conn); if(!cxd.active)return; cxd.pending=1; }
    u8 c5=(u8)(ch>14);
    if(cx.bt_on!=1||cx.wl_conn!=(u8)!!conn||cx.wl_connecting!=(u8)!!connecting||cx.wl_scan!=(u8)!!scanning||cx.under5g!=c5){
        cx.bt_on=1; cx.wl_conn=(u8)!!conn; cx.wl_connecting=(u8)!!connecting; cx.wl_scan=(u8)!!scanning; cx.under5g=c5; cxd.pending=1; }
    if(g_ticks-cxd.tb>=200){ cxd.tb=g_ticks; u8 b=conn?(u8)rtw_cx_busy(g_wnd_tx_frames-cxd.ltx,g_wnd_rx_uc-cxd.lrx):0; cxd.ltx=g_wnd_tx_frames; cxd.lrx=g_wnd_rx_uc; if(b!=cx.wl_busy){ cx.wl_busy=b; cxd.pending=1; } }
    if(!conn&&!connecting&&!scanning&&g_ticks-cxd.td>=10){ cxd.td=g_ticks; rtw_c2h_drain(m); }
    if(cx.inq_fall){ cx.inq_fall=0; cxd.inqw=1; cxd.tinq=g_ticks; }
    if(cxd.inqw&&g_ticks-cxd.tinq>=400){ cxd.inqw=0; cx.inq_remain=0; cxd.pending=1; }
    if(cx.ign_off){ cx.ign_off=0; if(cx_h2c(m,0x63,0,0,0,0,0)<0){ cx_fail(m,"firmware did not take the ignore-WLAN-action command"); return; } }
    { u8 ci[3]; rtw_cx_chinfo(conn,(u8)(conn?(g_rtw_bw_cur!=RTW_BW_20&&g_rtw_bw_cch?g_rtw_bw_cch:ch):0),conn?g_rtw_bw_cur:0,ci);
      if(cx.reinit||ci[0]!=cxd.ci[0]||ci[1]!=cxd.ci[1]||ci[2]!=cxd.ci[2]){ cx.reinit=0; if(cx_h2c(m,0x66,ci[0],ci[1],ci[2],0,0)<0){ cx_fail(m,"firmware did not take the channel info"); return; } cxd.ci[0]=ci[0]; cxd.ci[1]=ci[1]; cxd.ci[2]=ci[2]; } }
    if(g_ticks-cxd.tq>=200){ cxd.tq=g_ticks; cxd.q++; if(cx_h2c(m,0x61,1,0,0,0,0)<0){ cx_fail(m,"firmware did not take the BT info query"); return; } }
    if(cxd.pending){ cxd.pending=0; cx_run(m); } }
char* rtw_coex_status(char* p){
    p=scopy(p,"  coex: ");
    if(!cx_chip_ok())return scopy(p,"not an RTL8821C/RTL8723D, Wi-Fi-only antenna setup\n");
    if(!g_rtw_coex_en||!wifi_feat_on(14))return scopy(p,"off, Wi-Fi-only antenna setup\n");
    if(cxd.disabled){ p=scopy(p,"off after a failed coex command, Wi-Fi-only antenna setup (failures "); p=udec(p,(u32)cxd.fails); return scopy(p,")\n"); }
    if(!cxd.active)return scopy(p,g_rtw_coex_done?"Bluetooth not up, Wi-Fi-only antenna setup\n":"Wi-Fi not started\n");
    p=scopy(p,"active, case "); p=scopy(p,cxd.cs>=0?rtw_cx_names[cxd.cs]:"none yet"); p=scopy(p,"\n  table "); p=udec(p,cxd.tab); p=scopy(p," (0x6c0=0x"); p=hx(p,cxd.tab<38?cx_tab(cxd.tab)[0]:0,8);
    p=scopy(p," 0x6c4=0x"); p=hx(p,cxd.tab<38?cx_tab(cxd.tab)[1]:0,8); p=scopy(p,"), TDMA "); p=udec(p,cxd.tdma&0xFFu); p=scopy(p,(cxd.tdma&RTW_CX_4SLOT)?" 4-slot":" 2-slot");
    p=scopy(p,g_rtw_coex_nolps?", Wi-Fi power save held off":""); p=scopy(p,cx.wl_busy?", Wi-Fi busy":", Wi-Fi quiet");
    p=scopy(p,"\n  BT info "); if(!cx.have_info)p=scopy(p,"none yet"); else { const u8 b[6]={cx.lb2,cx.lb3,cx.hb0,cx.hb1,cx.hb2,cx.hb3}; for(int i=0;i<6;i++){ p=hx(p,b[i],2); *p++=' '; }
        p=scopy(p,cx.link?"connected":"no link"); if(cx.hid)p=scopy(p," HID"); if(cx.a2dp)p=scopy(p," A2DP"); if(cx.hfp)p=scopy(p," HFP"); if(cx.pan)p=scopy(p," PAN"); if(cx.inq_page)p=scopy(p," inquiry/page"); }
    if(cx.host_inq)p=scopy(p,", host scan running");
    p=scopy(p,"\n  runs "); p=udec(p,(u32)cxd.runs); p=scopy(p,", BT info C2H "); p=udec(p,(u32)cxd.c2h); p=scopy(p,", queries "); p=udec(p,(u32)cxd.q); *p++='\n'; *p=0; return p; }
