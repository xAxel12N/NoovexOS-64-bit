/* SPDX-License-Identifier: BSD-3-Clause-Clear */
/*
 * Derived from Linux drivers/net/wireless/mediatek/mt76 (mt76_connac_mcu.c, mt7921/main.c, mt7921/mcu.c).
 * Copyright (C) 2020 MediaTek Inc.
 * Licence text: drivers/net/wifi/mediatek/mt7921/LICENCE.BSD-3-Clause-Clear
 */
#include "drivers/net/wifi/mediatek/mt7921/mt7921.h"
#include "drivers/net/wifi/mediatek/mt7921/mcu/mcu.h"
#include "drivers/net/wifi/mediatek/mt7921/mcu/rate.h"
#include "include/nvx/net/wifi/wifi_hw.h"
#include "include/nvx/net/wifi/wpa_sm.h"
static u8 mt_req[MT_SCAN_REQ_LEN];
static u8 mt_scan_seq;
static u64 mt_scan_t0;
volatile int mt_join_auth=-1,mt_join_assoc=-1;
u16 mt_join_aid;
u32 mt_scan_req_build(struct mt7921_softc* sc,const u8* ch,int n,u8* out,u32 cap){
    if(cap<MT_SCAN_REQ_LEN)return 0;
    mt_zero(out,MT_SCAN_REQ_LEN);
    mt_scan_seq=(u8)((mt_scan_seq+1)&0x7f);
    out[0]=(u8)(mt_scan_seq|(sc->band_idx<<7)); out[1]=sc->bss_idx; out[2]=0; out[3]=1; out[4]=0; out[5]=0;
    out[6]=(u8)SCAN_FUNC_SPLIT_SCAN; out[7]=1;
    if(n<=0||!ch||n>64){ out[158]=0; out[159]=0; out[826]=0; }
    else {
        out[158]=4; out[159]=(u8)(n>32?32:n); out[826]=(u8)(n>32?n-32:0);
        for(int i=0;i<n;i++){ u8* c=i<32?out+160+i*2:out+830+(i-32)*2; int v=ch[i]>=MT7921_VCH6; c[0]=(u8)(v?3:ch[i]>14?2:1); c[1]=(u8)(v?(ch[i]-MT7921_VCH6)*4+1:ch[i]); }
    }
    mt_copy(out+1110,"\xff\xff\xff\xff\xff\xff",6);
    return MT_SCAN_REQ_LEN;
}
int mt7921_hw_scan(struct mt7921_softc* sc,const u8* ch,int n){
    if(!sc||sc->state<MT_ST_READY)return -1;
    if(sc->scanning&&mt_ms()-mt_scan_t0<15000)return -1;
    u32 l=mt_scan_req_build(sc,ch,n,mt_req,sizeof mt_req);
    sc->scanning=1; sc->scan_done=0; sc->nscan=0; mt_scan_t0=mt_ms();
    if(mt7921_mcu_send(sc,MCU_CE_CMD(MCU_CE_CMD_START_HW_SCAN),mt_req,l,0)<0){ sc->scanning=0; return -1; }
    return 0;
}
int mt7921_scan_busy(struct mt7921_softc* sc){
    if(!sc||!sc->scanning)return 0;
    if(mt_ms()-mt_scan_t0>15000){ sc->scanning=0; sc->scan_done=1; u8 b[4]={mt_scan_seq,0,0,0}; mt7921_mcu_send(sc,MCU_CE_CMD(MCU_CE_CMD_CANCEL_HW_SCAN),b,4,0); }
    return sc->scanning;
}
void mt_scan_done_event(struct mt7921_softc* sc,const u8* p,u32 n){
    if(n>=1&&(p[0]&0x7f)!=mt_scan_seq)return;
    sc->scanning=0; sc->scan_done=1;
}
void mt_beacon_loss(struct mt7921_softc* sc){ if(sc->connected){ sc->connected=0; sc->err="beacon loss"; } }
int mt_dev_add(struct mt7921_softc* sc,int en){
    u8 d[16]; mt_zero(d,sizeof d);
    d[0]=sc->omac_idx; d[1]=sc->band_idx; mt_put16(d+4,DEV_INFO_ACTIVE); mt_put16(d+6,12); d[8]=(u8)!!en; d[9]=0; mt_copy(d+10,sc->mac,6);
    u8 b[4+32]; mt_zero(b,sizeof b);
    b[0]=sc->bss_idx; u8* t=b+4;
    mt_put16(t,UNI_BSS_INFO_BASIC); mt_put16(t+2,32); t[4]=(u8)!!en; t[5]=sc->omac_idx; t[6]=sc->omac_idx; t[7]=sc->band_idx;
    mt_put32(t+8,CONNECTION_INFRA_STA); t[12]=1; t[13]=0; mt_put16(t+20,MT_WCID_BMC); mt_put16(t+26,MT_WCID_BMC);
    if(en){ if(mt7921_mcu_send(sc,MCU_UNI_CMD(MCU_UNI_CMD_DEV_INFO_UPDATE),d,sizeof d,1)<0)return -1; return mt7921_mcu_send(sc,MCU_UNI_CMD(MCU_UNI_CMD_BSS_INFO_UPDATE),b,sizeof b,1)<0?-1:0; }
    mt7921_mcu_send(sc,MCU_UNI_CMD(MCU_UNI_CMD_BSS_INFO_UPDATE),b,sizeof b,1);
    return mt7921_mcu_send(sc,MCU_UNI_CMD(MCU_UNI_CMD_DEV_INFO_UPDATE),d,sizeof d,1)<0?-1:0;
}
u32 mt_bss_req_build(struct mt7921_softc* sc,const u8* bssid,u8 ch,int en,u8* out){
    mt_zero(out,4+32+16);
    out[0]=sc->bss_idx; u8* t=out+4;
    mt_put16(t,UNI_BSS_INFO_BASIC); mt_put16(t+2,32); t[4]=1; t[5]=sc->omac_idx; t[6]=sc->omac_idx; t[7]=sc->band_idx;
    mt_put32(t+8,CONNECTION_INFRA_STA); t[12]=(u8)(en?CONN_STATE_CONNECT:CONN_STATE_DISCONNECT); t[13]=0;
    if(bssid)mt_copy(t+14,bssid,6);
    mt_put16(t+20,MT_WCID_BMC); mt_put16(t+22,100); t[24]=1; t[25]=(u8)(sc->band6?0xb1:ch>14?0x31:0x0e); mt_put16(t+26,MT_WCID_AP); if(sc->band6)t[30]=1;
    t=out+36;
    mt_put16(t,UNI_BSS_INFO_RLM); mt_put16(t+2,16); t[4]=ch; t[5]=ch; t[6]=0; t[7]=0; t[8]=1; t[9]=1; t[10]=1; t[11]=4; t[12]=0; t[13]=(u8)(sc->band6?3:ch>14?2:1);
    return 4+32+16;
}
u32 mt_sta_req_build(struct mt7921_softc* sc,u8 wcid,const u8* peer,u8 state,u16 aid,u8* out){
    mt_zero(out,8+20);
    out[0]=sc->bss_idx; out[1]=wcid; mt_put16(out+2,1); out[4]=1; out[5]=sc->omac_idx; out[6]=0;
    u8* t=out+8;
    mt_put16(t,STA_REC_BASIC); mt_put16(t+2,20); mt_put32(t+4,CONNECTION_INFRA_AP); t[8]=state; t[9]=1; mt_put16(t+10,aid);
    if(peer)mt_copy(t+12,peer,6); else mt_copy(t+12,"\xff\xff\xff\xff\xff\xff",6);
    mt_put16(t+18,(u16)(EXTRA_INFO_VER|(state!=CONN_STATE_DISCONNECT?EXTRA_INFO_NEW:0)));
    return 8+20;
}
u32 mt_key_req_build(struct mt7921_softc* sc,u8 wcid,int idx,int type,const u8* key,int len,u8* out){
    u32 kl=8+36;
    mt_zero(out,8+kl);
    out[0]=sc->bss_idx; out[1]=wcid; mt_put16(out+2,1); out[4]=1; out[5]=sc->omac_idx;
    u8* t=out+8;
    mt_put16(t,STA_REC_KEY_V2);
    if(!key||len<=0){ mt_put16(t+2,8); t[4]=0; t[5]=0; return 8+8; }
    if(len>32)return 0;
    u8 cid=type==WIFI_KEY_CCMP?MCU_CIPHER_AES_CCMP:type==WIFI_KEY_BIP?MCU_CIPHER_BIP_CMAC_128:0;
    if(!cid)return 0;
    mt_put16(t+2,(u16)kl); t[4]=1; t[5]=1;
    u8* k=t+8; k[0]=cid; k[1]=36; k[2]=(u8)idx; k[3]=(u8)len; mt_copy(k+4,key,(u32)len);
    return 8+kl;
}
static int mt_sta_send(struct mt7921_softc* sc,u8 wcid,const u8* peer,u8 st,u16 aid){
    u8 b[MT_REQ_MAX]; u32 l=sc->sta_build?sc->sta_build(sc,wcid,peer,st,aid,b):mt_sta_req_build(sc,wcid,peer,st,aid,b);
    if(!l)return -1;
    return mt7921_mcu_send(sc,MCU_UNI_CMD(MCU_UNI_CMD_STA_REC_UPDATE),b,l,1);
}
static int mt_bss_send(struct mt7921_softc* sc,const u8* bssid,u8 ch,int en){
    u8 b[MT_REQ_MAX]; u32 l=sc->bss_build?sc->bss_build(sc,bssid,ch,en,b):mt_bss_req_build(sc,bssid,ch,en,b);
    if(!l)return -1;
    return mt7921_mcu_send(sc,MCU_UNI_CMD(MCU_UNI_CMD_BSS_INFO_UPDATE),b,l,1);
}
static int mt_mgmt_tx(struct mt7921_softc* sc,u8 fc,const u8* body,int n){
    u8 f[24+400]; if(n<0||n>400)return -1;
    mt_zero(f,24); f[0]=fc; mt_copy(f+4,sc->bssid,6); mt_copy(f+10,sc->mac,6); mt_copy(f+16,sc->bssid,6);
    mt_copy(f+24,body,(u32)n);
    return mt7921_tx_data(sc,f,(u32)(24+n));
}
extern int wifi_gen_sae_auth(const struct wpa_io* io) __attribute__((weak));
extern int wifi_hw_assoc_ie(u8* out,int cap) __attribute__((weak));
extern int mt7925_assoc_eht(u8* out,int cap) __attribute__((weak));
int mt7921_has_6g,mt7921_6g_on=1,mt_assoc_eht_res=-1;
static struct { struct mt7921_softc* sc; u16 seq; volatile int h,n; int len[4]; u8 body[4][400]; } mt_sx;
int mt7921_sae_fail;
void mt_sae_rx(const u8* f,u32 len){
    if(!mt_sx.sc||mt_sx.n>=4||len<30||(f[0]&0xFC)!=0xB0||f[24]!=3||f[25]!=0)return;
    u32 n=len-24; if(n>400)n=400; int s=(mt_sx.h+mt_sx.n)%4; mt_copy(mt_sx.body[s],f+24,n); mt_sx.len[s]=(int)n; mt_sx.n++; }
