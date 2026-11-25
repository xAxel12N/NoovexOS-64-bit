/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * Derived from Linux drivers/net/wireless/realtek/rtw89/fw.c (rtw89_hw_scan_start/complete, rx filter during scan) and
 * mac.c (rtw89_mac_c2h_scanofld_rsp, struct rtw89_c2h_scanofld fields from fw.h); GPL-2.0 OR BSD-3-Clause, used under BSD-3-Clause.
 * Copyright(c) 2019-2022  Realtek Corporation
 * Licence text: drivers/net/wifi/realtek/rtw89/LICENCE.BSD-3-Clause
 */
#include "drivers/net/wifi/realtek/rtw89/rtw89.h"
#include "include/nvx/drivers/net/wifi/realtek/rtw89/rtw89_core.h"
#include "include/nvx/drivers/net/wifi/realtek/rtw89/rtw89_pci.h"
#include "include/nvx/drivers/net/wifi/realtek/rtw89/rtw89_mac.h"

struct rtw89_scan_state g_r89scan;
int (*rtw89_scan_pre_hook)(struct rtw89_softc* sc);
static rtw89_c2h_cb s3_prev;
static u8 s3_buf[2048];
static const u8 s3_def[]={1,2,3,4,5,6,7,8,9,10,11,12,13,36,40,44,48,149,153,157,161,165};
#define S3_POLLS_PER_CH 40u

static u32 s3_le32(const u8* p){ return (u32)p[0]|((u32)p[1]<<8)|((u32)p[2]<<16)|((u32)p[3]<<24); }
static void s3_end(struct rtw89_softc* sc){ g_r89scan.active=0; g_r89scan.ended=1; rtw89_mac_set_rx_fltr(sc,RTW89_DEFAULT_RX_FLTR); }

void rtw89_scan_c2h(struct rtw89_softc* sc,u8 cat,u8 cls,u8 func,const u8* pl,u32 len){
    if(cat==RTW89_C2H_CAT_MAC&&cls==RTW89_C2H_CL_OFLD&&func==RTW89_C2H_FUNC_SCANOFLD_RSP&&len>=16){
        u32 w2=s3_le32(pl); u8 rsn=(u8)((w2>>16)&0xF); g_r89scan.events++; g_r89scan.last_rsn=rsn;
        if(rsn==RTW89_SCAN_ENTER_CH_NOTIFY){ g_r89scan.cur_ch=(u8)(w2&0xFF); g_r89scan.enter++; }
        else if(rsn==RTW89_SCAN_LEAVE_CH_NOTIFY)g_r89scan.leave++;
        else if(rsn==RTW89_SCAN_END_SCAN_NOTIFY&&g_r89scan.active)s3_end(sc);
        return; }
    if(s3_prev&&s3_prev!=rtw89_scan_c2h)s3_prev(sc,cat,cls,func,pl,len); }

static void s3_attach(void){ if(rtw89_c2h_hook!=rtw89_scan_c2h){ s3_prev=rtw89_c2h_hook; rtw89_c2h_hook=rtw89_scan_c2h; } }

int rtw89_scan_begin(struct rtw89_softc* sc,const u8* ch,int n){
    if(!sc||sc->state!=RTW89_ST_READY)return -1;
    if(g_r89scan.active)return 0;
    if(!ch||n<=0){ ch=s3_def; n=(int)sizeof s3_def; }
    u8 nch=0; for(int i=0;i<n&&nch<RTW89_SCAN_MAX_CH;i++) if(rtw89_chan_valid(ch[i]))g_r89scan.ch[nch++]=ch[i];
    if(!nch)return -2;
    if(rtw89_scan_pre_hook&&rtw89_scan_pre_hook(sc)<0){ g_r89scan.start_err++; return -1; }
    s3_attach();
    g_r89scan.nch=nch; g_r89scan.enter=g_r89scan.leave=g_r89scan.polls=0; g_r89scan.ended=0; g_r89scan.cur_ch=0;
    rtw89_mac_set_rx_fltr(sc,RTW89_DEFAULT_RX_FLTR&~(B_AX_A_BCN_CHK_EN|B_AX_A_BC|B_AX_A_A1_MATCH));
    int r=rtw89_hw_scan_start(sc,g_r89scan.ch,nch);
    if(r<0){ g_r89scan.start_err++; rtw89_mac_set_rx_fltr(sc,RTW89_DEFAULT_RX_FLTR); return -1; }
    g_r89scan.active=1; return 0; }

void rtw89_scan_poll(struct rtw89_softc* sc,u32 budget){
    if(!sc||sc->state!=RTW89_ST_READY||!rtw89_hci||!rtw89_hci->c2h_rx)return;
    while(budget--){ int r=rtw89_hci->c2h_rx(sc,s3_buf,sizeof s3_buf); if(r<=0)break; rtw89_c2h_handle(sc,s3_buf,(u32)r); }
    if(g_r89scan.active&&++g_r89scan.polls>(u32)g_r89scan.nch*S3_POLLS_PER_CH+100u){ rtw89_hw_scan_stop(sc); s3_end(sc); } }

int rtw89_scan_finished(struct rtw89_softc* sc){ (void)sc; return !g_r89scan.active; }
