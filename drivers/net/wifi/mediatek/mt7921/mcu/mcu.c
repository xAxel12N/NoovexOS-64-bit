/* SPDX-License-Identifier: BSD-3-Clause-Clear */
/*
 * Derived from Linux drivers/net/wireless/mediatek/mt76 (mt76_connac_mcu.c, mt7921/mcu.c).
 * Copyright (C) 2020 MediaTek Inc.
 * Licence text: drivers/net/wifi/mediatek/mt7921/LICENCE.BSD-3-Clause-Clear
 */
#include "drivers/net/wifi/mediatek/mt7921/mt7921.h"
#include "drivers/net/wifi/mediatek/mt7921/mcu/mcu.h"
u64 hr_ns(void);
u64 mt_ms(void){ return hr_ns()/1000000ull; }
void mt_put16(u8* p,u16 v){ p[0]=(u8)v; p[1]=(u8)(v>>8); }
void mt_put32(u8* p,u32 v){ p[0]=(u8)v; p[1]=(u8)(v>>8); p[2]=(u8)(v>>16); p[3]=(u8)(v>>24); }
u16 mt_get16(const u8* p){ return (u16)(p[0]|(p[1]<<8)); }
u32 mt_get32(const u8* p){ return (u32)p[0]|((u32)p[1]<<8)|((u32)p[2]<<16)|((u32)p[3]<<24); }
void mt_zero(void* d,u32 n){ u8* p=(u8*)d; while(n--)*p++=0; }
void mt_copy(void* d,const void* s,u32 n){ u8* a=(u8*)d; const u8* b=(const u8*)s; while(n--)*a++=*b++; }
static u8 mt_cmdbuf[MT_TXBUF_SIZE];
u32 mt_mcu_frame(struct mt7921_softc* sc,u32 cmd,const void* data,u32 len,u8* out,u32 cap,u8* seqo){
    int uni=!!(cmd&MCU_FIELD_UNI);
    u32 hl=uni?sizeof(struct mt_mcu_uni_txd):sizeof(struct mt_mcu_txd),tot=hl+len;
    if(tot>cap||tot>0xffffu)return 0;
    u8 seq=++sc->mcu_seq&0xf; if(!seq)seq=++sc->mcu_seq&0xf;
    mt_zero(out,hl); mt_copy(out+hl,data,len);
    mt_put32(out,(tot&0xffffu)|((u32)MT_TX_TYPE_CMD<<23)|((u32)MT_TX_MCU_PORT_RX_Q0<<25));
    mt_put32(out+4,MT_TXD1_LONG_FORMAT|((u32)MT_HDR_FORMAT_CMD<<16));
    if(uni){
        struct mt_mcu_uni_txd* u=(struct mt_mcu_uni_txd*)out;
        u->len=(u16)(tot-32); u->cid=(u16)MCU_CMD(cmd); u->option=MCU_CMD_UNI_EXT_ACK;
        u->s2d_index=MCU_S2D_H2N; u->pkt_type=MCU_PKT_ID; u->seq=seq;
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
int mt_mcu_wait(struct mt7921_softc* sc,u8 seq,u32 ms){
    u64 t0=mt_ms();
    while(!(sc->resp_ok&&sc->resp_seq==seq)){
        if(mt_ms()-t0>=ms){ sc->mcu_timeouts++; return -1; }
        mt7921_rx_poll(sc);
    }
    return 0;
}
int mt_mcu_status(struct mt7921_softc* sc,u32 cmd){
    const u8* p=sc->resp+sizeof(struct mt_mcu_rxd); int n=sc->resp_len-(int)sizeof(struct mt_mcu_rxd);
    if(cmd==MCU_CMD(MCU_CMD_PATCH_SEM_CONTROL)||cmd==MCU_CMD(MCU_CMD_PATCH_FINISH_REQ))return sc->resp_len>=(int)sizeof(struct mt_mcu_rxd)?sc->resp[sizeof(struct mt_mcu_rxd)-4]:-1;
    if(cmd&MCU_FIELD_UNI){
        if(n<8)return -1;
        if(p[0]!=(u8)MCU_CMD(cmd))return -2;
        return (int)mt_get32(p+4);
    }
    return 0;
}
int mt7921_mcu_send(struct mt7921_softc* sc,u32 cmd,const void* data,u32 len,int wait){
    if(!sc||sc->state<MT_ST_FOUND)return -1;
    if(sc->mcu_send)return sc->mcu_send(sc,cmd,data,len,wait);
    u8 seq; u32 n=mt_mcu_frame(sc,cmd,data,len,mt_cmdbuf,sizeof mt_cmdbuf,&seq);
    if(!n)return -1;
    sc->resp_ok=0; sc->resp_len=0;
    if(mt7921_dma_tx(sc,MT_Q_MCU,mt_cmdbuf,n)<0)return -1;
    sc->mcu_cmds++;
    if(!wait)return 0;
    for(int tries=0;tries<4;tries++){
        if(mt_mcu_wait(sc,seq,MT_MCU_TIMEOUT_MS)<0)return -1;
        int r=mt_mcu_status(sc,cmd);
        if(r!=-2)return r;
        sc->resp_ok=0;
    }
    return -1;
}
int mt7921_mcu_send_fwdl(struct mt7921_softc* sc,const u8* data,u32 len){
    if(!sc||!data||!len||len>MT_FWDL_CHUNK)return -1;
    return mt7921_dma_tx(sc,MT_Q_FWDL,data,len);
}
void mt7921_mcu_rx_event(struct mt7921_softc* sc,const u8* buf,u32 len){
    if(sc&&buf&&sc->rx_fix&&!(buf=sc->rx_fix(sc,buf,&len)))return;
    if(!sc||!buf||len<8)return;
    u32 d0=mt_get32(buf); u32 type=d0>>27,flag=(d0>>16)&0xf;
    if(type==PKT_TYPE_NORMAL||(type==PKT_TYPE_RX_EVENT&&flag==1)){ mt7921_rx_data(sc,buf,len); return; }
    if(type==PKT_TYPE_TXRX_NOTIFY){ mt7921_tx_free_event(sc,buf,len); return; }
    if(type!=PKT_TYPE_RX_EVENT||len<sizeof(struct mt_mcu_rxd))return;
    const struct mt_mcu_rxd* r=(const struct mt_mcu_rxd*)buf;
    u32 pl=len; if((d0&0xffffu)&&(d0&0xffffu)<pl)pl=d0&0xffffu;
    if(pl<sizeof(struct mt_mcu_rxd))return;
    const u8* p=buf+sizeof(struct mt_mcu_rxd); u32 n=pl-(u32)sizeof(struct mt_mcu_rxd);
    if(r->option&MCU_UNI_UNSOLICITED){ if(r->eid==MCU_UNI_EVENT_ROC)mt7921_roc_event(sc,p,n); return; }
    if(r->eid==MCU_EVENT_SCAN_DONE||r->eid==MCU_EVENT_SCHED_SCAN_DONE){ mt_scan_done_event(sc,p,n); return; }
    if(r->eid==MCU_EVENT_BSS_BEACON_LOSS){ mt_beacon_loss(sc); return; }
    if(r->eid==MCU_EVENT_DBG_MSG||r->eid==MCU_EVENT_COREDUMP||r->eid==MCU_EVENT_LP_INFO||r->eid==MCU_EVENT_TX_DONE)return;
    if(r->seq){
        u32 c=pl>sizeof sc->resp?(u32)sizeof sc->resp:pl;
        mt_copy(sc->resp,buf,c); sc->resp_len=(int)c; sc->resp_seq=r->seq; sc->resp_ok=1;
    }
}
int mt_mcu_query(struct mt7921_softc* sc,u32 cmd,const void* data,u32 len,const u8** out,int* olen){
    int r=mt7921_mcu_send(sc,cmd,data,len,1);
    if(r<0)return r;
    *out=sc->resp+sizeof(struct mt_mcu_rxd); *olen=sc->resp_len-(int)sizeof(struct mt_mcu_rxd);
    return *olen<0?-1:0;
}
int mt_mcu_nic_capab(struct mt7921_softc* sc){
    const u8* p; int n;
    if(mt_mcu_query(sc,MCU_CE_CMD(MCU_CE_CMD_GET_NIC_CAPAB),0,0,&p,&n)<0||n<4)return -1;
    int ne=mt_get16(p),o=4,got=0;
    for(int i=0;i<ne&&o+8<=n;i++){
        u32 t=mt_get32(p+o),l=mt_get32(p+o+4); o+=8;
        if(l>(u32)(n-o))break;
        if(t==MT_NIC_CAP_6G&&l>=1&&!sc->bss_build)mt7921_has_6g=!!p[o];
        if(t==MT_NIC_CAP_MAC_ADDR&&l>=6){ int z=1; for(int k=0;k<6;k++)if(p[o+k])z=0; if(!z&&!(p[o]&1)){ mt_copy(sc->mac,p+o,6); got=1; } }
        o+=(int)l;
    }
    return got?0:-1;
}
int mt_mcu_efuse_read(struct mt7921_softc* sc,u32 off,u8* val){
    u8 req[8+16]; mt_zero(req,sizeof req); mt_put32(req,off&~15u);
    const u8* p; int n;
    if(mt_mcu_query(sc,MCU_EXT_QUERY(MCU_EXT_CMD_EFUSE_ACCESS),req,sizeof req,&p,&n)<0||n<24)return -1;
    *val=p[8+(off&15)]; return 0;
}
static int mt_mcu_fwlog(struct mt7921_softc* sc,u8 ctrl){
    u8 d[4]={ctrl,0,0,0};
    return mt7921_mcu_send(sc,MCU_CE_CMD(MCU_CE_CMD_FWLOG_2_HOST),d,4,0);
}
int regdb_ch_allowed(u8 ch) __attribute__((weak));
int regdb_is_dfs(u8 ch) __attribute__((weak));
int regdb_no_ir(u8 ch) __attribute__((weak));
const char* regdb_country(void) __attribute__((weak));
static char mt_cc[3];
u32 mt_chan_domain_build(u8* b,u32 cap){
    static const u8 c2[]={1,2,3,4,5,6,7,8,9,10,11,12,13};
    static const u8 c5[]={36,40,44,48,52,56,60,64,100,104,108,112,116,120,124,128,132,136,140,144,149,153,157,161,165};
    if(cap<12+8*(sizeof c2+sizeof c5))return 0;
    mt_zero(b,12); const char* cc=regdb_country();
    b[0]=cc&&cc[0]&&cc[1]?(u8)cc[0]:'0'; b[1]=cc&&cc[0]&&cc[1]?(u8)cc[1]:'0'; b[4]=0; b[5]=3; b[6]=3;
    u8* q=b+12;
    for(int band=0;band<2;band++){
        const u8* c=band?c5:c2; u32 n=band?sizeof c5:sizeof c2,k=0;
        for(u32 i=0;i<n;i++){
            u8 ch=c[i];
            if(!regdb_ch_allowed(ch))continue;
            int dfs=regdb_is_dfs(ch);
            u32 fl=(dfs?MT_CHAN_RADAR|MT_CHAN_NO_IR:0)|(regdb_no_ir(ch)?MT_CHAN_NO_IR:0);
            mt_put16(q,ch); mt_put16(q+2,0); mt_put32(q+4,fl); q+=8; k++;
        }
        b[8+band]=(u8)k;
    }
    return (u32)(q-b);
}
int mt_mcu_set_chan_domain(struct mt7921_softc* sc){
    u8 b[12+8*38]; u32 n=mt_chan_domain_build(b,sizeof b);
    if(!n)return -1;
    mt_cc[0]=(char)b[0]; mt_cc[1]=(char)b[1];
    return mt7921_mcu_send(sc,MCU_CE_CMD(MCU_CE_CMD_SET_CHAN_DOMAIN),b,n,0);
}
int mt_mcu_chan_domain_sync(struct mt7921_softc* sc){
    const char* cc=regdb_country();
    if(!cc||!cc[0]||!cc[1]||(cc[0]==mt_cc[0]&&cc[1]==mt_cc[1]))return 0;
    return mt_mcu_set_chan_domain(sc);
}
int mt7921_mcu_init(struct mt7921_softc* sc){
    if(!sc||sc->state<MT_ST_FWOK)return -1;
    sc->omac_idx=0; sc->bss_idx=0; sc->band_idx=0; sc->wlan_idx=MT_WCID_AP;
    if(mt_mcu_nic_capab(sc)<0){
        u8 m[6]; int ok=1;
        for(int i=0;i<6&&ok;i++)if(mt_mcu_efuse_read(sc,MT_EE_MAC_ADDR+(u32)i,&m[i])<0)ok=0;
        if(ok&&!(m[0]&1)&&(m[0]|m[1]|m[2]|m[3]|m[4]|m[5]))mt_copy(sc->mac,m,6);
        else { sc->err="no MAC address from firmware"; return -1; }
    }
    mt_mcu_fwlog(sc,0);
    mt_mcu_set_chan_domain(sc);
    if(mt_dev_add(sc,1)<0){ sc->err="DEV_INFO/BSS_INFO add failed"; return -1; }
    sc->state=MT_ST_READY;
    return 0;
}
