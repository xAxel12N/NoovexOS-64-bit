/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * Derived from Linux drivers/net/wireless/realtek/rtw89/fw.c (rtw89_fw_h2c_role_maintain, rtw89_fw_h2c_join_info, rtw89_fw_h2c_cam,
 * rtw89_fw_h2c_default_cmac_tbl), cam.c (rtw89_cam_fill_addr_cam_info, rtw89_cam_fill_bssid_cam_info, rtw89_cam_get_sec_key_cmd)
 * and core.c (TX descriptor header length); GPL-2.0 OR BSD-3-Clause, used under BSD-3-Clause.
 * Copyright(c) 2019-2022  Realtek Corporation
 * Licence text: drivers/net/wifi/realtek/rtw89/LICENCE.BSD-3-Clause
 */
#include "include/nvx/drivers/net/wifi/realtek/rtw89/rtw89_sta.h"
#include "include/nvx/drivers/net/wifi/realtek/rtw89/rtw89_pci.h"
#include "include/nvx/net/wifi/wifi_hw.h"
#include "include/nvx/net/wifi/wpa_sm.h"
#include "include/nvx/core/klog.h"

struct rtw89_sta g_r89s;
int (*rtw89_chan_hook)(struct rtw89_softc* sc,u8 ch);
int (*rtw89_tx_hook)(struct rtw89_softc* sc,const u8* frame,u32 len,int mgmt,u8 rate,u8 macid)=rtw89_pci_tx_frame;

static void s_p32(u8* p,u32 v){ p[0]=(u8)v; p[1]=(u8)(v>>8); p[2]=(u8)(v>>16); p[3]=(u8)(v>>24); }
static void s_zero(u8* d,u32 n){ while(n--)*d++=0; }
static void s_cpy(u8* d,const u8* s,u32 n){ while(n--)*d++=*s++; }
static int s_eq(const u8* a,const u8* b){ for(int i=0;i<6;i++) if(a[i]!=b[i])return 0; return 1; }
static u8 s_hash(const u8* a){ u8 h=0; for(int i=0;i<6;i++)h^=a[i]; return h; }

u32 rtw89_build_role_maintain(u8* o,u8 macid,u8 upd){ s_p32(o,(u32)macid|((u32)(upd&7)<<10)|((u32)RTW89_WIFI_ROLE_STATION<<13)); return 4; }
u32 rtw89_build_join_info(u8* o,u8 macid,int disconn,u8 net_type){ s_p32(o,(u32)macid|(disconn?(1u<<8):0)|((u32)(net_type&3)<<24)|((u32)RTW89_WIFI_ROLE_STATION<<26)); return 4; }

u32 rtw89_build_addr_cam(u8* o,const struct rtw89_sta* s,u8 net_type,int valid){
    s_zero(o,RTW89_ADDR_CAM_LEN); const u8* a=s->me; const u8* t=s->bssid;
    s_p32(o+4,(u32)s->macid|((u32)RTW89_ADDR_CAM_ENT_SIZE<<16));
    s_p32(o+8,(valid?1u:0)|((u32)(net_type&3)<<1)|((u32)s_hash(a)<<16)|((u32)s_hash(t)<<24));
    s_p32(o+12,s->macid);
    s_p32(o+16,(u32)a[0]|((u32)a[1]<<8)|((u32)a[2]<<16)|((u32)a[3]<<24));
    s_p32(o+20,(u32)a[4]|((u32)a[5]<<8)|((u32)t[0]<<16)|((u32)t[1]<<24));
    s_p32(o+24,(u32)t[2]|((u32)t[3]<<8)|((u32)t[4]<<16)|((u32)t[5]<<24));
    s_p32(o+32,(u32)s->macid);
    u32 w9=(net_type==RTW89_NET_TYPE_INFRA?(u32)(s->aid&0xFFF):0)|((u32)(s->sec_mode&3)<<16);
    for(int i=0;i<7;i++)w9|=(u32)(s->sec_keyid[i]&3)<<(18+2*i);
    s_p32(o+36,w9);
    s_p32(o+40,(u32)s->sec_valid|((u32)s->sec_ent[0]<<8)|((u32)s->sec_ent[1]<<16)|((u32)s->sec_ent[2]<<24));
    s_p32(o+44,(u32)s->sec_ent[3]|((u32)s->sec_ent[4]<<8)|((u32)s->sec_ent[5]<<16)|((u32)s->sec_ent[6]<<24));
    s_p32(o+48,(u32)s->macid|((u32)RTW89_BSSID_CAM_ENT_SIZE<<16));
    s_p32(o+52,(valid?1u:0)|(0x3Fu<<2)|((u32)t[0]<<16)|((u32)t[1]<<24));
    s_p32(o+56,(u32)t[2]|((u32)t[3]<<8)|((u32)t[4]<<16)|((u32)t[5]<<24));
    return RTW89_ADDR_CAM_LEN; }

