/* SPDX-License-Identifier: BSD-3-Clause-Clear */
/*
 * Derived from Linux drivers/net/wireless/mediatek/mt76 (mt7925/mac.c, mt7925/mcu.c, mt7925/mcu.h, mt76_connac3_mac.h, mt76_connac_mcu.h).
 * Copyright (C) 2023 MediaTek Inc.
 * Licence text: drivers/net/wifi/mediatek/mt7921/LICENCE.BSD-3-Clause-Clear
 */
#include "drivers/net/wifi/mediatek/mt7925/mt7925.h"
#include "drivers/net/wifi/mediatek/mt7921/mcu/mcu.h"
#include "include/nvx/net/wifi/wifi_hw.h"
static u8 m5m_cipher;
static u8 m5m_ptk[16];
u32 mt7925_txd_build(struct mt7921_softc* sc,const u8* f,u32 len,u8* out){
    if(!sc||!f||len<24||len+MT_TXD_SIZE+MT_TXP_SIZE>MT_TXBUF_SIZE)return 0;
    u8 fc=f[0],type=(fc>>2)&3,stype=fc>>4;
    int mgmt=type==0,data=type==2,mc=f[4]&1,fixed=!data||mc;
    u32 hl=24; if(data&&(stype&8))hl+=2; if((f[1]&3)==3)hl+=6;
    u8 q=mgmt?MT_LMAC_ALTX0:1,wcid=mc?MT_WCID_BMC:MT_WCID_AP;
    u8 tid=(mgmt&&stype==13&&len>=26&&f[24]==3&&f[25]==0)?MT7925_TX_ADDBA:MT7925_TX_NORMAL;
    mt_zero(out,MT_TXD_SIZE);
    mt_put32(out,((len+MT_TXD_SIZE)&0xffffu)|((u32)MT_TX_TYPE_CT<<23)|((u32)q<<25));
    u32 d1=wcid|((u32)(sc->omac_idx&0x3f)<<25)|((u32)MT_HDR_FORMAT_802_11<<14)|((hl/2)<<16)|((u32)(tid&15)<<21);
    if(sc->band_idx&&sc->band_idx!=0xff)d1|=(u32)(sc->band_idx&3)<<12;
    if(fixed)d1|=MT7925_TXD1_FIXED_RATE;
    mt_put32(out+4,d1);
    mt_put32(out+8,((u32)type<<4)|stype);
    u32 d3=(15u<<11)|((u32)mc<<4);
    if(fixed)d3|=MT7925_TXD3_BA_DISABLE;
    mt_put32(out+12,d3);
    u32 d6=MT7925_TXD6_DAS|(1u<<4)|MT7925_TXD6_DIS_MAT;
    if(fixed)d6|=(u32)MT7925_BASIC_RATES_TBL<<16;
    mt_put32(out+24,d6);
    mt_copy(out+MT_TXD_SIZE,f,len);
    return len+MT_TXD_SIZE;
}
int mt7925_rxd_parse(const u8* buf,u32 len,u32* off,u32* flen,int* rssi,u8* ch){
    if(!buf||len<MT7925_RXD_BASE)return -1;
    u32 d0=mt_get32(buf),d1=mt_get32(buf+4),d2=mt_get32(buf+8),d3=mt_get32(buf+12);
    u32 pl=d0&0xffffu; if(pl&&pl<len)len=pl;
    if(len<MT7925_RXD_BASE)return -1;
    if(d1&((1u<<25)|(1u<<26)))return -1;
    if(d2&((1u<<23)|(1u<<24)|(1u<<7)))return -1;
    if(d3&(1u<<24))return -1;
    *ch=(u8)((d3>>8)&0xff); *rssi=-127;
    u32 o=MT7925_RXD_BASE;
    if(d1&(1u<<19))o+=16;
    if(d1&(1u<<16))o+=16;
    if(d1&(1u<<17))o+=8;
    if(d1&(1u<<18)){
        if(o+16>len)return -1;
        int r=(int)(mt_get32(buf+o+12)&0xff); if(r)*rssi=(r-220)/2;
        o+=16;
        if(d1&(1u<<20))o+=96;
    }
    o+=2*((d2>>13)&7);
    if(o>=len||len-o<10)return -1;
    *off=o; *flen=len-o;
    return 0;
}
static u8* m5m_tlv(u8* q,u16 tag,u16 len){ mt_zero(q,len); mt_put16(q,tag); mt_put16(q+2,len); return q; }
u32 mt7925_bss_build(struct mt7921_softc* sc,const u8* bssid,u8 ch,int en,u8* out){
    if(!sc||!out)return 0;
    int b6=sc->band6,b2=!b6&&ch<=14;
    mt_zero(out,4); out[0]=sc->bss_idx; u8* q=out+4;
    u8* t=m5m_tlv(q,UNI_BSS_INFO_BASIC,32); q+=32;
    t[4]=1; t[5]=sc->omac_idx; t[6]=sc->omac_idx; t[7]=sc->band_idx; mt_put32(t+8,CONNECTION_INFRA_STA); t[12]=(u8)!en; t[13]=0;
    if(bssid)mt_copy(t+14,bssid,6);
    mt_put16(t+20,MT_WCID_BMC); mt_put16(t+22,100); t[24]=1;
    t[25]=(u8)(b6?0:b2?(MT7925_PHY_MODE_B|MT7925_PHY_MODE_G|MT7925_PHY_MODE_GN):(MT7925_PHY_MODE_A|MT7925_PHY_MODE_AN|MT7925_PHY_MODE_AC));
    mt_put16(t+26,MT_WCID_AP); mt_put16(t+28,b2?MT7925_PHY_TYPE_ERP_INDEX:MT7925_PHY_TYPE_OFDM_INDEX); t[30]=(u8)(b6?MT7925_PHY_MODE_AX_6G:0); t[31]=sc->bss_idx;
    t=m5m_tlv(q,UNI_BSS_INFO_SEC,8); q+=8; t[4]=m5m_cipher==MT7925_CIPHER_AES_CCMP?MT7925_MODE_WPA2_PSK:0; t[5]=m5m_cipher==MT7925_CIPHER_AES_CCMP?6:1; t[6]=m5m_cipher;
    t=m5m_tlv(q,UNI_BSS_INFO_RATE,16); q+=16; mt_put16(t+6,b2?0x000f:0x0540); t[12]=(u8)b2; t[13]=MT7925_BASIC_RATES_TBL; t[14]=MT7925_BASIC_RATES_TBL;
    t=m5m_tlv(q,UNI_BSS_INFO_QBSS,8); q+=8; t[4]=1;
    t=m5m_tlv(q,UNI_BSS_INFO_MLD,20); q+=20; t[4]=0xff; t[5]=(u8)(sc->bss_idx+32); mt_copy(t+6,sc->mac,6); t[12]=0xff; t[13]=0xff;
    t=m5m_tlv(q,UNI_BSS_INFO_IFS_TIME,20); q+=20; t[4]=1; mt_put16(t+8,(u16)(b2?20:9));
    if(en){ t=m5m_tlv(q,UNI_BSS_INFO_RLM,16); q+=16; t[4]=ch; t[5]=ch; t[7]=0; t[8]=2; t[9]=2; t[12]=(u8)(b6?MT7925_BAND_6G:b2?MT7925_BAND_2G:MT7925_BAND_5G); }
    return (u32)(q-out);
}
static u32 m5m_sta_hdr(struct mt7921_softc* sc,u8 wcid,u16 n,u8* out){ mt_zero(out,8); out[0]=sc->bss_idx; out[1]=wcid; mt_put16(out+2,n); out[4]=1; out[5]=sc->omac_idx; out[6]=0; return 8; }
u32 mt7925_sta_build(struct mt7921_softc* sc,u8 wcid,const u8* peer,u8 state,u16 aid,u8* out){
    if(!sc||!out)return 0;
    u8* q=out+8; u8* t;
    if(state==MT_STA_REMOVE){ m5m_sta_hdr(sc,wcid,2,out); m5m_tlv(q,STA_REC_REMOVE,8); q+=8; m5m_tlv(q,STA_REC_MLD_OFF,4); q+=4; return (u32)(q-out); }
    int b2=!sc->band6&&sc->chan&&sc->chan<=14,st=state==CONN_STATE_DISCONNECT?MT7925_STA_STATE_NONE:MT7925_STA_STATE_ASSOC;
    u16 n=4;
    t=m5m_tlv(q,STA_REC_BASIC,20); q+=20; mt_put32(t+4,CONNECTION_INFRA_AP); t[8]=CONN_STATE_PORT_SECURE; t[9]=1; mt_put16(t+10,aid);
    if(peer)mt_copy(t+12,peer,6); else mt_copy(t+12,"\xff\xff\xff\xff\xff\xff",6);
    mt_put16(t+18,(u16)(EXTRA_INFO_VER|(st==MT7925_STA_STATE_NONE?EXTRA_INFO_NEW:0)));
    t=m5m_tlv(q,STA_REC_PHY,12); q+=12; mt_put16(t+4,(u16)(b2?0x000f:0x0015)); t[6]=(u8)(b2?(MT7925_MODE_CCK|MT7925_MODE_OFDM):MT7925_MODE_OFDM);
    t=m5m_tlv(q,STA_REC_STATE,16); q+=16; t[4]=(u8)st;
    t=m5m_tlv(q,STA_REC_HDR_TRANS,8); q+=8; t[5]=1; t[6]=1;
    if(st==MT7925_STA_STATE_ASSOC&&mt7925_peer_eht.ok&&mt7925_eht_on&&mt7925_eht_fail<3){ q+=mt7925_sta_eht_tlv(&mt7925_peer_eht,q); t=out+8+20; t[6]|=MT7925_MODE_EHT; n++; }
    m5m_sta_hdr(sc,wcid,n,out);
    return (u32)(q-out);
}
u32 mt7925_key_build(struct mt7921_softc* sc,u8 wcid,int idx,int type,const u8* key,int len,u8* out){
    if(!sc||!out)return 0;
    m5m_sta_hdr(sc,wcid,1,out);
    u8* t=m5m_tlv(out+8,STA_REC_KEY_V3,68);
    t[7]=0; mt_copy(t+8,sc->bssid,6); t[14]=sc->bss_idx; t[19]=1; t[18]=wcid;
    if(wcid==MT_WCID_AP){ t[5]=1; t[6]=1; }
    if(!key||len<=0)return 8+68;
    if(len>32)return 0;
    if(type==WIFI_KEY_CCMP){ if(len!=16)return 0; t[15]=MT7925_CIPHER_AES_CCMP; t[17]=(u8)len; mt_copy(t+20,key,16); if(wcid==MT_WCID_AP){ mt_copy(m5m_ptk,key,16); m5m_cipher=MT7925_CIPHER_AES_CCMP; } }
    else if(type==WIFI_KEY_BIP){ if(len!=16)return 0; t[15]=MT7925_CIPHER_BIP_CMAC_128; t[17]=32; mt_copy(t+20,m5m_ptk,16); mt_copy(t+36,key,16); }
    else return 0;
    t[4]=1; t[16]=(u8)idx;
    return 8+68;
}
struct mt7925_eht mt7925_peer_eht;
int mt7925_eht_on=1, mt7925_eht_fail;
static u8 m5m_eap[16][6]; static int m5m_neap; int mt7925_eht_adv;
static int m5m_eq6(const u8* a,const u8* b){ for(int i=0;i<6;i++)if(a[i]!=b[i])return 0; return 1; }
int mt7925_eht_ap(const u8* bssid){ for(int i=0;bssid&&i<m5m_neap&&i<16;i++)if(m5m_eq6(m5m_eap[i],bssid))return 1; return 0; }
void mt7925_eht_note(const u8* f,u32 len){ if(!f||len<36||((f[0]&0xFC)!=0x80&&(f[0]&0xFC)!=0x50)||mt7925_eht_ap(f+16))return;
    for(u32 o=36;o+2<=len;){ u32 l=f[o+1]; if(o+2+l>len)return; if(f[o]==255&&l>=13&&f[o+2]==108){ mt_copy(m5m_eap[m5m_neap%16],f+16,6); m5m_neap++; if(m5m_neap>=32)m5m_neap=16; return; } o+=2+l; } }