static int mt_sae_tx(void* c,const u8* f,int n,int ph,int round){ (void)c;(void)round; if(ph>0)return 0; if(n<24||mt_mgmt_tx(mt_sx.sc,0xB0,f+24,n-24)<0)return 0; return ph<0?1:3; }
static int mt_sae_rxm(void* c,u8 want,u8* body,int* blen,int ms){ (void)c;
    if(want!=0xB0)return 0; u64 t0=mt_ms(); while(!mt_sx.n&&mt_ms()-t0<(u64)ms)mt7921_rx_poll(mt_sx.sc);
    if(!mt_sx.n)return 0; int s=mt_sx.h; mt_copy(body,mt_sx.body[s],(u32)mt_sx.len[s]); *blen=mt_sx.len[s]; mt_sx.h=(s+1)%4; mt_sx.n--; return 1; }
static const struct wpa_io mt_sae_io={ 0,0,0,0,mt_sae_tx,mt_sae_rxm,0,&mt_sx.seq,0,0 };
int mt7921_sae_auth(struct mt7921_softc* sc){
    int (*volatile sa)(const struct wpa_io*)=wifi_gen_sae_auth; if(!sa)return -1;
    mt_sx.sc=sc; mt_sx.h=mt_sx.n=0; mt_sx.seq=0x100; int r=sa(&mt_sae_io); mt_sx.sc=0;
    if(r==0){ mt7921_sae_fail=0; return 0; } mt7921_sae_fail++; return -1; }