u32 rtw89_build_sec_cam(u8* o,u8 idx,u8 type,const u8* key){
    s_zero(o,RTW89_SEC_CAM_H2C_LEN); s_p32(o,(u32)idx|((u32)RTW89_SEC_CAM_LEN<<16)); s_p32(o+4,type&15u);
    if(key)s_cpy(o+8,key,16);
    return RTW89_SEC_CAM_H2C_LEN; }

u32 rtw89_build_cmac_default(u8* o,u8 macid){
    s_zero(o,RTW89_CMC_TBL_LEN); s_p32(o,(u32)(macid&0x7F)|(1u<<7));
    s_p32(o+4*6,(3u<<16)|(1u<<22)); s_p32(o+4*14,(0xFu<<16)|(3u<<20)|(3u<<22));
    s_p32(o+4*9,1u<<21);
    return RTW89_CMC_TBL_LEN; }

static u32 s_hdr(u8* f,u8 fc,struct rtw89_sta* s){
    s_zero(f,24); f[0]=fc; s_cpy(f+4,s->bssid,6); s_cpy(f+10,s->me,6); s_cpy(f+16,s->bssid,6);
    u16 sq=(u16)((s->seq++&0xFFF)<<4); f[22]=(u8)sq; f[23]=(u8)(sq>>8); return 24; }
u32 rtw89_build_auth(u8* f,const struct rtw89_sta* s){ u32 n=s_hdr(f,0xB0,(struct rtw89_sta*)s); static const u8 b[6]={0,0,1,0,0,0}; s_cpy(f+n,b,6); return n+6; }
u32 rtw89_build_assoc(u8* f,u32 cap,const struct rtw89_sta* s,const char* ssid,u16 capab){
    u32 n=s_hdr(f,0x00,(struct rtw89_sta*)s); u16 c=(u16)(1u|(capab&0x0420u));
    f[n++]=(u8)c; f[n++]=(u8)(c>>8); f[n++]=10; f[n++]=0;
    u8 sl=0; while(ssid&&sl<32&&ssid[sl])sl++; f[n++]=0; f[n++]=sl; for(u8 i=0;i<sl;i++)f[n++]=(u8)ssid[i];
    static const u8 r5[]={1,8,0x8C,0x12,0x98,0x24,0xB0,0x48,0x60,0x6C};
    static const u8 r2[]={1,8,0x82,0x84,0x8B,0x96,0x0C,0x12,0x18,0x24,50,4,0x30,0x48,0x60,0x6C};
    if(s->ch>14){ s_cpy(f+n,r5,sizeof r5); n+=sizeof r5; } else { s_cpy(f+n,r2,sizeof r2); n+=sizeof r2; }
    if(cap>n)n+=(u32)wifi_hw_assoc_ie(f+n,(int)(cap-n));
    return n; }

int rtw89_sta_rx_mgmt(const u8* f,int len){
    struct rtw89_sta* s=&g_r89s; if(!f||len<24||(f[0]&0x0C)||s->state==R89S_IDLE)return 0;
    if(!s_eq(f+4,s->me)||!s_eq(f+10,s->bssid))return 0;
    u8 st=f[0]&0xF0; const u8* b=f+24; int bl=len-24;
    if(st==0xB0&&bl>=6&&b[0]==0&&b[1]==0&&b[2]==2){ s->auth_st=b[4]|(b[5]<<8); return 1; }
    if((st==0x10||st==0x30)&&bl>=6){ s->assoc_st=b[2]|(b[3]<<8); if(!s->assoc_st){ s->aid=(u16)((b[4]|(b[5]<<8))&0x3FFF); s->aid_ok=1; } return 1; }
    if(st==0xC0||st==0xA0){ int r=bl>=2?(b[0]|(b[1]<<8)):0; s->deauths++; if(s->state==R89S_AUTH&&s->auth_st<0)s->auth_st=0x10000|r; else if(s->state==R89S_ASSOC&&s->assoc_st<0)s->assoc_st=0x10000|r; return 1; }
    return 0; }

void rtw89_sta_rx(const u8* frame,int len,int rssi){ rtw89_sta_rx_mgmt(frame,len); wifi_hw_rx(frame,len,rssi); }

static u8 s_c2h[2048];
static void s_pump(struct rtw89_softc* sc){
    for(int i=0;i<8&&rtw89_hci&&rtw89_hci->c2h_rx;i++){ int n=rtw89_hci->c2h_rx(sc,s_c2h,sizeof s_c2h); if(n<=0)break; rtw89_c2h_handle(sc,s_c2h,(u32)n); } }