int mt7925_assoc_eht(u8* o,int cap){ if(!mt7925_eht_adv||!mt7925_eht_on||mt7925_eht_fail>=3||cap<17)return 0;
    static const u8 e[17]={255,15,108,0,0,0,0,0,0,0,0,0,0,0,0x22,0x22,0x22}; mt_copy(o,e,17); return 17; }
void mt7925_assoc_ies(const u8* ies,int n){ mt7925_peer_eht.ok=0; if(mt7925_eht_on&&mt7925_eht_fail<3&&ies&&n>0)mt7925_eht_parse(ies,n,&mt7925_peer_eht); }
int mt7925_eht_parse(const u8* ies,int n,struct mt7925_eht* e){
    if(!e)return -1;
    mt_zero(e,sizeof *e);
    for(int o=0;ies&&o+2<=n;){
        int id=ies[o],l=ies[o+1];
        if(o+2+l>n)return -1;
        const u8* d=ies+o+2;
        if(id==255&&l>=1+2+9&&d[0]==108){
            e->mac_cap=mt_get16(d+1); mt_copy(e->phy,d+3,9);
            int m=l-12; if(m>9)m=9; if(m>0)mt_copy(e->mcs,d+12,(u32)m);
            e->nmcs=(u8)(m>0?m:0); e->ok=1; return 0; }
        o+=2+l;
    }
    return -1;
}
u32 mt7925_sta_eht_tlv(const struct mt7925_eht* e,u8* out){
    if(!e||!e->ok)return 0;
    u8* t=m5m_tlv(out,STA_REC_EHT,40);
    t[4]=0xff; mt_put16(t+6,e->mac_cap); mt_copy(t+8,e->phy,8); t[16]=e->phy[8];
    u32 m=e->nmcs; if(m>9)m=9; mt_copy(t+28,e->mcs,m);
    return 40;
}
void mt7925_mac_attach(struct mt7921_softc* sc){
    if(!sc)return;
    m5m_cipher=0; mt_zero(m5m_ptk,16);
    sc->txd_build=mt7925_txd_build; sc->rxd_parse=mt7925_rxd_parse; sc->bss_build=mt7925_bss_build; sc->sta_build=mt7925_sta_build; sc->key_build=mt7925_key_build;
}
