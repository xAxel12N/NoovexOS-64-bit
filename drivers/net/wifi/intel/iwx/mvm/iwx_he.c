/* SPDX-License-Identifier: GPL-3.0-or-later. Firmware API layouts from Linux iwlwifi fw/api (GPL-2.0 OR BSD-3-Clause, Copyright (C) 2012-2014, 2018-2026 Intel Corporation), used under BSD-3-Clause; ADD_STA BA fields per OpenBSD if_iwx.c (ISC). See NOTICE. */
#include "drivers/net/wifi/intel/iwx/mvm/iwx_he.h"
#include "include/nvx/core/klog.h"
#include "include/nvx/net/wifi/wifi_ampdu.h"
extern int iwx_cmd_version(struct iwx_softc* sc,u8 grp,u8 cmd) __attribute__((weak));
volatile int iwx_he_on=1,iwx_he_fail,iwx_he_up,iwx_ba_on=1;
struct iwx_hepeer iwx_he_cur;
static volatile int iwx_he_pend,iwx_ba_sta,iwx_ba_err;
static struct { u8 tid,start; u16 ssn,win; } iwx_baq[8]; static volatile u32 iwx_baq_h,iwx_baq_t;
static u32 iwx_he_sku; static u64 iwx_he_160m;
static struct { u8 bssid[6]; struct iwx_hepeer h; } iwx_he_tab[IWX_MAX_SCAN_BSS];
static int iwx_he_n;
static struct iwx_hepeer iwx_he_asr; static int iwx_he_asr_ok;
static void iwx_he_z(void* d,u32 n){ u8* a=(u8*)d; while(n--)*a++=0; }
void iwx_he_parse(const u8* ie,u32 len,struct iwx_hepeer* h){
    iwx_he_z(h,sizeof *h); h->rts=1023; h->mcs80=h->mcs160=0xFFFF;
    for(u32 o=0;o+2<=len;){
        u8 id=ie[o],l=ie[o+1]; const u8* v=ie+o+2;
        if(o+2+l>len)break;
        if(id==255&&l>=1+6+11+4&&v[0]==35){
            const u8* ph=v+7; h->he=1; h->ldpc=!!(ph[1]&0x20); h->mcs80=(u16)(v[18]|(v[19]<<8));
            if((ph[0]&0x08)&&l>=1+6+11+8){ h->c160=1; h->mcs160=(u16)(v[22]|(v[23]<<8)); }
        } else if(id==255&&l>=1+6&&v[0]==36){
            u32 op=(u32)v[1]|((u32)v[2]<<8)|((u32)v[3]<<16); h->rts=(u16)((op>>4)&0x3FF);
            h->color=v[4]&0x3F; h->cdis=!!(v[4]&0x80);
        }
        o+=2u+l;
    }
}
void iwx_he_scan_note(const u8* bssid,const u8* ie,u32 len){
    int i; for(i=0;i<iwx_he_n;i++){ int s=1; for(int k=0;k<6;k++)if(iwx_he_tab[i].bssid[k]!=bssid[k])s=0; if(s)break; }
    if(i==iwx_he_n){ if(iwx_he_n>=IWX_MAX_SCAN_BSS)return; iwx_he_n++; }
    for(int k=0;k<6;k++)iwx_he_tab[i].bssid[k]=bssid[k];
    iwx_he_parse(ie,len,&iwx_he_tab[i].h);
}
int iwx_he_scan_peer(const u8* bssid,struct iwx_hepeer* h){
    for(int i=0;i<iwx_he_n;i++){ int s=1; for(int k=0;k<6;k++)if(iwx_he_tab[i].bssid[k]!=bssid[k])s=0; if(s){ *h=iwx_he_tab[i].h; return 0; } }
    iwx_he_z(h,sizeof *h); return -1;
}
void iwx_he_assoc_note(const u8* ie,u32 len){ iwx_he_parse(ie,len,&iwx_he_asr); iwx_he_asr_ok=1; }
void iwx_he_nvm_note(u32 sku,int ch,u32 flags){
    iwx_he_sku=sku;
    if(ch>=36&&ch<=177&&(flags&IWX_NVM_CH_160))iwx_he_160m|=1ull<<((ch-36)/4);
}
int iwx_he_ch160(int ch){ return ch>=36&&ch<=177&&((iwx_he_160m>>((ch-36)/4))&1); }
u32 iwx_he_cap_ie(u8* f,int ch,int mimo,int c160){
    u32 n=0; u16 mm=mimo?0xFFFA:0xFFFE;
    f[n++]=255; f[n++]=(u8)(1+6+11+(c160?8:4)); f[n++]=35;
    for(int i=0;i<6;i++)f[n++]=0;
    u8* ph=f+n; for(int i=0;i<11;i++)f[n++]=0;
    if(ch>14)ph[0]=(u8)(0x04|(c160?0x08:0));
    ph[1]=0x20; ph[9]=0x80;
    for(int k=0;k<(c160?4:2);k++){ f[n++]=(u8)mm; f[n++]=(u8)(mm>>8); }
    return n;
}
u32 iwx_he_insert(u8* b,u32 n,u32 htl,u32 cap,int ch,int mimo,int c160){
    u8 t[32]; u32 hl=iwx_he_cap_ie(t,ch,mimo,c160);
    if(n+hl>cap||htl<9)return n;
    for(u32 i=0;i<9;i++)b[n+hl-1-i]=b[n-1-i];
    for(u32 i=0;i<hl;i++)b[n-9+i]=t[i];
    iwx_he_pend=1;
    return n+hl;
}
static u8 iwx_he_pos(int d){
    static const signed char dv[8]={-2,-6,-10,-14,2,6,10,14};
    for(int i=0;i<8;i++)if(dv[i]==d)return (u8)i;
    return 0xFF;
}
int iwx_he_link160(const struct iwx_hepeer* h,const struct iwx_peer* p,int ch,struct iwx_link* l){
    if(!h->he||!h->c160||!l->vht||l->width!=2||!p->vop)return 0;
    int c=0;
    if(p->vw==1&&p->vc1&&(p->vc1-p->vc0==8||p->vc0-p->vc1==8))c=p->vc1;
    else if(p->vw==2)c=p->vc0;
    if(!c)return 0;
    u8 pos=iwx_he_pos(ch-c); if(pos==0xFF)return 0;
    l->width=3; l->ctrl_pos=pos; l->sta_flags=(l->sta_flags&~(3u<<26))|(3u<<26);
    return 1;
}
u16 iwx_he_rates(u16 m,int nss){
    u32 v=(m>>(2*(nss-1)))&3;
    return v==0?0xFF:v==1?0x3FF:v==2?0xFFF:0;
}
u32 iwx_he_tlc_build(const struct iwx_peer* p,const struct iwx_hepeer* h,const struct iwx_link* l,u8 txant,int ver,void* out){
    u32 n=iwx_tlc_build(p,l,txant,ver,out);
    if(!h)h=&iwx_he_cur;
    if(!h->he||!l->ht)return n;
    u16 r80a=iwx_he_rates(h->mcs80,1), r80b=l->mimo?iwx_he_rates(h->mcs80,2):0;
    u16 r16a=l->width==3?iwx_he_rates(h->mcs160,1):0, r16b=l->width==3&&l->mimo?iwx_he_rates(h->mcs160,2):0;
    if(!r80a)return n;
    if(!h->ldpc){ r80a&=0x3FF; r80b&=0x3FF; r16a&=0x3FF; r16b&=0x3FF; }
    if(ver==4){
        struct iwx_tlc_config_cmd_v4* c=(struct iwx_tlc_config_cmd_v4*)out;
        c->mode=IWX_TLC_MODE_HE; c->ht_rates[0][0]=r80a; c->ht_rates[1][0]=r80b; c->ht_rates[0][1]=r16a; c->ht_rates[1][1]=r16b;
        if(h->ldpc)c->flags|=IWX_TLC_CFG_LDPC;
    } else {
        struct iwx_tlc_config_cmd_v3* c=(struct iwx_tlc_config_cmd_v3*)out;
        c->mode=IWX_TLC_MODE_HE; c->ht_rates[0][0]=r80a; c->ht_rates[1][0]=r80b; c->ht_rates[0][1]=r16a; c->ht_rates[1][1]=r16b;
        if(h->ldpc)c->flags|=IWX_TLC_CFG_LDPC;
    }
    return n;
}
static void iwx_he_w32(u8* p,u32 v){ p[0]=(u8)v; p[1]=(u8)(v>>8); p[2]=(u8)(v>>16); p[3]=(u8)(v>>24); }
u32 iwx_he_ctxt_build(const struct iwx_hepeer* h,int ver,u8* o){
    u32 pe=ver>=3?20:16, end=24+pe+40+(ver>=2?8:0);
    iwx_he_z(o,end);
    o[0]=0; o[1]=8;
    u32 fl=IWX_STA_CTXT_HE_NIC_NOT_ACK_ENABLED; if(h->cdis)fl|=IWX_STA_CTXT_HE_BSS_COLOR_DIS;
    iwx_he_w32(o+4,fl);
    for(u32 i=0;i<pe;i++)o[24+i]=IWX_HE_PKT_EXT_NONE;
    u8* q=o+24+pe; q[0]=h->color; q[2]=(u8)h->rts; q[3]=(u8)(h->rts>>8);
    return end;
}
int iwx_he_allowed(struct iwx_softc* sc){
    if(!iwx_he_on||iwx_he_fail>=2||!(iwx_he_sku&IWX_NVM_SKU_11AX))return 0;
    int (*volatile vf)(struct iwx_softc*,u8,u8)=iwx_cmd_version; int v=vf?vf(sc,IWX_DATA_PATH_GROUP,IWX_STA_HE_CTXT_CMD):0;
    return v==0||v==99||(v>=1&&v<=3);
}
int iwx_he_prepare(struct iwx_softc* sc,const u8* bssid,int ch,int htok,struct iwx_hepeer* h){
    if(iwx_he_pend){ iwx_he_pend=0; if(++iwx_he_fail>=2)klog("iwx: HE association failed twice; HE off for this session (wifi he on to retry)"); }
    iwx_he_reset(); iwx_he_asr_ok=0;
    iwx_he_scan_peer(bssid,h);
    if(!htok||!h->he||!iwx_he_allowed(sc)){ h->he=0; return 0; }
    h->c160=h->c160&&ch>14&&iwx_he_ch160(ch);
    return 1;
}
int iwx_he_assoc_done(struct iwx_softc* sc,struct iwx_hepeer* h,int sent,int ch,const struct iwx_peer* ap,struct iwx_link* l){
    (void)sc; (void)ch; iwx_he_pend=0; iwx_ba_sta=1; iwx_ba_err=0;
    if(!sent){ iwx_he_up=0; return 0; }
    if(iwx_he_asr_ok&&iwx_he_asr.he){
        const struct iwx_hepeer* a=&iwx_he_asr; h->mcs80=a->mcs80; h->ldpc=a->ldpc; h->c160=h->c160&&a->c160; if(a->c160)h->mcs160=a->mcs160;
        if(a->color||a->cdis||a->rts!=1023){ h->color=a->color; h->cdis=a->cdis; h->rts=a->rts; }
    } else if(iwx_he_asr_ok){ iwx_he_up=0; return 0; }
    iwx_he_link160(h,ap,ch,l);
    iwx_he_cur=*h; iwx_he_up=1;
    return 1;
}
int iwx_he_after_assoc(struct iwx_softc* sc,const struct iwx_peer* ap,const struct iwx_link* l){
    (void)ap; (void)l;
    if(!iwx_he_up)return 0;
    int v=({ int (*volatile vf_)(struct iwx_softc*,u8,u8)=iwx_cmd_version; vf_?vf_(sc,IWX_DATA_PATH_GROUP,IWX_STA_HE_CTXT_CMD):0; }); if(v<1||v>3)v=1;
    u8 c[96]; u32 n=iwx_he_ctxt_build(&iwx_he_cur,v,c);
    if(iwx_send_cmd_sync(sc,IWX_WIDE_ID(IWX_DATA_PATH_GROUP,IWX_STA_HE_CTXT_CMD),c,n,0,0,0,1000)<0){
        klog("iwx: firmware refused the HE station context; using VHT/HT rates"); iwx_he_up=0; iwx_he_fail++; return -1;
    }
    return 1;
}
void iwx_he_reset(void){ iwx_he_up=0; iwx_ba_sta=0; iwx_baq_h=iwx_baq_t; }
struct __attribute__((packed)) iwx_ba_sta_cmd {
    u8 add_modify,awake_acs; u16 tid_disable_tx; u32 mac_id_n_color; u8 addr[6]; u16 reserved2; u8 sta_id,modify_mask; u16 reserved3;
    u32 station_flags,station_flags_msk; u8 add_immediate_ba_tid,remove_immediate_ba_tid; u16 add_immediate_ba_ssn,sleep_tx_count;
    u8 sleep_state_flags,station_type; u16 assoc_id,beamform_flags; u32 tfd_queue_msk; u16 rx_ba_window; u8 sp_length,uapsd_acs;
};
_Static_assert(sizeof(struct iwx_ba_sta_cmd)==48,"ba sta");
extern volatile int iwx_in_rx;
extern void wnd_delba_rx(int tid) __attribute__((weak));
volatile u32 iwx_baq_drop,iwx_baq_sent;
static int iwx_ba_rx_now(struct iwx_softc* sc,int tid,u16 ssn,u16 win,int start);
int iwx_ba_rx(int tid,u16 ssn,u16 win,int start){
    struct iwx_softc* sc=iwx_get_sc();
    if(!sc||!iwx_ba_sta)return 1;
    if(start&&!iwx_ba_on)return -1;
    if(tid<0||tid>7)return -1;
    if(iwx_in_rx){ if(iwx_baq_t-iwx_baq_h>=8){ iwx_baq_drop++; return -1; } u32 i=iwx_baq_t&7; iwx_baq[i].tid=(u8)tid; iwx_baq[i].start=(u8)start; iwx_baq[i].ssn=ssn; iwx_baq[i].win=win; iwx_baq_t++; return 0; }
    return iwx_ba_rx_now(sc,tid,ssn,win,start);
}
void iwx_batx_poll(struct iwx_softc* sc);
void iwx_ba_drain(struct iwx_softc* sc){
    if(!sc||iwx_in_rx)return;
    iwx_batx_poll(sc);
    for(int k=0;k<8&&iwx_baq_h!=iwx_baq_t;k++){ u32 i=iwx_baq_h&7; iwx_baq_h++; iwx_baq_sent++;
        if(!iwx_ba_sta)continue;
        if(iwx_ba_rx_now(sc,iwx_baq[i].tid,iwx_baq[i].ssn,iwx_baq[i].win,iwx_baq[i].start)<0&&iwx_baq[i].start){ ampdu_delba(iwx_baq[i].tid); { void (*volatile dr)(int)=wnd_delba_rx; if(dr)dr(iwx_baq[i].tid); } } }
}
static int iwx_ba_rx_now(struct iwx_softc* sc,int tid,u16 ssn,u16 win,int start){
    if(start&&!iwx_ba_on)return -1;
    struct iwx_ba_sta_cmd c; iwx_he_z(&c,sizeof c); c.add_modify=1; c.sta_id=0;
    if(start){ c.add_immediate_ba_tid=(u8)tid; c.add_immediate_ba_ssn=(u16)(ssn&0xFFF); c.rx_ba_window=win>64?64:win; c.modify_mask=(u8)IWX_STA_MODIFY_ADD_BA_TID; }
    else { c.remove_immediate_ba_tid=(u8)tid; c.modify_mask=(u8)IWX_STA_MODIFY_REMOVE_BA_TID; }
    u32 st=0; int r=iwx_send_cmd_status(sc,0x18,&c,sizeof c,&st);
    if(r<0||(st&0xFF)!=1){
        if(start&&++iwx_ba_err>=3){ iwx_ba_on=0; klog("iwx: firmware refused RX block-ack 3 times; A-MPDU RX off (wifi ba on to retry)"); }
        return -1;
    }
    if(start)iwx_ba_err=0;
    return 0;
}
extern int iwx_txq_tid(struct iwx_softc* sc,int qid,int tid,int add) __attribute__((weak));
extern int iwx_txq_info(struct iwx_softc* sc,int qid,u64* desc,u64* bc) __attribute__((weak));
extern int ampdu_tx_active(void) __attribute__((weak));
extern void wnd_delba_tx(void) __attribute__((weak));
extern void* wifi_hw_ctx(void) __attribute__((weak));
volatile int iwx_batx_on=1,iwx_batx_q,iwx_batx_err;
volatile u32 iwx_batx_full,iwx_batx_frames,iwx_batx_notifs;
static volatile int iwx_batx_used,iwx_batx_kill; static volatile u32 iwx_batx_rec;
static int iwx_batx_qcmd(struct iwx_softc* sc,int q,int add){ int (*volatile f)(struct iwx_softc*,int,int,int)=iwx_txq_tid; return f?f(sc,q,0,add):-1; }
static void iwx_batx_off(struct iwx_softc* sc){ if(iwx_batx_q){ iwx_batx_qcmd(sc,iwx_batx_q,0); iwx_batx_q=0; } iwx_batx_used=0; }
int iwx_ba_tx(int tid,u16 ssn,u16 win,int start){
    (void)ssn; struct iwx_softc* sc=iwx_get_sc();
    if(!sc||!iwx_ba_sta||tid!=0||iwx_in_rx)return 0;
    { void* (*volatile hc)(void)=wifi_hw_ctx; if(hc&&hc()!=(void*)sc)return 0; }
    if(!start){ iwx_batx_off(sc); return 0; }
    if(!iwx_batx_on)return 0;
    u16 w=win>IWX_BATX_WIN?IWX_BATX_WIN:win; if(!w)w=IWX_BATX_WIN;
    if(iwx_batx_q&&!iwx_batx_used)return w;
    iwx_batx_off(sc);
    if(iwx_batx_qcmd(sc,2,1)<0){ if(++iwx_batx_err>=3){ iwx_batx_on=0; klog("iwx: firmware refused the A-MPDU TX queue 3 times; A-MPDU TX off (wifi batx on to retry)"); } return -1; }
    iwx_batx_q=2; iwx_batx_rec=0; iwx_batx_err=0; iwx_batx_kill=0; return w;
}
int iwx_batx_route(struct iwx_softc* sc,const u8* f,u32 len){
    int (*volatile act)(void)=ampdu_tx_active; int (*volatile qi)(struct iwx_softc*,int,u64*,u64*)=iwx_txq_info;
    if(!iwx_batx_q||!iwx_batx_on||iwx_batx_kill||len<26||(f[0]&0xFC)!=0x88||(f[4]&1)||(f[24]&15)||!act||!act()||!qi)return 1;
    u64 a,b; int cur=qi(sc,iwx_batx_q,&a,&b); if(cur<0)return 1;
    if((((u32)cur-iwx_batx_rec)&0xFFu)>=IWX_BATX_INFLIGHT){ iwx_batx_full++;
        if(!iwx_batx_notifs){ iwx_batx_kill=1; klog("iwx: no block-ack reports from firmware; A-MPDU TX off (wifi batx on to retry)"); return 1; } return -1; }
    iwx_batx_used=1; iwx_batx_frames++; return iwx_batx_q;
}
void iwx_batx_notif(const u8* d,u32 len){
    if(len<32||d[4]!=0)return; u32 tc=(u32)(d[28]|(d[29]<<8)); if(!tc||32+8*tc>len)return;
    iwx_batx_notifs++;
    for(u32 i=0;i<tc;i++){ const u8* t=d+32+8*i; u32 q=(u32)(t[0]|(t[1]<<8)); if(t[5]==0&&iwx_batx_q&&q==(u32)iwx_batx_q)iwx_batx_rec=(u32)(t[2]|(t[3]<<8)); }
}
void iwx_batx_poll(struct iwx_softc* sc){
    if(!iwx_batx_kill||iwx_in_rx)return; iwx_batx_kill=0; { void (*volatile dt)(void)=wnd_delba_tx; if(dt)dt(); } iwx_batx_off(sc); iwx_batx_on=0;
}
void iwx_batx_teardown(struct iwx_softc* sc){ iwx_batx_off(sc); iwx_batx_kill=0; iwx_batx_rec=0; }
