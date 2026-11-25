/* SPDX-License-Identifier: BSD-3-Clause-Clear */
/*
 * Derived from Linux drivers/net/wireless/mediatek/mt76 (mt76_connac_mcu.c sta_tlv/sta_he_tlv/sta_ba, mt7921/mcu.c uni_tx_ba/uni_rx_ba).
 * Copyright (C) 2020 MediaTek Inc.
 * Licence text: drivers/net/wifi/mediatek/mt7921/LICENCE.BSD-3-Clause-Clear
 */
#include "drivers/net/wifi/mediatek/mt7921/mt7921.h"
#include "drivers/net/wifi/mediatek/mt7921/mcu/mcu.h"
#include "drivers/net/wifi/mediatek/mt7921/mcu/rate.h"
int mt_ra_on=1,mt_caps_on=1,mt_he_on=1,mt_ba_on=1,mt_batx_on=1;
int mt_caps_fail,mt_ra_fail,mt_ba_fail,mt_ba_port;
struct mt_peer_caps mt_peer;
struct mt_ba_st mt_ba[8];
u16 mt_tx_sn[8];
u32 mt_ba_rx_sess,mt_ba_tx_sess,mt_ba_tx_req;
static u8 mt_ba_tok;
static const u8 mt_r24[12]={2,4,11,22,12,18,24,36,48,72,96,108};
static int mt_rate_idx(u8 r,int g){ r&=0x7f; for(int i=g?0:4;i<12;i++)if(mt_r24[i]==r)return g?i:i-4; return -1; }
static u16 mt_mcs_clamp(u16 m){ return (u16)(m|0xfff0u); }
static void mt_he_cap_v1(const struct mt_peer_caps* c,u8* t){
    const u8* m=c->he_mac; const u8* p=c->he_phy; u32 cap=0;
    static const u8 map[][3]={{0,0x01,0},{2,0x08,2},{3,0x02,3},{4,0x40,4},{4,0x04,1},{6,0x60,18},{7,0x20,6},{7,0x40,13},{8,0x02,17},{8,0x04,9},{8,0x08,10},
        {12,0x20,8},{13,0x04,14},{13,0x40,11},{13,0x80,12},{14,0x01,16},{14,0x10,15},{15,0x02,7},{15,0x04,19},{15,0x08,20}};
    for(u32 i=0;i<sizeof map/sizeof map[0];i++){ u8 v=map[i][0]<6?m[map[i][0]]:p[map[i][0]-6]; if(v&map[i][1])cap|=1u<<map[i][2]; }
    mt_put32(t+4,cap); t[8]=(u8)((m[1]>>2)&3); t[9]=(u8)((m[3]>>3)&3); t[10]=(u8)((p[0]>>1)&0x7f); t[11]=(u8)((p[1]>>4)&1);
    t[12]=(u8)(p[3]&3); t[13]=(u8)((p[3]>>2)&1); t[14]=(u8)((p[3]>>3)&3); t[15]=(u8)((p[3]>>5)&1); t[16]=(u8)((p[8]>>6)&3); t[17]=(u8)(p[1]&0x0f); t[18]=2;
    mt_put16(t+20,c->he_mcs[0]); if(c->bw>=3){ mt_put16(t+22,c->he_mcs[1]); if(p[0]&0x10)mt_put16(t+24,c->he_mcs[2]); }
}
int mt_peer_parse(const u8* ie,int n,u8 ch,struct mt_peer_caps* c){
    mt_zero(c,sizeof *c); if(!ie||n<=0)return -1;
    int g=ch<=14; u8 vbw=0,hbw=0;
    for(int o=0;o+2<=n;){
        u8 id=ie[o],l=ie[o+1]; const u8* d=ie+o+2; if(o+2+l>n)break;
        if(id==1||id==50)for(int i=0;i<l;i++){ int k=mt_rate_idx(d[i],g); if(k>=0){ c->legacy|=(u16)(1u<<k); if(d[i]&0x80)c->basic|=(u16)(1u<<k); } }
        else if(id==45&&l>=26){ c->ht=1; c->ht_cap=mt_get16(d); c->ampdu=d[2]&0x1f; for(int i=0;i<10;i++)c->mcs[i]=i<2?d[3+i]:0; }
        else if(id==61&&l>=2&&(d[1]&4)&&(d[1]&3))hbw=1;
        else if(id==191&&l>=12){ c->vht=1; c->vht_cap=mt_get32(d); c->vht_rx=mt_mcs_clamp(mt_get16(d+4)); c->vht_tx=mt_mcs_clamp(mt_get16(d+8)); }
        else if(id==192&&l>=3&&d[0]>=1)vbw=(u8)(d[2]?3:2);
        else if(id==255&&l>=1+6+11+4&&d[0]==35){ c->he=1; mt_copy(c->he_mac,d+1,6); mt_copy(c->he_phy,d+7,11); int k=18; c->he_mcs[0]=mt_mcs_clamp(mt_get16(d+k)); k+=4;
            if((c->he_phy[0]&0x08)&&k+4<=l){ c->he_mcs[1]=mt_mcs_clamp(mt_get16(d+k)); k+=4; } if((c->he_phy[0]&0x10)&&k+4<=l)c->he_mcs[2]=mt_mcs_clamp(mt_get16(d+k)); }
        o+=2+l;
    }
    c->bw=c->vht&&vbw?vbw:hbw; if(c->bw>2)c->bw=2; if(g&&c->bw>1)c->bw=1;
    c->nss=(c->ht&&c->mcs[1])||(c->vht&&((c->vht_rx>>2)&3)!=3)?2:1;
    if(!c->ht){ c->vht=0; c->he=c->he&&!g?c->he:0; }
    if(!mt_he_on)c->he=0;
    c->ok=c->legacy!=0; return c->ok?0:-1;
}
static u8* mt_tlv(u8* out,u32* l,u16 tag,u16 len){ u8* t=out+*l; mt_zero(t,len); mt_put16(t,tag); mt_put16(t+2,len); *l+=len; mt_put16(out+2,(u16)(mt_get16(out+2)+1)); return t; }
u32 mt_sta_rate_build(struct mt7921_softc* sc,u8 wcid,const u8* peer,u16 aid,const struct mt_peer_caps* c,int rssi,u8* out,u32 cap){
    if(cap<MT_STA_RATE_MAX||!c||!c->ok)return 0;
    u32 l=mt_sta_req_build(sc,wcid,peer,CONN_STATE_CONNECT,aid,out); u8* t; int g=sc->chan<=14;
    if(c->ht){ t=mt_tlv(out,&l,STA_REC_HT,8); mt_put16(t+4,c->ht_cap); }
    if(c->vht){ t=mt_tlv(out,&l,STA_REC_VHT,16); mt_put32(t+4,c->vht_cap); mt_put16(t+8,c->vht_rx); mt_put16(t+10,c->vht_tx); }
    if(c->he){ t=mt_tlv(out,&l,STA_REC_HE,28); mt_he_cap_v1(c,t);
        t=mt_tlv(out,&l,STA_REC_HE_V2,28); mt_copy(t+4,c->he_mac,6); mt_copy(t+10,c->he_phy,11); t[21]=2; mt_put16(t+22,c->he_mcs[0]); if(c->bw>=3){ mt_put16(t+24,c->he_mcs[1]); if(c->he_phy[0]&0x10)mt_put16(t+26,c->he_mcs[2]); } }
    t=mt_tlv(out,&l,STA_REC_PHY,12);
    u8 pt=g?(u8)(MT_PHY_HR_DSSS|MT_PHY_ERP):MT_PHY_OFDM; if(c->ht)pt|=MT_PHY_HT; if(c->vht&&!g)pt|=MT_PHY_VHT; if(c->he)pt|=MT_PHY_HE;
    mt_put16(t+4,c->basic); t[6]=pt; t[7]=c->ampdu; int rc=rssi>-127?(rssi+110)*2:0; t[9]=(u8)(rc<0?0:rc>220?220:rc);
    t=mt_tlv(out,&l,STA_REC_RA,16);
    u16 lg=g?(u16)((((c->legacy>>4)&0xffu)<<6)|(c->legacy&0xfu)):(u16)((c->legacy&0xffu)<<6); mt_put16(t+4,lg);
    if(c->ht)mt_copy(t+6,c->mcs,10);
    t=mt_tlv(out,&l,STA_REC_STATE,12); t[8]=MT_STA_INFO_ASSOC; if(c->vht)t[9]=(u8)(c->bw|((c->nss-1)<<4));
    return l;
}
int mt_sta_rate_send(struct mt7921_softc* sc,u16 aid){
    static u8 b[MT_STA_RATE_MAX];
    if(!mt_ra_on||!mt_peer.ok)return -1;
    u32 l=mt_sta_rate_build(sc,MT_WCID_AP,sc->bssid,aid,&mt_peer,sc->rssi,b,sizeof b);
    if(!l)return -1;
    if(mt7921_mcu_send(sc,MCU_UNI_CMD(MCU_UNI_CMD_STA_REC_UPDATE),b,l,1)<0){ if(++mt_ra_fail>=3)mt_ra_on=0; return -1; }
    mt_ra_fail=0; return 0;
}
int mt_assoc_caps(struct mt7921_softc* sc,u8 ch,u8* o){
    int n=0,g=ch<=14&&!sc->band6;
    if(sc->band6){ if(!mt_he_on)return 0; o[n++]=255; o[n++]=22; o[n++]=35; static const u8 mac6[6]={0,0,0,0x10,0,0}; for(int i=0;i<6;i++)o[n++]=mac6[i];
        static const u8 phy6[11]={0x04,0x20,0,0,0,0,0,0,0,0x80,0}; for(int i=0;i<11;i++)o[n++]=phy6[i];
        o[n++]=0xfa; o[n++]=0xff; o[n++]=0xfa; o[n++]=0xff; o[n++]=255; o[n++]=3; o[n++]=59; o[n++]=0x38; o[n++]=0; return n; }
    static const u8 ht[26]={0xef,0x01,0x17,0xff,0xff,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0};
    o[n++]=45; o[n++]=26; for(int i=0;i<26;i++)o[n++]=ht[i]; if(g)o[2]&=(u8)~0x02;
    if(!g){ static const u8 vht[12]={0xb0,0x11,0x80,0x03,0xfa,0xff,0,0,0xfa,0xff,0,0}; o[n++]=191; o[n++]=12; for(int i=0;i<12;i++)o[n++]=vht[i]; }
    if(mt_he_on){ o[n++]=255; o[n++]=22; o[n++]=35; static const u8 mac[6]={0,0,0,0x10,0,0}; for(int i=0;i<6;i++)o[n++]=mac[i];
        u8 phy[11]={(u8)(g?0x02:0x04),0x20,0,0,0,0,0,0,0,0x80,0}; for(int i=0;i<11;i++)o[n++]=phy[i];
        o[n++]=0xfa; o[n++]=0xff; o[n++]=0xfa; o[n++]=0xff; }
    return n;
}
static u32 mt_ba_hdr(struct mt7921_softc* sc,u8* out){ mt_zero(out,8); out[0]=sc->bss_idx; out[1]=MT_WCID_AP; out[4]=1; out[5]=sc->omac_idx; return 8; }
u32 mt_ba_wtbl_build(struct mt7921_softc* sc,int tid,u16 ssn,u16 win,int en,int tx,u8* out){
    u32 l=mt_ba_hdr(sc,out); u8* t=mt_tlv(out,&l,STA_REC_WTBL,4+8+28);
    u8* h=t+4; h[0]=MT_WCID_AP; h[1]=WTBL_SET; mt_put16(h+2,1);
    u8* b=h+8; mt_put16(b,WTBL_BA); mt_put16(b+2,28); b[4]=(u8)tid;
    if(tx){ b[5]=MT_BA_TYPE_ORIGINATOR; mt_put16(b+8,en?ssn:0); b[10]=(u8)!!en; }
    else { b[5]=MT_BA_TYPE_RECIPIENT; mt_copy(b+14,sc->bssid,6); b[20]=(u8)tid; b[21]=RST_BA_MAC_TID_MATCH; b[22]=1; }
    mt_put16(b+12,en?win:0);
    return l;
}
u32 mt_ba_sta_build(struct mt7921_softc* sc,int tid,u16 ssn,u16 win,int en,int tx,int amsdu,u8* out){
    u32 l=mt_ba_hdr(sc,out); u8* t=mt_tlv(out,&l,STA_REC_BA,12);
    t[4]=(u8)tid; t[5]=tx?MT_BA_TYPE_ORIGINATOR:MT_BA_TYPE_RECIPIENT; t[6]=(u8)!!amsdu; t[7]=(u8)((en?1u:0u)<<tid); mt_put16(t+8,ssn); mt_put16(t+10,win);
    return l;
}
int mt_ba_in_rx; u32 mt_ba_deferred;
static struct { u16 ssn,win; u8 tid,en,tx,amsdu; } mt_baq[16]; static int mt_baqn,mt_ba_draining;
static int mt_ba_now(struct mt7921_softc* sc,int tid,u16 ssn,u16 win,int en,int tx,int amsdu);
int mt7921_ba(struct mt7921_softc* sc,int tid,u16 ssn,u16 win,int en,int tx,int amsdu){
    if(!sc||tid<0||tid>7||!mt_ba_on)return -1;
    if(!mt_ba_in_rx)return mt_ba_now(sc,tid,ssn,win,en,tx,amsdu);
    if(mt_baqn>=16)return -1;
    mt_baq[mt_baqn].tid=(u8)tid; mt_baq[mt_baqn].ssn=ssn; mt_baq[mt_baqn].win=win; mt_baq[mt_baqn].en=(u8)!!en; mt_baq[mt_baqn].tx=(u8)!!tx; mt_baq[mt_baqn].amsdu=(u8)!!amsdu; mt_baqn++; mt_ba_deferred++;
    return 0;
}
int mt_ba_pending(void){ return mt_baqn; }
void mt_ba_drain(struct mt7921_softc* sc){
    if(!sc||mt_ba_in_rx||mt_ba_draining)return;
    mt_ba_draining=1;
    for(int i=0;i<mt_baqn;i++){ int t=mt_baq[i].tid;
        if(mt_ba_on&&mt_ba_now(sc,t,mt_baq[i].ssn,mt_baq[i].win,mt_baq[i].en,mt_baq[i].tx,mt_baq[i].amsdu)==0)continue;
        if(!mt_baq[i].en)continue;
        if(mt_baq[i].tx&&mt_ba[t].tx_st==2){ mt_ba[t].tx_st=0; mt_ba[t].tx_fail++; if(mt_ba_tx_sess)mt_ba_tx_sess--; }
        if(!mt_baq[i].tx&&mt_ba[t].rx_on){ mt_ba[t].rx_on=0; if(mt_ba_rx_sess)mt_ba_rx_sess--; } }
    mt_baqn=0; mt_ba_draining=0;
}
static int mt_ba_now(struct mt7921_softc* sc,int tid,u16 ssn,u16 win,int en,int tx,int amsdu){
    u8 b[64];
    u32 l=mt_ba_wtbl_build(sc,tid,ssn,win,en,tx,b);
    if(mt7921_mcu_send(sc,MCU_UNI_CMD(MCU_UNI_CMD_STA_REC_UPDATE),b,l,1))goto fail;
    l=mt_ba_sta_build(sc,tid,ssn,win,en,tx,amsdu,b);
    if(mt7921_mcu_send(sc,MCU_UNI_CMD(MCU_UNI_CMD_STA_REC_UPDATE),b,l,1))goto fail;
    mt_ba_fail=0; return 0;
fail: if(en&&++mt_ba_fail>=3){ mt_ba_on=0; sc->err="block-ack setup failed 3 times, aggregation off"; } return -1;
}
static int mt_ba_mgmt(struct mt7921_softc* sc,const u8* body,int n){
    u8 f[24+32]; if(n>32)return -1;
    mt_zero(f,24); f[0]=0xD0; mt_copy(f+4,sc->bssid,6); mt_copy(f+10,sc->mac,6); mt_copy(f+16,sc->bssid,6); mt_copy(f+24,body,(u32)n);
    return mt7921_tx_data(sc,f,(u32)(24+n));
}
int mt_ba_tx_start(struct mt7921_softc* sc,int tid){
    struct mt_ba_st* s=&mt_ba[tid&7];
    if(!mt_ba_on||!mt_batx_on||!sc->connected||s->tx_st||s->tx_fail>=3)return -1;
    u16 prm=(u16)(2u|((u32)(tid&7)<<2)|(64u<<6)); u16 ssn=mt_tx_sn[tid&7];
    s->tok=++mt_ba_tok; if(!s->tok)s->tok=mt_ba_tok=1; s->tx_ssn=ssn;
    u8 a[9]={3,0,s->tok,(u8)prm,(u8)(prm>>8),0,0,(u8)(ssn<<4),(u8)(ssn>>4)};
    if(mt_ba_mgmt(sc,a,9)<0)return -1;
    s->tx_st=1; s->tx_t=mt_ms(); mt_ba_tx_req++; return 0;
}
void mt_ba_tx_stop(struct mt7921_softc* sc,int tid,int send){
    struct mt_ba_st* s=&mt_ba[tid&7]; int was=s->tx_st==2; s->tx_st=0;
    if(!was)return;
    if(mt_ba_tx_sess)mt_ba_tx_sess--;
    mt7921_ba(sc,tid&7,0,0,0,1,0);
    if(send&&sc->connected){ u16 p=(u16)((1u<<11)|((u32)(tid&7)<<12)); u8 a[6]={3,2,(u8)p,(u8)(p>>8),39,0}; mt_ba_mgmt(sc,a,6); }
}
void mt_ba_reset(struct mt7921_softc* sc,int send){
    if(!mt_ba_in_rx)mt_baqn=0;
    for(int t=0;t<8;t++){ mt_ba_tx_stop(sc,t,send); if(mt_ba[t].rx_on){ mt7921_ba(sc,t,0,0,0,0,0); mt_ba[t].rx_on=0; } mt_ba[t].tx_fail=0; mt_tx_sn[t]=0; }
    mt_ba_rx_sess=mt_ba_tx_sess=0; mt_ba_port=0;
}
void mt_ba_rx_action(struct mt7921_softc* sc,const u8* f,u32 fl){
    if(fl<24+3||(f[1]&0x40)||f[24]!=3)return;
    const u8* a=f+24; u32 n=fl-24;
    if(a[1]==0&&n>=9){ u16 p=mt_get16(a+3); int tid=(p>>2)&7; mt_ba[tid].rx_ssn=(u16)(mt_get16(a+7)>>4); mt_ba[tid].rx_tok=a[2]; }
    else if(a[1]==1&&n>=9){ u16 p=mt_get16(a+5); int tid=(p>>2)&7; struct mt_ba_st* s=&mt_ba[tid];
        if(s->tx_st!=1||a[2]!=s->tok)return;
        u16 win=(u16)(p>>6); if(!win||win>64)win=64;
        if(mt_get16(a+3)==0&&mt7921_ba(sc,tid,s->tx_ssn,win,1,1,p&1)==0){ s->tx_st=2; mt_ba_tx_sess++; }
        else { s->tx_st=0; s->tx_fail++; } }
    else if(a[1]==2&&n>=6){ u16 p=mt_get16(a+2); int tid=(p>>12)&7;
        if(p&0x0800){ if(mt_ba[tid].rx_on){ mt7921_ba(sc,tid,0,0,0,0,0); mt_ba[tid].rx_on=0; if(mt_ba_rx_sess)mt_ba_rx_sess--; } }
        else { if(mt_ba[tid].tx_st==1)mt_ba[tid].tx_st=0; mt_ba_tx_stop(sc,tid,0); } }
}
int mt_ba_tx_hook(struct mt7921_softc* sc,const u8* f,u32 len,u32* d3){
    if(len<24)return 0;
    u8 fc=f[0];
    if(fc==0xD0&&len>=24+9&&!(f[1]&0x40)&&f[24]==3&&f[25]==1){ const u8* a=f+24; u16 p=mt_get16(a+5); int tid=(p>>2)&7; u16 win=(u16)(p>>6);
        if(mt_get16(a+3)==0&&win&&!mt_ba[tid].rx_on){ if(mt7921_ba(sc,tid,mt_ba[tid].rx_ssn,win,1,0,p&1)==0){ mt_ba[tid].rx_on=1; mt_ba_rx_sess++; } } return 0; }
    if(fc==0xD0&&len>=24+6&&!(f[1]&0x40)&&f[24]==3&&f[25]==2){ u16 p=mt_get16(f+26); int tid=(p>>12)&7; if(!(p&0x0800)&&mt_ba[tid].rx_on){ mt7921_ba(sc,tid,0,0,0,0,0); mt_ba[tid].rx_on=0; if(mt_ba_rx_sess)mt_ba_rx_sess--; } return 0; }
    if((fc&0x8C)!=0x88||(f[4]&1)||!mt_batx_on||!mt_ba_on)return 0;
    int hl=24+((f[1]&3)==3?6:0); if((int)len<hl+2)return 0;
    int tid=f[hl]&7; struct mt_ba_st* s=&mt_ba[tid];
    u16 sn=mt_tx_sn[tid]; mt_tx_sn[tid]=(u16)((sn+1)&0xfff);
    if(d3)*d3=(1u<<31)|((u32)sn<<16);
    int eapol=!(f[1]&0x40)&&(int)len>=hl+2+8&&f[hl+8]==0x88&&f[hl+9]==0x8E;
    if(!s->tx_st&&sc->connected&&mt_ba_port&&!eapol&&s->tx_fail<3)mt_ba_tx_start(sc,tid);
    else if(s->tx_st==1&&mt_ms()-s->tx_t>1000){ s->tx_st=0; s->tx_fail++; }
    return tid+1;
}
static int mt_w(const char* a,const char* w){ int i=0; while(w[i]&&a[i]==w[i])i++; return !w[i]&&(!a[i]||a[i]==' '); }
static char* mt_s(char* p,char* e,const char* s){ while(*s&&p<e)*p++=*s++; *p=0; return p; }
static char* mt_u(char* p,char* e,u32 v){ char t[12]; int n=0; do{ t[n++]=(char)('0'+v%10); v/=10; }while(v); while(n&&p<e)*p++=t[--n]; *p=0; return p; }
void mt_rate_cmd(const char* a,char* out,int cap){
    static const char* nm[5]={"ra","ht","he","ba","batx"}; int* fl[5]={&mt_ra_on,&mt_caps_on,&mt_he_on,&mt_ba_on,&mt_batx_on};
    char* p=out; char* e=out+cap-1; if(cap<=0)return; *p=0;
    for(int i=0;i<5;i++)if(mt_w(a,nm[i])){ const char* v=a; while(*v&&*v!=' ')v++; while(*v==' ')v++;
        if(mt_w(v,"on")){ *fl[i]=1; if(i==0)mt_ra_fail=0; if(i==1)mt_caps_fail=0; if(i>=3){ mt_ba_fail=0; for(int t=0;t<8;t++)mt_ba[t].tx_fail=0; } }
        else if(mt_w(v,"off"))*fl[i]=0;
        else { mt_s(p,e,"mtwifi ra|ht|he|ba|batx on|off\n"); return; } break; }
    p=mt_s(p,e,"mt7921 rate: firmware RA "); p=mt_s(p,e,mt_ra_on?"on":"off"); p=mt_s(p,e,", HT/VHT caps in assoc "); p=mt_s(p,e,mt_caps_on?"on":"off");
    p=mt_s(p,e,", HE "); p=mt_s(p,e,mt_he_on?"on":"off"); p=mt_s(p,e,", block-ack "); p=mt_s(p,e,mt_ba_on?"on":"off"); p=mt_s(p,e," (TX "); p=mt_s(p,e,mt_batx_on?"on":"off"); p=mt_s(p,e,")\n");
    if(mt_peer.ok){ p=mt_s(p,e,"  peer: "); p=mt_s(p,e,mt_peer.he?"HE":mt_peer.vht?"VHT":mt_peer.ht?"HT":"legacy"); p=mt_s(p,e,", "); p=mt_u(p,e,20u<<mt_peer.bw); p=mt_s(p,e," MHz, "); p=mt_u(p,e,mt_peer.nss); p=mt_s(p,e," streams\n"); }
    p=mt_s(p,e,"  BA sessions: RX "); p=mt_u(p,e,mt_ba_rx_sess); p=mt_s(p,e,", TX "); p=mt_u(p,e,mt_ba_tx_sess); p=mt_s(p,e,", ADDBA sent "); p=mt_u(p,e,mt_ba_tx_req); p=mt_s(p,e,"\n");
}
