/* SPDX-License-Identifier: BSD-3-Clause-Clear */
/*
 * Derived from Linux drivers/net/wireless/mediatek/mt76 (mt7925/mcu.c, mt7925/mcu.h, mt76_connac_mcu.h).
 * Copyright (C) 2023 MediaTek Inc.
 * Licence text: drivers/net/wifi/mediatek/mt7921/LICENCE.BSD-3-Clause-Clear
 */
#include "drivers/net/wifi/mediatek/mt7925/mt7925.h"
#include "drivers/net/wifi/mediatek/mt7921/mcu/mcu.h"
int mt_mcu_wait(struct mt7921_softc* sc,u8 seq,u32 ms);
int mt7925_has_6g;
static u8 m5_cmd[MT_TXBUF_SIZE];
static u8 m5_rx[MT_RXBUF_SIZE];
static u8 m5_scan_seq;
static u64 m5_scan_t0;
static int m5_tmo;
void (*mt7925_fw_lost)(struct mt7921_softc* sc);
u32 mt7925_mcu_frame(struct mt7921_softc* sc,u32 cmd,const void* data,u32 len,u8* out,u32 cap,u8* seqo){
    int uni=!!(cmd&MCU_FIELD_UNI);
    u32 hl=uni?sizeof(struct mt_mcu_uni_txd):sizeof(struct mt_mcu_txd),tot=hl+len;
    if(tot>cap||tot>0xffffu||(len&&!data))return 0;
    u8 seq=++sc->mcu_seq&0xf; if(!seq)seq=++sc->mcu_seq&0xf;
    mt_zero(out,hl); if(len)mt_copy(out+hl,data,len);
    mt_put32(out,(tot&0xffffu)|((u32)MT_TX_TYPE_CMD<<23)|((u32)MT_TX_MCU_PORT_RX_Q0<<25));
    mt_put32(out+4,(u32)MT_HDR_FORMAT_CMD<<16);
    if(uni){
        struct mt_mcu_uni_txd* u=(struct mt_mcu_uni_txd*)out;
        u->len=(u16)(tot-32); u->cid=(u16)MCU_CMD(cmd); u->s2d_index=MCU_S2D_H2N; u->pkt_type=MCU_PKT_ID; u->seq=seq;
        u->option=(cmd&MCU_FIELD_QUERY)?MCU7925_UNI_QUERY_ACK:MCU7925_UNI_EXT_ACK;
        if(MCU_CMD(cmd)==MCU_UNI_CMD_HIF_CTRL||MCU_CMD(cmd)==MCU_UNI_CMD_CHIP_CONFIG)u->option&=(u8)~MCU7925_CMD_ACK;
    } else {
        struct mt_mcu_txd* t=(struct mt_mcu_txd*)out;
        t->len=(u16)(tot-32); t->pq_id=(u16)MCU_PQ_ID(MT_TX_PORT_IDX_MCU,MT_TX_MCU_PORT_RX_Q0);
        t->pkt_type=MCU_PKT_ID; t->seq=seq; t->cid=(u8)MCU_CMD(cmd); t->ext_cid=(u8)(cmd>>8);
        if(t->ext_cid||(cmd&MCU_FIELD_CE)){ t->set_query=(cmd&MCU_FIELD_QUERY)?MCU_Q_QUERY:MCU_Q_SET; t->ext_cid_ack=!!t->ext_cid; }
        else t->set_query=MCU_Q_NA;
        t->s2d_index=MCU_S2D_H2N;
    }
    *seqo=seq; return tot;
}
static int m5_status(struct mt7921_softc* sc,u32 cmd){
    u32 c=MCU_CMD(cmd);
    if(!(cmd&MCU_FIELD_UNI))return (cmd==MCU_CMD(MCU_CMD_PATCH_SEM_CONTROL)||cmd==MCU_CMD(MCU_CMD_PATCH_FINISH_REQ))?mt_mcu_status(sc,cmd):0;
    if(c==MCU_UNI_CMD_DEV_INFO_UPDATE||c==MCU_UNI_CMD_BSS_INFO_UPDATE||c==MCU_UNI_CMD_STA_REC_UPDATE||c==MCU_UNI_CMD_OFFLOAD||c==MCU_UNI_CMD_SUSPEND)return mt_mcu_status(sc,cmd);
    return sc->resp_len>=(int)sizeof(struct mt_mcu_rxd)?0:-1;
}
int mt7925_mcu_send(struct mt7921_softc* sc,u32 cmd,const void* data,u32 len,int wait){
    if(!sc||sc->state<MT_ST_FOUND)return -1;
    u8 seq; u32 n=mt7925_mcu_frame(sc,cmd,data,len,m5_cmd,sizeof m5_cmd,&seq);
    if(!n)return -1;
    sc->resp_ok=0; sc->resp_len=0;
    if(mt7921_dma_tx(sc,MT_Q_MCU,m5_cmd,n)<0)return -1;
    sc->mcu_cmds++;
    if(!wait)return 0;
    for(int tries=0;tries<4;tries++){
        if(mt_mcu_wait(sc,seq,MT_MCU_TIMEOUT_MS)<0){ if(sc->state==MT_ST_READY&&++m5_tmo>=2){ m5_tmo=0; if(mt7925_fw_lost)mt7925_fw_lost(sc); } return -1; }
        m5_tmo=0; int r=m5_status(sc,cmd);
        if(r!=-2)return r;
        sc->resp_ok=0;
    }
    return -1;
}
const u8* mt7925_rx_fix(struct mt7921_softc* sc,const u8* buf,u32* len){
    u32 n=*len; if(n<8)return 0;
    u32 d0=mt_get32(buf),type=d0>>27,flag=(d0>>16)&0xf;
    if(type!=PKT_TYPE_RX_EVENT||flag==1)return buf;
    u32 pl=n; if((d0&0xffffu)&&(d0&0xffffu)<pl)pl=d0&0xffffu;
    if(pl<MT7925_RXD_SIZE||pl>sizeof m5_rx+MT7925_RXD_EXTRA)return 0;
    mt_copy(m5_rx,buf,24); mt_copy(m5_rx+24,buf+32,pl-32); pl-=MT7925_RXD_EXTRA;
    mt_put32(m5_rx,(d0&~0xffffu)|pl);
    const struct mt_mcu_rxd* r=(const struct mt_mcu_rxd*)m5_rx;
    if(r->eid==MCU_UNI_EVENT_SCAN_DONE&&(r->option&MCU_UNI_UNSOLICITED)){
        const u8* p=m5_rx+sizeof(struct mt_mcu_rxd);
        if(pl>sizeof(struct mt_mcu_rxd)&&(p[0]&0x7f)==m5_scan_seq){ sc->scanning=0; sc->scan_done=1; }
        sc->mcu_events++; return 0;
    }
    *len=pl; return m5_rx;
}
int mt7925_nic_capab_parse(struct mt7921_softc* sc,const u8* p,int n){
    if(n<4)return -1;
    int ne=mt_get16(p),o=4,got=0;
    for(int i=0;i<ne&&o+4<=n;i++){
        int t=mt_get16(p+o),l=mt_get16(p+o+2);
        if(l<4||l>n-o)break;
        const u8* d=p+o+4; int dl=l-4;
        if(t==MT7925_NIC_CAP_MAC_ADDR&&dl>=6&&!(d[0]&1)&&(d[0]|d[1]|d[2]|d[3]|d[4]|d[5])){ mt_copy(sc->mac,d,6); got=1; }
        if(t==MT7925_NIC_CAP_6G&&dl>=1)mt7925_has_6g=!!d[0];
        o+=l;
    }
    return got?0:-1;
}
int mt7925_mcu_init(struct mt7921_softc* sc){
    if(!sc||sc->state<MT_ST_FWOK)return -1;
    sc->omac_idx=0; sc->bss_idx=0; sc->band_idx=0; sc->wlan_idx=MT_WCID_AP;
    u8 req[8]; mt_zero(req,sizeof req); mt_put16(req+4,UNI_CHIP_CONFIG_NIC_CAPA); mt_put16(req+6,4);
    const u8* p; int n;
    if(mt_mcu_query(sc,MCU_UNI_CMD(MCU_UNI_CMD_CHIP_CONFIG),req,sizeof req,&p,&n)<0||mt7925_nic_capab_parse(sc,p,n)<0){ sc->err="no MAC address from firmware (NIC capability)"; return -1; }
    if(mt_dev_add(sc,1)<0){ sc->err="DEV_INFO/BSS_INFO add failed"; return -1; }
    sc->state=MT_ST_READY;
    return 0;
}
static u8* m5_tlv(u8* q,u16 tag,u16 len){ mt_zero(q,len); mt_put16(q,tag); mt_put16(q+2,len); return q; }
u32 mt7925_scan_req_build(struct mt7921_softc* sc,u8 seq,const u8* ch,const u8* band,int n,u8* out,u32 cap){
    if(cap<MT7925_SCAN_REQ_LEN||n<0)return 0;
    if(n>MT7925_SCAN_MAX_CH)n=MT7925_SCAN_MAX_CH;
    mt_zero(out,4); out[0]=(u8)((seq&0x7f)|(sc->band_idx<<7)); out[1]=sc->bss_idx;
    u8* q=out+4;
    u8* r=m5_tlv(q,UNI_SCAN_REQ,20); r[6]=SCAN_FUNC_SPLIT_SCAN; q+=20;
    u8* s=m5_tlv(q,UNI_SCAN_SSID,368); s[4]=1; q+=368;
    u8* b=m5_tlv(q,UNI_SCAN_BSSID,16); for(int i=0;i<6;i++)b[4+i]=0xff; q+=16;
    u8* c=m5_tlv(q,UNI_SCAN_CHANNEL,136); c[4]=n?4:0; c[5]=(u8)n;
    for(int i=0;i<n;i++){ c[8+2*i]=band&&band[i]?band[i]:(ch[i]<=14?MT7925_BAND_2G:MT7925_BAND_5G); c[9+2*i]=ch[i]; }
    q+=136;
    m5_tlv(q,UNI_SCAN_MISC,12); q+=12;
    return (u32)(q-out);
}
u32 mt7925_scan_cancel_build(struct mt7921_softc* sc,u8 seq,u8* out){
    mt_zero(out,12); out[0]=(u8)((seq&0x7f)|(sc->band_idx<<7)); out[1]=sc->bss_idx; m5_tlv(out+4,UNI_SCAN_CANCEL,8); return 12;
}
int mt7925_hw_scan(struct mt7921_softc* sc,const u8* ch,const u8* band,int n){
    static u8 req[MT7925_SCAN_REQ_LEN];
    if(!sc||sc->state<MT_ST_READY||(n&&!ch))return -1;
    if(sc->scanning&&mt_ms()-m5_scan_t0<15000)return -1;
    for(int i=0;band&&i<n;i++)if(band[i]==MT7925_BAND_6G&&!mt7925_has_6g)return -1;
    m5_scan_seq=(u8)((m5_scan_seq+1)&0x7f);
    u32 l=mt7925_scan_req_build(sc,m5_scan_seq,ch,band,n,req,sizeof req);
    if(!l)return -1;
    sc->scanning=1; sc->scan_done=0; sc->nscan=0; m5_scan_t0=mt_ms();
    if(mt7925_mcu_send(sc,MCU_UNI_CMD(MCU_UNI_CMD_SCAN_REQ),req,l,1)<0){ sc->scanning=0; return -1; }
    return 0;
}
int mt7925_scan_busy(struct mt7921_softc* sc){
    if(!sc||!sc->scanning)return 0;
    if(mt_ms()-m5_scan_t0>15000){ u8 b[12]; sc->scanning=0; sc->scan_done=1; mt7925_mcu_send(sc,MCU_UNI_CMD(MCU_UNI_CMD_SCAN_REQ),b,mt7925_scan_cancel_build(sc,m5_scan_seq,b),0); }
    return sc->scanning;
}
int mt7925_connect(struct mt7921_softc* sc,const u8* bssid,u8 ch,u8 band,const char* ssid,u8 akm,u8 ht){
    if(!sc)return -1;
    if(band==MT7925_BAND_6G&&!mt7925_has_6g){ sc->err="6 GHz not supported by this card"; return -1; }
    sc->band6=band==MT7925_BAND_6G;
    mt7925_peer_eht.ok=0; mt7925_eht_adv=mt7925_eht_on&&mt7925_eht_fail<3&&mt7925_eht_ap(bssid);
    int r=mt7921_connect(sc,bssid,ch,ssid,akm,ht); mt7925_eht_adv=0;
    if(mt_assoc_eht_res==1&&++mt7925_eht_fail>=3)sc->err="access point refused Wi-Fi 7 (EHT) 3 times; using Wi-Fi 6 (HE)";
    if(r<0){ sc->band6=0; if(mt7925_peer_eht.ok&&++mt7925_eht_fail>=3)sc->err="Wi-Fi 7 (EHT) station setup failed 3 times; EHT off"; mt7925_peer_eht.ok=0; }
    else if(mt7925_peer_eht.ok&&mt_assoc_eht_res!=1)mt7925_eht_fail=0;
    return r;
}