int mt7921_roc_enable=1;
int mt7921_roc_acquire(struct mt7921_softc* sc,u8 ch,u32 ms,u8 type){
    if(!sc||sc->state<MT_ST_READY||!ch||!mt7921_roc_enable)return -1;
    if(sc->roc_active)mt7921_roc_abort(sc);
    u8 b[4+24]; mt_zero(b,sizeof b); u8* t=b+4;
    mt_put16(t,UNI_ROC_ACQUIRE); mt_put16(t+2,24); t[4]=sc->bss_idx; t[5]=++sc->roc_token; t[6]=ch; t[7]=0; t[8]=(u8)(sc->band6?3:ch>14?2:1); t[9]=0;
    t[10]=ch; t[11]=0; t[12]=0; t[13]=ch; t[14]=0; t[15]=type; mt_put32(t+16,ms); t[20]=0xff;
    sc->roc_granted=0;
    if(mt7921_mcu_send(sc,MCU_UNI_CMD(MCU_UNI_CMD_ROC),b,sizeof b,0)<0)return -1;
    sc->roc_active=1;
    u64 t0=mt_ms(); while(!sc->roc_granted&&mt_ms()-t0<1000)mt7921_rx_poll(sc);
    if(sc->roc_granted){ sc->roc_fails=0; sc->roc_grants++; return 0; }
    sc->roc_timeouts++; mt7921_roc_abort(sc);
    if(++sc->roc_fails>=3)mt7921_roc_enable=0;
    return -1;
}
void mt7921_roc_abort(struct mt7921_softc* sc){
    if(!sc||!sc->roc_active)return;
    u8 b[4+12]; mt_zero(b,sizeof b); u8* t=b+4;
    mt_put16(t,UNI_ROC_ABORT); mt_put16(t+2,12); t[4]=sc->bss_idx; t[5]=sc->roc_token; t[6]=0xff;
    sc->roc_active=0; sc->roc_granted=0;
    mt7921_mcu_send(sc,MCU_UNI_CMD(MCU_UNI_CMD_ROC),b,sizeof b,0);
}
void mt7921_roc_event(struct mt7921_softc* sc,const u8* p,u32 n){
    if(!sc||n<4+20)return;
    const u8* g=p+4;
    if(mt_get16(g)!=UNI_EVENT_ROC_GRANT||!sc->roc_active||g[5]!=sc->roc_token)return;
    sc->roc_granted=1;
}
static int mt_wait_st(struct mt7921_softc* sc,volatile int* st,u32 ms){ u64 t0=mt_ms(); while(*st<0&&mt_ms()-t0<ms)mt7921_rx_poll(sc); return *st; }
int mt7921_connect(struct mt7921_softc* sc,const u8* bssid,u8 ch,const char* ssid,u8 akm,u8 ht){
    (void)ht;
    if(!sc||sc->state<MT_ST_READY||!bssid||!ssid||!ch)return -1;
    if(akm&&akm!=2&&akm!=8){ sc->err="only open, WPA2-PSK and WPA3-SAE are supported"; return -1; }
    if(sc->connected)mt7921_disconnect(sc);
    mt_ba_reset(sc,0); mt_copy(sc->bssid,bssid,6); sc->chan=ch;
    if(mt_bss_send(sc,bssid,ch,1)<0)return -1;
    if(mt_sta_send(sc,MT_WCID_AP,bssid,CONN_STATE_DISCONNECT,0)<0)return -1;
    if(!sc->bss_build)mt_mcu_chan_domain_sync(sc);
    mt7921_roc_acquire(sc,ch,3000,MT_ROC_REQ_JOIN);
    int st=-1;
    if(akm==8)st=mt7921_sae_auth(sc);
    else for(int t=0;t<3&&st<0;t++){ static const u8 ab[6]={0,0,1,0,0,0}; mt_join_auth=-1; if(mt_mgmt_tx(sc,0xB0,ab,6)<0)break; st=mt_wait_st(sc,&mt_join_auth,300); }
    if(st!=0){ sc->err=akm==8?"WPA3 SAE authentication failed":"auth failed"; mt7921_roc_abort(sc); mt_bss_send(sc,bssid,ch,0); return -1; }
    u8 body[256]; int n=0; int sl=0; while(ssid[sl]&&sl<32)sl++;
    body[n++]=0x31; body[n++]=0x04; body[n++]=10; body[n++]=0;
    body[n++]=0; body[n++]=(u8)sl; for(int i=0;i<sl;i++)body[n++]=(u8)ssid[i];
    if(ch>14||sc->band6){ static const u8 r5[]={1,8,0x8C,0x12,0x98,0x24,0xB0,0x48,0x60,0x6C}; for(u32 i=0;i<sizeof r5;i++)body[n++]=r5[i]; }
    else { static const u8 r2[]={1,8,0x82,0x84,0x8B,0x96,0x0C,0x12,0x18,0x24,50,4,0x30,0x48,0x60,0x6C}; for(u32 i=0;i<sizeof r2;i++)body[n++]=r2[i]; }
    { int (*volatile ai)(u8*,int)=wifi_hw_assoc_ie; int l=akm==8&&ai?ai(body+n,(int)sizeof body-n-70):0; if(l>0)n+=l; else if(akm==8){ sc->err="no WPA3 RSN element"; mt7921_roc_abort(sc); mt_bss_send(sc,bssid,ch,0); return -1; } }
    if(akm==2){ static const u8 rsn[]={48,20,1,0,0,0x0F,0xAC,4,1,0,0,0x0F,0xAC,4,1,0,0,0x0F,0xAC,2,0,0}; for(u32 i=0;i<sizeof rsn;i++)body[n++]=rsn[i]; }
    int nb=n,ne=n; mt_assoc_eht_res=-1;
    if(ht&&mt_caps_on&&n+66<=(int)sizeof body){ n+=mt_assoc_caps(sc,ch,body+n); ne=n; int (*volatile eh)(u8*,int)=mt7925_assoc_eht; if(sc->gen==3&&eh&&ne>nb){ int l=eh(body+n,(int)sizeof body-n); if(l>0)n+=l; } }
    mt_peer.ok=0; st=-1;
    for(int t=0;t<3&&st<0;t++){ mt_join_assoc=-1; if(mt_mgmt_tx(sc,0x00,body,n)<0)break; st=mt_wait_st(sc,&mt_join_assoc,500); }
    if(n!=ne){ mt_assoc_eht_res=st!=0; if(st!=0){ n=ne; for(int t=0;t<2&&st!=0;t++){ mt_join_assoc=-1; if(mt_mgmt_tx(sc,0x00,body,n)<0)break; st=mt_wait_st(sc,&mt_join_assoc,500); } } }
    if(n!=nb){ if(st==0)mt_caps_fail=0; else { if(++mt_caps_fail>=2)mt_caps_on=0; n=nb; for(int t=0;t<2&&st!=0;t++){ mt_join_assoc=-1; if(mt_mgmt_tx(sc,0x00,body,n)<0)break; st=mt_wait_st(sc,&mt_join_assoc,500); } } }
    if(st!=0){ sc->err="assoc failed"; mt7921_roc_abort(sc); mt_bss_send(sc,bssid,ch,0); return -1; }
    if((sc->sta_build||mt_sta_rate_send(sc,mt_join_aid)<0)&&mt_sta_send(sc,MT_WCID_AP,bssid,CONN_STATE_CONNECT,mt_join_aid)<0){ mt7921_roc_abort(sc); return -1; }
    sc->connected=1;
    if(!akm){ mt7921_roc_abort(sc); mt_ba_port=1; }
    return 0;
}
void mt7921_disconnect(struct mt7921_softc* sc){
    if(!sc||sc->state<MT_ST_READY)return;
    mt_ba_reset(sc,sc->connected);
    if(sc->connected){ static const u8 r[2]={3,0}; mt_mgmt_tx(sc,0xC0,r,2); }
    mt7921_roc_abort(sc);
    mt_sta_send(sc,MT_WCID_AP,sc->bssid,sc->sta_build?MT_STA_REMOVE:CONN_STATE_DISCONNECT,0);
    mt_bss_send(sc,sc->bssid,sc->chan?sc->chan:1,0);
    sc->connected=0;
}
int mt7921_set_key(struct mt7921_softc* sc,int idx,int type,const u8* key,int len,int pairwise){
    if(!sc||sc->state<MT_ST_READY)return -1;
    u8 b[MT_REQ_MAX]; u32 l=sc->key_build?sc->key_build(sc,pairwise?MT_WCID_AP:MT_WCID_BMC,idx,type,key,len,b):mt_key_req_build(sc,pairwise?MT_WCID_AP:MT_WCID_BMC,idx,type,key,len,b);
    if(!l)return -1;
    int r=mt7921_mcu_send(sc,MCU_UNI_CMD(MCU_UNI_CMD_STA_REC_UPDATE),b,l,1);
    if(r==0&&pairwise&&key&&len>0){ mt_sta_send(sc,MT_WCID_AP,sc->bssid,CONN_STATE_PORT_SECURE,mt_join_aid); mt7921_roc_abort(sc); mt_ba_port=1; }
    return r;
}