static int s_wait(struct rtw89_softc* sc,volatile int* st,u32 ms){ for(u32 t=0;t<ms&&*st<0;t++){ s_pump(sc); if(*st>=0)break; sc->io->delay(1000); } return *st; }

static int s_h2c(struct rtw89_softc* sc,u8 cls,u8 func,int rack,int dack,const u8* p,u32 n){
    int r=rtw89_h2c_send(sc,RTW89_H2C_CAT_MAC,cls,func,rack,dack,p,n); s_pump(sc); return r; }
static int s_cam(struct rtw89_softc* sc,u8 net,int valid){ u8 b[RTW89_ADDR_CAM_LEN]; rtw89_build_addr_cam(b,&g_r89s,net,valid); return s_h2c(sc,H2C_CL_MAC_ADDR_CAM_UPDATE,H2C_FUNC_MAC_ADDR_CAM_UPD,0,1,b,sizeof b); }
static int s_role(struct rtw89_softc* sc,u8 upd){ u8 b[4]; rtw89_build_role_maintain(b,g_r89s.macid,upd); return s_h2c(sc,H2C_CL_MAC_MEDIA_RPT,H2C_FUNC_MAC_FWROLE_MAINTAIN,0,1,b,4); }
static int s_join(struct rtw89_softc* sc,int disc){ u8 b[4]; rtw89_build_join_info(b,g_r89s.macid,disc,disc?RTW89_NET_TYPE_NO_LINK:RTW89_NET_TYPE_INFRA); return s_h2c(sc,H2C_CL_MAC_MEDIA_RPT,H2C_FUNC_MAC_JOININFO,0,1,b,4); }

static u8 s_mrate(void){ return g_r89s.ch>14?RTW89_HW_RATE_OFDM6:RTW89_HW_RATE_CCK1; }
static int s_mgmt(struct rtw89_softc* sc,const u8* f,u32 n){ return rtw89_tx_hook?rtw89_tx_hook(sc,f,n,1,s_mrate(),g_r89s.macid):-1; }

static void s_teardown(struct rtw89_softc* sc){
    struct rtw89_sta* s=&g_r89s; int up=s->state==R89S_UP; s->state=R89S_IDLE;
    if(up)s_join(sc,1);
    s_cam(sc,RTW89_NET_TYPE_NO_LINK,0); s_role(sc,RTW89_ROLE_REMOVE); s->vif=0;
    s->sec_valid=0; for(int i=0;i<7;i++){ s->sec_ent[i]=0; s->sec_keyid[i]=0; } s->aid=0; s->aid_ok=0; }

static int s_fail(struct rtw89_softc* sc,const char* why,int r){ g_r89s.why=why; klog(why); s_teardown(sc); return r; }

static int s_vif(struct rtw89_softc* sc){
    struct rtw89_sta* s=&g_r89s;
    if(!s->vif)rtw89_pci_post_init(sc);
    if(!s->vif&&s_role(sc,RTW89_ROLE_CREATE)<0)return -1;
    if(s_cam(sc,RTW89_NET_TYPE_NO_LINK,1)<0)return -1;
    if(!s->vif&&sc->chip==RTW89_CHIP_8852B){ u8 c[RTW89_CMC_TBL_LEN]; rtw89_build_cmac_default(c,s->macid); if(s_h2c(sc,H2C_CL_MAC_FR_EXCHG,H2C_FUNC_MAC_CCTLINFO_UD,0,1,c,sizeof c)<0)return -1; }
    s->vif=1; s->vif_inits++; return 0; }

int rtw89_sta_vif_init(struct rtw89_softc* sc){
    struct rtw89_sta* s=&g_r89s;
    if(!sc||!sc->io||sc->state!=RTW89_ST_READY||!rtw89_hci)return -1;
    if(s->state!=R89S_IDLE)return 0;
    if(s->vif)return 0;
    s_cpy(s->me,sc->mac,6); s_zero(s->bssid,6); s->macid=0; s->sec_mode=RTW89_ADDR_CAM_SEC_NONE; s->sec_valid=0;
    if(s_vif(sc)<0){ s_cam(sc,RTW89_NET_TYPE_NO_LINK,0); s_role(sc,RTW89_ROLE_REMOVE); s->vif=0; return -1; }
    return 0; }

