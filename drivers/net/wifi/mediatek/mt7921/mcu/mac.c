/* SPDX-License-Identifier: BSD-3-Clause-Clear */
/*
 * Derived from Linux drivers/net/wireless/mediatek/mt76 (mt76_connac_mac.c, mt7921/mac.c).
 * Copyright (C) 2020 MediaTek Inc.
 * Licence text: drivers/net/wifi/mediatek/mt7921/LICENCE.BSD-3-Clause-Clear
 */
#include "drivers/net/wifi/mediatek/mt7921/mt7921.h"
#include "drivers/net/wifi/mediatek/mt7921/mcu/mcu.h"
#include "drivers/net/wifi/mediatek/mt7921/mcu/rate.h"
extern volatile int mt_join_auth,mt_join_assoc;
extern u16 mt_join_aid;
static u8 mt_txbuf[MT_TXBUF_SIZE];
u32 mt_txd_build(struct mt7921_softc* sc,const u8* f,u32 len,u8* out){
    if(len<24||len+MT_TXD_SIZE+MT_TXP_SIZE>MT_TXBUF_SIZE)return 0;
    u8 fc=f[0],type=(fc>>2)&3,stype=fc>>4;
    int mgmt=type==0,mc=f[4]&1;
    int hl=24; if(type==2&&(stype&8))hl+=2; if((f[1]&3)==3)hl+=6;
    u8 q=mgmt?MT_LMAC_ALTX0:1;
    u8 wcid=mc?MT_WCID_BMC:MT_WCID_AP;
    mt_zero(out,MT_TXD_SIZE);
    mt_put32(out,((len+MT_TXD_SIZE)&0xffffu)|((u32)MT_TX_TYPE_CT<<23)|((u32)q<<25));
    mt_put32(out+4,MT_TXD1_LONG_FORMAT|wcid|((u32)sc->omac_idx<<24)|((u32)MT_HDR_FORMAT_802_11<<16)|((u32)(hl/2)<<11));
    u32 d2=((u32)type<<4)|stype|((u32)mc<<10);
    if(mgmt||mc)d2|=1u<<31;
    mt_put32(out+8,d2);
    mt_put32(out+12,(15u<<11)|(mc?1u:0)|((mgmt||mc)?(1u<<28):0));
    if(mgmt||mc)mt_put32(out+24,(1u<<2)|((u32)(sc->chan>14?((1u<<6)|11u):0u)<<16));
    mt_put32(out+28,((u32)type<<20)|((u32)stype<<16));
    mt_copy(out+MT_TXD_SIZE,f,len);
    return len+MT_TXD_SIZE;
}
int mt7921_tx_data(struct mt7921_softc* sc,const u8* frame,u32 len){
    if(!sc||sc->state<MT_ST_READY||!frame)return -1;
    u32 d3=0; int tid=sc->txd_build?0:mt_ba_tx_hook(sc,frame,len,&d3);
    u32 n=sc->txd_build?sc->txd_build(sc,frame,len,mt_txbuf):mt_txd_build(sc,frame,len,mt_txbuf);
    if(!n)return -1;
    if(tid){ mt_put32(mt_txbuf+4,mt_get32(mt_txbuf+4)|((u32)(tid-1)<<20)); mt_put32(mt_txbuf+12,mt_get32(mt_txbuf+12)|d3); }
    return mt7921_dma_tx(sc,MT_Q_TX,mt_txbuf,n)<0?-1:0;
}
int mt_rxd_parse(const u8* buf,u32 len,u32* off,u32* flen,int* rssi,u8* ch){
    if(len<24)return -1;
    u32 d0=mt_get32(buf),d1=mt_get32(buf+4),d2=mt_get32(buf+8),d3=mt_get32(buf+12);
    u32 pl=d0&0xffffu; if(pl&&pl<len)len=pl;
    if(d1&((1u<<27)|(1u<<25)|(1u<<26)))return -1;
    if(d2&((1u<<24)|(1u<<23)))return -1;
    if(d2&(1u<<13))return -1;
    *ch=(u8)((d3>>8)&0xff); *rssi=-127;
    u32 o=24;
    if(d1&(1u<<14))o+=16;
    if(d1&(1u<<11))o+=16;
    if(d1&(1u<<12))o+=8;
    if(d1&(1u<<13)){
        if(o+8>len)return -1;
        u32 v1=mt_get32(buf+o+4);
        int r=(int)(v1&0xff); if(r)*rssi=(r-220)/2;
        o+=8;
        if(d1&(1u<<15)){ o+=24; if(o>len)return -1; u32 g5=mt_get32(buf+o-24); int r5=(int)(g5&0xff); if(r5)*rssi=(r5-220)/2; o+=48; }
    }
    o+=2*((d2>>14)&3);
    if(o>=len||len-o<10)return -1;
    *off=o; *flen=len-o;
    return 0;
}
extern void mt7925_assoc_ies(const u8* ies,int n) __attribute__((weak));
void mt_sae_rx(const u8* f,u32 len);
void mt7921_tx_free_event(struct mt7921_softc* sc,const u8* buf,u32 len){
    if(!sc||!buf||len<8)return;
    u32 d0=mt_get32(buf),pl=d0&0xffffu; if(pl&&pl<len)len=pl;
    u32 cnt=(d0>>16)&0x3ffu; sc->txfree_events++;
    for(u32 i=0,o=8;i<cnt&&o+4<=len;i++,o+=4){
        u32 info=mt_get32(buf+o);
        if(info&(1u<<31)){ cnt++; continue; }
        mt7921_tx_token_release(sc,(info>>16)&0x7fffu);
    }
}
static int mt_eq6(const u8* a,const u8* b){ for(int i=0;i<6;i++)if(a[i]!=b[i])return 0; return 1; }
static void mt_rx_data_in(struct mt7921_softc* sc,const u8* buf,u32 len){
    if(!sc||!buf||len<8)return;
    u32 d0=mt_get32(buf),type=d0>>27,flag=(d0>>16)&0xf;
    if(type==PKT_TYPE_TXRX_NOTIFY){ mt7921_tx_free_event(sc,buf,len); return; }
    if(len<24)return;
    if(type==PKT_TYPE_RX_EVENT&&flag!=1){ mt7921_mcu_rx_event(sc,buf,len); return; }
    if(type!=PKT_TYPE_NORMAL&&type!=PKT_TYPE_RX_EVENT)return;
    u32 o,fl; int rssi; u8 ch;
    if((sc->rxd_parse?sc->rxd_parse:mt_rxd_parse)(buf,len,&o,&fl,&rssi,&ch)<0)return;
    const u8* f=buf+o;
    if(fl<24)return;
    u8 fc=f[0];
    if((fc&0x0C)==0&&mt_eq6(f+16,sc->bssid)&&mt_eq6(f+4,sc->mac)){
        if(fc==0xB0&&fl>=30&&mt_get16(f+24)==3)mt_sae_rx(f,fl);
        else if(fc==0xB0&&fl>=30&&mt_join_auth<0){ if(mt_get16(f+24)==0&&mt_get16(f+26)==2)mt_join_auth=mt_get16(f+28); }
        else if((fc==0x10||fc==0x30)&&fl>=30&&mt_join_assoc<0){ mt_join_assoc=mt_get16(f+26); mt_join_aid=(u16)(mt_get16(f+28)&0x3fff); if(!mt_join_assoc){ mt_peer_parse(f+30,(int)fl-30,sc->chan,&mt_peer); if(sc->gen==3){ void (*volatile ai)(const u8*,int)=mt7925_assoc_ies; if(ai)ai(f+30,(int)fl-30); } } }
        else if((fc==0xC0||fc==0xA0)&&sc->connected)sc->connected=0;
        else if(fc==0xD0&&sc->connected&&!sc->txd_build)mt_ba_rx_action(sc,f,fl);
    }
    if(sc->connected&&mt_eq6(f+16,sc->bssid))sc->rssi=rssi;
    sc->rx_ch=ch;
    if(mt7921_rx_deliver)mt7921_rx_deliver(f,fl,rssi);
}
void mt7921_rx_data(struct mt7921_softc* sc,const u8* buf,u32 len){ mt_ba_in_rx++; mt_rx_data_in(sc,buf,len); mt_ba_in_rx--; }