int rtw89_sta_join(struct rtw89_softc* sc,const u8* bssid,u8 ch,const char* ssid,u8 akm,u8 ht){
    (void)akm; (void)ht; struct rtw89_sta* s=&g_r89s;
    if(!sc||!bssid||!sc->io)return -1;
    if(sc->state!=RTW89_ST_READY||!rtw89_hci){ s->why="rtw89: adapter not ready"; return -1; }
    if(!rtw89_chan_valid(ch)){ s->why="rtw89: channel not supported"; return 2; }
    if(s->state!=R89S_IDLE)rtw89_sta_leave(sc);
    if(!rtw89_chan_hook){ s->why="rtw89: channel switch not ported; cannot join"; klog(s->why); return -1; }
    if(rtw89_chan_hook(sc,ch)<0){ s->why="rtw89: channel switch failed"; klog(s->why); return -1; }
    s_cpy(s->bssid,bssid,6); s_cpy(s->me,sc->mac,6); s->ch=ch; s->macid=0; s->seq=0; s->aid=0; s->aid_ok=0; s->joins++;
    s->sec_mode=s->hwcrypt?RTW89_ADDR_CAM_SEC_NORMAL:RTW89_ADDR_CAM_SEC_NONE; s->sec_valid=0; for(int i=0;i<7;i++){ s->sec_ent[i]=0; s->sec_keyid[i]=0; }
    if(rtw89_rx_frame==wifi_hw_rx)rtw89_rx_frame=rtw89_sta_rx;
    s->state=R89S_AUTH;
    if(s_vif(sc)<0)return s_fail(sc,"rtw89: firmware did not take the station role",-1);
    static u8 f[512]; int st=-1;
    for(int t=0;t<3&&st<0;t++){ s->auth_st=-1; u32 n=rtw89_build_auth(f,s); if(s_mgmt(sc,f,n)<0)break; st=s_wait(sc,&s->auth_st,300); }
    if(st<0)return s_fail(sc,"rtw89: no authentication reply",1);
    if(st!=0)return s_fail(sc,"rtw89: authentication refused",2);
    s->state=R89S_ASSOC; st=-1;
    for(int t=0;t<3&&st<0;t++){ s->assoc_st=-1; u32 n=rtw89_build_assoc(f,sizeof f,s,ssid,0); if(s_mgmt(sc,f,n)<0)break; st=s_wait(sc,&s->assoc_st,500); }
    if(st<0)return s_fail(sc,"rtw89: no association reply",3);
    if(st!=0)return s_fail(sc,"rtw89: association refused",2);
    s->state=R89S_UP;
    if(s_join(sc,0)<0||s_cam(sc,RTW89_NET_TYPE_INFRA,1)<0)return s_fail(sc,"rtw89: firmware did not take the connection",-1);
    s->why=0; klog(s->hwcrypt?"rtw89: associated (fixed rate, hardware keys)":"rtw89: associated (fixed rate, software encryption)");
    return 0; }

void rtw89_sta_leave(struct rtw89_softc* sc){
    struct rtw89_sta* s=&g_r89s; if(!sc||s->state==R89S_IDLE)return;
    if(s->state==R89S_UP){ static u8 f[32]; u32 n=s_hdr(f,0xC0,s); f[n++]=3; f[n++]=0; s_mgmt(sc,f,n); }
    s_teardown(sc); }

int rtw89_sta_tx(struct rtw89_softc* sc,const u8* f,int len){
    if(!sc||!f||len<24||!rtw89_tx_hook)return -1;
    int mgmt=(f[0]&0x0C)==0;
    if(!mgmt&&g_r89s.state!=R89S_UP)return -1;
    return rtw89_tx_hook(sc,f,(u32)len,mgmt,mgmt?s_mrate():RTW89_HW_RATE_OFDM6,g_r89s.macid); }

int rtw89_sta_set_key(struct rtw89_softc* sc,int idx,int type,const u8* key,int len,int pairwise){
    struct rtw89_sta* s=&g_r89s; if(!sc||!key||len!=16||idx<0||idx>7)return -1;
    if(type!=WIFI_KEY_CCMP&&type!=WIFI_KEY_BIP)return -1;
    if(!s->hwcrypt)return 0;
    if(s->state!=R89S_UP)return -1;
    int slot=type==WIFI_KEY_BIP?5:pairwise?0:2+((idx+2)%3);
    u8 b[RTW89_SEC_CAM_H2C_LEN]; rtw89_build_sec_cam(b,(u8)slot,type==WIFI_KEY_BIP?RTW89_SEC_KEY_TYPE_BIP_CCMP128:RTW89_SEC_KEY_TYPE_CCMP128,key);
    if(s_h2c(sc,H2C_CL_MAC_SEC_CAM,H2C_FUNC_MAC_SEC_UPD,1,0,b,sizeof b)<0)return -1;
    s->sec_ent[slot]=(u8)slot; s->sec_keyid[slot]=(u8)(idx&3); s->sec_valid|=(u8)(1u<<slot);
    return s_cam(sc,RTW89_NET_TYPE_INFRA,1); }

void rtw89_sta_poll(struct rtw89_softc* sc){ if(sc)s_pump(sc); }
