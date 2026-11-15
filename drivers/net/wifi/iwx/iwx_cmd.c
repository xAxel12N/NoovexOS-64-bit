/*
 * Derived from OpenBSD sys/dev/pci/if_iwx.c, if_iwxreg.h (rev 1.230 / 1.75).
 * Copyright (c) 2014, 2016 genua gmbh <info@genua.de>
 *   Author: Stefan Sperling <stsp@openbsd.org>
 * Copyright (c) 2014 Fixup Software Ltd.
 * Copyright (c) 2017, 2019, 2020 Stefan Sperling <stsp@openbsd.org>
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 *
 * Portions: Copyright(c) 2017 Intel Deutschland GmbH, Copyright(c) 2018 - 2019
 * Intel Corporation, used under the BSD-3-Clause branch of the dual BSD/GPLv2
 * licence: Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 * Redistributions of source code must retain the above copyright notice, this
 * list of conditions and the following disclaimer. Redistributions in binary
 * form must reproduce the above copyright notice, this list of conditions and
 * the following disclaimer in the documentation and/or other materials provided
 * with the distribution. Neither the name Intel Corporation nor the names of
 * its contributors may be used to endorse or promote products derived from this
 * software without specific prior written permission. THIS SOFTWARE IS PROVIDED
 * BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND ANY EXPRESS OR IMPLIED
 * WARRANTIES ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR
 * CONTRIBUTORS BE LIABLE FOR ANY DAMAGES ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */
#include "drivers/net/wifi/iwx/iwx_cmd.h"
#include "include/nvx/core/klog.h"
static struct { volatile int pending,done; u32 id; u8* resp; u32 max,len; } iwx_w;
static struct iwx_nvm iwx_nvm_d;
static int iwx_alive_seen;
u32 iwx_sku_id[3];
volatile int iwx_pnvm_done;
static volatile int iwx_sp_state;
extern int iwx_load_pnvm(struct iwx_softc* sc) __attribute__((weak));
extern int iwx_txq_info(struct iwx_softc* sc,int qid,u64* desc,u64* bc) __attribute__((weak));
extern void iwx_txq_reset(struct iwx_softc* sc,int qid) __attribute__((weak));
extern void iwx_set_ready(struct iwx_softc* sc,int on) __attribute__((weak));
extern int iwx_use_mld(struct iwx_softc* sc) __attribute__((weak));
extern int iwx_cmd_version(struct iwx_softc* sc,u8 grp,u8 cmd) __attribute__((weak));
extern int iwx_notif_version(struct iwx_softc* sc,u8 grp,u8 cmd) __attribute__((weak));
static int iwx_ver_call(int (*volatile f)(struct iwx_softc*,u8,u8),struct iwx_softc* sc,u8 g,u8 c,int dflt){ return f?f(sc,g,c):dflt; }
extern int iwx_scan_find(const u8* bssid,struct iwx_bss* out);
extern int iwx_rate_v2,iwx_hwkeys;
extern u8 iwx_jbss[6];
extern volatile int iwx_jwant,iwx_auth_st,iwx_assoc_st;
extern volatile u16 iwx_assoc_aid;
static void iwx_cpy(void* d,const void* s,u32 n){ u8* a=(u8*)d; const u8* b=(const u8*)s; while(n--)*a++=*b++; }
static void iwx_zero(void* d,u32 n){ u8* a=(u8*)d; while(n--)*a++=0; }
const struct iwx_nvm* iwx_nvm_info(void){ return &iwx_nvm_d; }
void iwx_rx_pkt(struct iwx_softc* sc,const u8* p,u32 len){
    if(len<sizeof(struct iwx_rx_packet))return;
    const struct iwx_rx_packet* pkt=(const struct iwx_rx_packet*)p;
    u32 plen=(pkt->len_n_flags&0x3FFFu);
    if(plen<sizeof(struct iwx_cmd_header)||plen+4>len)plen=len-4;
    plen-=sizeof(struct iwx_cmd_header);
    if(pkt->len_n_flags==0||pkt->len_n_flags==0x55550000u||pkt->len_n_flags==0xFFFFFFFFu)return;
    u32 id=IWX_WIDE_ID(pkt->hdr.flags,pkt->hdr.code);
    const u8* d=pkt->data;
    if(!(pkt->hdr.qid&0x80)&&iwx_w.pending&&!iwx_w.done&&(iwx_w.id==id||(iwx_w.id<0x100&&pkt->hdr.code==iwx_w.id&&pkt->hdr.flags<=IWX_LONG_GROUP)||pkt->hdr.code==IWX_REPLY_ERROR)){
        u32 n=plen<iwx_w.max?plen:iwx_w.max;
        if(iwx_w.resp&&n)iwx_cpy(iwx_w.resp,d,n);
        iwx_w.len=plen; iwx_w.done=pkt->hdr.code==IWX_REPLY_ERROR?2:1; return;
    }
    switch(pkt->hdr.code){
    case IWX_ALIVE: iwx_alive_seen=1;
        if(pkt->hdr.flags==0&&plen>=128){ for(int i=0;i<3;i++)iwx_sku_id[i]=(u32)d[116+i*4]|((u32)d[117+i*4]<<8)|((u32)d[118+i*4]<<16)|((u32)d[119+i*4]<<24); }
        break;
    case 0xfe: if(pkt->hdr.flags==IWX_REGULATORY_AND_NVM_GROUP)iwx_pnvm_done=1; break;
    case 0xfb: if(pkt->hdr.flags==IWX_MAC_CONF_GROUP&&plen>=16){ u32 st=(u32)d[4]|((u32)d[5]<<8), sta=(u32)d[8]|((u32)d[9]<<8); iwx_sp_state=st?(sta?1:2):3; } break;
    case IWX_REPLY_RX_MPDU_CMD: iwx_rx_mpdu(sc,d,plen); break;
    case IWX_SCAN_COMPLETE_UMAC: iwx_scan_done_notif(d,plen); break;
    case IWX_REPLY_ERROR: klog("iwx: firmware REPLY_ERROR"); break;
    default: break;
    }
}
int iwx_send_cmd_sync(struct iwx_softc* sc,u32 id,const void* d,u32 len,void* resp,u32 max,u32* rlen,u32 tmo){
    if(!sc)return -1;
    iwx_w.id=id; iwx_w.resp=(u8*)resp; iwx_w.max=resp?max:0; iwx_w.len=0; iwx_w.done=0; iwx_w.pending=1;
    if(iwx_tx_hcmd(sc,id,d,len)<0){ iwx_w.pending=0; return -1; }
    u64 t0=iwx_now_ms();
    while(!iwx_w.done){ iwx_rx_poll(sc); if(iwx_now_ms()-t0>tmo)break; }
    int r=iwx_w.done==1?0:(iwx_w.done==2?-2:-3);
    iwx_w.pending=0; iwx_w.resp=0;
    if(rlen)*rlen=iwx_w.len;
    if(r==-3){ static int warned; if(!warned){ warned=1; klog("iwx: host command timeout"); } }
    return r;
}
int iwx_send_cmd_status(struct iwx_softc* sc,u32 id,const void* d,u32 len,u32* status){
    u32 st=0,rl=0; int r=iwx_send_cmd_sync(sc,id,d,len,&st,4,&rl,1000);
    if(r==0&&rl<4)r=-4;
    if(status)*status=st;
    return r;
}
int iwx_nvm_init(struct iwx_softc* sc){
    static struct iwx_nvm_get_info_rsp rsp;
    u32 cmd=0,rl=0;
    int v4=iwx_fw_api(sc,IWX_UCODE_TLV_API_REGULATORY_NVM_INFO);
    u32 want=v4?sizeof(struct iwx_nvm_get_info_rsp):sizeof(struct iwx_nvm_get_info_rsp_v3);
    iwx_zero(&rsp,sizeof rsp);
    if(iwx_send_cmd_sync(sc,IWX_WIDE_ID(IWX_REGULATORY_AND_NVM_GROUP,IWX_NVM_GET_INFO),&cmd,4,&rsp,sizeof rsp,&rl,2000)<0)return -1;
    if(rl!=want){ klog("iwx: NVM_GET_INFO size mismatch"); return -1; }
    struct iwx_nvm* n=&iwx_nvm_d; iwx_zero(n,sizeof *n);
    iwx_hw_mac(sc,n->hw_addr);
    int ok=0; for(int i=0;i<6;i++)if(n->hw_addr[i])ok=1;
    if(!ok||(n->hw_addr[0]&1)){ klog("iwx: no valid mac address"); return -1; }
    n->nvm_version=rsp.nvm_version;
    n->band24=!!(rsp.mac_sku_flags&IWX_NVM_MAC_SKU_FLAGS_BAND_2_4_ENABLED);
    n->band5=!!(rsp.mac_sku_flags&IWX_NVM_MAC_SKU_FLAGS_BAND_5_2_ENABLED);
    n->valid_tx_ant=(u8)rsp.tx_chains; n->valid_rx_ant=(u8)rsp.rx_chains;
    n->lar_enabled=rsp.lar_enabled&&iwx_fw_capa(sc,IWX_UCODE_TLV_CAPA_LAR_SUPPORT);
    static const u8 ch[]={1,2,3,4,5,6,7,8,9,10,11,12,13,14,36,40,44,48,52,56,60,64,100,104,108,112,116,120,124,128,132,136,140,144,149,153,157,161,165};
    const struct iwx_nvm_get_info_rsp_v3* r3=(const struct iwx_nvm_get_info_rsp_v3*)&rsp;
    for(u32 i=0;i<sizeof ch;i++){
        u32 f=v4?rsp.channel_profile[i]:r3->channel_profile[i];
        if(!(f&1))continue;
        if(ch[i]<=14&&!n->band24)continue;
        if(ch[i]>14&&!n->band5)continue;
        n->chan[n->n_channels++]=ch[i];
    }
    return 0;
}
int iwx_init_hw_cmds(struct iwx_softc* sc){
    if(!sc)return -1;
    if(iwx_is_ax210(sc)){
        if(!iwx_load_pnvm){ static int s; if(!s){ s=1; klog("iwx: PNVM loader not built in; AX210/AX211 firmware may not start"); } }
        else if(iwx_load_pnvm(sc)<0)return -1;
    }
    if(iwx_nvm_init(sc)<0)return -1;
    { int nv=iwx_ver_call(iwx_notif_version,sc,IWX_LONG_GROUP,IWX_TX_CMD,0); iwx_rate_v2=nv>6&&nv!=99; }
    u32 ant=iwx_nvm_d.valid_tx_ant?iwx_nvm_d.valid_tx_ant:3;
    if(iwx_send_cmd_sync(sc,IWX_TX_ANT_CONFIGURATION_CMD,&ant,4,0,0,0,1000)==-3)return -1;
    u32 bt[2]={0x3,0};
    iwx_send_cmd_sync(sc,IWX_BT_CONFIG,bt,8,0,0,0,1000);
    if(iwx_fw_capa(sc,IWX_UCODE_TLV_CAPA_DQA_SUPPORT)){ u32 q=IWX_DEFAULT_QUEUE_CMD; iwx_send_cmd_sync(sc,IWX_WIDE_ID(IWX_DATA_PATH_GROUP,IWX_DQA_ENABLE_CMD),&q,4,0,0,0,1000); }
    u16 pw[2]={0,0};
    iwx_send_cmd_sync(sc,IWX_POWER_TABLE_CMD,pw,4,0,0,0,1000);
    if(iwx_nvm_d.lar_enabled){
        struct iwx_mcc_update_cmd m; iwx_zero(&m,sizeof m); m.mcc=('Z'<<8)|'Z'; m.source_id=0x11;
        static u8 mr[64]; iwx_send_cmd_sync(sc,IWX_MCC_UPDATE_CMD,&m,sizeof m,mr,sizeof mr,0,2000);
    }
    struct iwx_scan_config sc5; iwx_zero(&sc5,sizeof sc5); sc5.bcast_sta_id=1; sc5.tx_chains=ant; sc5.rx_chains=iwx_nvm_d.valid_rx_ant?iwx_nvm_d.valid_rx_ant:3;
    if(iwx_send_cmd_sync(sc,IWX_WIDE_ID(IWX_LONG_GROUP,IWX_SCAN_CFG_CMD),&sc5,sizeof sc5,0,0,0,1000)<0){ klog("iwx: scan config failed"); return -1; }
    struct iwx_beacon_filter_cmd bf; iwx_zero(&bf,sizeof bf);
    iwx_send_cmd_sync(sc,IWX_REPLY_BEACON_FILTERING_CMD,&bf,sizeof bf,0,0,0,1000);
    return 0;
}
#define IWX_PHY_CONTEXT_CMD 0x08
#define IWX_ADD_STA_KEY 0x17
#define IWX_ADD_STA 0x18
#define IWX_REMOVE_STA 0x19
#define IWX_SCD_QUEUE_CFG 0x1d
#define IWX_MAC_CONTEXT_CMD 0x28
#define IWX_BINDING_CONTEXT_CMD 0x2b
#define IWX_SESSION_PROTECTION_CMD 0x05
#define IWX_RLC_CONFIG_CMD 0x08
#define IWX_SCD_QUEUE_CONFIG_CMD 0x17
#define IWX_ACT_ADD 1
#define IWX_ACT_MODIFY 2
#define IWX_ACT_REMOVE 3
#define IWX_MGMT_TID 15
#define IWX_UCODE_TLV_CAPA_CDB_SUPPORT 40
#define IWX_UCODE_TLV_CAPA_ULTRA_HB_CHANNELS 48
struct __attribute__((packed)) iwx_phy_ctxt_cmd { u32 id_and_color,action; u8 band,channel,width,ctrl_pos; u32 lmac_id,rxchain_info,dsp_cfg_flags,reserved; };
struct __attribute__((packed)) iwx_phy_ctxt_cmd_uhb { u32 id_and_color,action; u32 channel; u8 band,width,ctrl_pos,rsv; u32 lmac_id,rxchain_info,dsp_cfg_flags,reserved; };
struct __attribute__((packed)) iwx_rlc_cmd { u32 phy_id; u32 rx_chain_info,rsv; u32 sad[4]; u8 flags,reserved[3]; };
struct __attribute__((packed)) iwx_binding_cmd { u32 id_and_color,action,macs[3],phy,lmac_id; };
struct __attribute__((packed)) iwx_ac_qos { u16 cw_min,cw_max; u8 aifsn,fifos_mask; u16 edca_txop; };
struct __attribute__((packed)) iwx_mac_ctx_cmd {
    u32 id_and_color,action,mac_type,tsf_id; u8 node_addr[6]; u16 r1; u8 bssid_addr[6]; u16 r2;
    u32 cck_rates,ofdm_rates,protection_flags,cck_short_preamble,short_slot,filter_flags,qos_flags; struct iwx_ac_qos ac[5];
    u32 is_assoc,dtim_time; u64 dtim_tsf; u32 bi,reserved1,dtim_interval,data_policy,listen_interval,assoc_id,assoc_beacon_arrive_time,ctwin;
};
struct __attribute__((packed)) iwx_add_sta_cmd {
    u8 add_modify,awake_acs; u16 tid_disable_tx; u32 mac_id_n_color; u8 addr[6]; u16 reserved2; u8 sta_id,modify_mask; u16 reserved3;
    u32 station_flags,station_flags_msk; u8 add_immediate_ba_tid,remove_immediate_ba_tid; u16 add_immediate_ba_ssn,sleep_tx_count;
    u8 sleep_state_flags,station_type; u16 assoc_id,beamform_flags; u32 tfd_queue_msk; u16 rx_ba_window; u8 sp_length,uapsd_acs;
};
struct __attribute__((packed)) iwx_add_sta_key_cmd { u8 sta_id,key_offset; u16 key_flags; u8 key[32]; u8 rx_secur_seq_cnt[16]; u64 rx_mic_key,tx_mic_key,transmit_seq_cnt; };
struct __attribute__((packed)) iwx_txq_cfg_v0 { u8 sta_id,tid; u16 flags; u32 cb_size; u64 byte_cnt_addr,tfdq_addr; };
struct __attribute__((packed)) iwx_txq_cfg_v3 { u32 operation; u32 sta_mask; u8 tid,reserved[3]; u32 flags,cb_size; u64 bc_dram_addr,tfdq_dram_addr; };
struct __attribute__((packed)) iwx_txq_cfg_rsp { u16 queue_number,flags,write_pointer,reserved; };
struct __attribute__((packed)) iwx_session_prot_cmd { u32 id_and_color,action,conf_id,duration_tu,repetition_count,interval; };
_Static_assert(sizeof(struct iwx_mac_ctx_cmd)==148,"mac ctx");
_Static_assert(sizeof(struct iwx_phy_ctxt_cmd)==28&&sizeof(struct iwx_phy_ctxt_cmd_uhb)==32,"phy ctx");
_Static_assert(sizeof(struct iwx_add_sta_key_cmd)==76&&sizeof(struct iwx_txq_cfg_v3)==36&&sizeof(struct iwx_rlc_cmd)==32,"cmd sizes");
static struct { int phy,mac,bind,sta,txq,ch; u8 bssid[6]; u8 me[6]; u16 seq; } iwx_lk;
static int iwx_cver(struct iwx_softc* sc,u8 g,u8 c){ return iwx_ver_call(iwx_cmd_version,sc,g,c,0); }
static int iwx_cmd_ok(struct iwx_softc* sc,u32 id,const void* d,u32 len){ return iwx_send_cmd_sync(sc,id,d,len,0,0,0,1000); }
static int iwx_phy_cmd(struct iwx_softc* sc,u32 act,int ch){
    int v=iwx_cver(sc,IWX_LONG_GROUP,IWX_PHY_CONTEXT_CMD);
    if(v!=3&&v!=4){ klog("iwx: firmware does not support phy-context-cmd v3/v4"); return -1; }
    u32 lmac=(ch<=14||!iwx_fw_capa(sc,IWX_UCODE_TLV_CAPA_CDB_SUPPORT))?0:1;
    u8 band=ch<=14?IWX_PHY_BAND_24:IWX_PHY_BAND_5;
    u32 rxc=0;
    if(iwx_cver(sc,IWX_DATA_PATH_GROUP,IWX_RLC_CONFIG_CMD)!=2){ u32 ra=iwx_nvm_d.valid_rx_ant?iwx_nvm_d.valid_rx_ant:3; rxc=(ra<<1)|(1u<<10)|(1u<<12); }
    if(iwx_fw_capa(sc,IWX_UCODE_TLV_CAPA_ULTRA_HB_CHANNELS)){
        struct iwx_phy_ctxt_cmd_uhb c; iwx_zero(&c,sizeof c); c.action=act; c.channel=(u32)ch; c.band=band; c.lmac_id=lmac; c.rxchain_info=rxc;
        return iwx_cmd_ok(sc,IWX_PHY_CONTEXT_CMD,&c,sizeof c);
    }
    struct iwx_phy_ctxt_cmd c; iwx_zero(&c,sizeof c); c.action=act; c.channel=(u8)ch; c.band=band; c.lmac_id=lmac; c.rxchain_info=rxc;
    return iwx_cmd_ok(sc,IWX_PHY_CONTEXT_CMD,&c,sizeof c);
}
static int iwx_rlc_cmd(struct iwx_softc* sc){
    if(iwx_cver(sc,IWX_DATA_PATH_GROUP,IWX_RLC_CONFIG_CMD)!=2)return 0;
    struct iwx_rlc_cmd c; iwx_zero(&c,sizeof c); u32 ra=iwx_nvm_d.valid_rx_ant?iwx_nvm_d.valid_rx_ant:3;
    c.rx_chain_info=(ra<<1)|(1u<<10)|(1u<<12);
    return iwx_cmd_ok(sc,IWX_WIDE_ID(IWX_DATA_PATH_GROUP,IWX_RLC_CONFIG_CMD),&c,sizeof c);
}
static int iwx_mac_cmd(struct iwx_softc* sc,u32 act,int assoc,u16 aid){
    static const u8 ecwmin[4]={4,4,3,2}, ecwmax[4]={10,10,4,3}, aifs[4]={3,7,2,2}, fifo[4]={1,0,2,3}; static const u16 txop[4]={0,0,94,47};
    struct iwx_mac_ctx_cmd c; iwx_zero(&c,sizeof c); c.action=act;
    if(act!=IWX_ACT_REMOVE){
        c.mac_type=5; iwx_cpy(c.node_addr,iwx_lk.me,6); iwx_cpy(c.bssid_addr,iwx_lk.bssid,6);
        c.cck_rates=iwx_lk.ch<=14?0x0f:0; c.ofdm_rates=0x15; c.short_slot=iwx_lk.ch>14?(1u<<4):0;
        for(int i=0;i<4;i++){ struct iwx_ac_qos* a=&c.ac[fifo[i]]; a->cw_min=(u16)((1u<<ecwmin[i])-1); a->cw_max=(u16)((1u<<ecwmax[i])-1); a->aifsn=aifs[i]; a->fifos_mask=(u8)(1u<<fifo[i]); a->edca_txop=(u16)(txop[i]*32); }
        c.filter_flags=(1u<<2)|(1u<<6);
        c.is_assoc=(u32)assoc; c.bi=100; c.dtim_interval=100; c.listen_interval=10; c.assoc_id=aid;
    }
    return iwx_cmd_ok(sc,IWX_MAC_CONTEXT_CMD,&c,sizeof c);
}
static int iwx_bind_cmd(struct iwx_softc* sc,u32 act){
    struct iwx_binding_cmd c; iwx_zero(&c,sizeof c); c.action=act; c.macs[0]=0; c.macs[1]=c.macs[2]=0xFFFFFFFFu;
    c.lmac_id=(iwx_lk.ch<=14||!iwx_fw_capa(sc,IWX_UCODE_TLV_CAPA_CDB_SUPPORT))?0:1;
    u32 st=0; int r=iwx_send_cmd_status(sc,IWX_BINDING_CONTEXT_CMD,&c,sizeof c,&st);
    return r<0?r:(st?-5:0);
}
static int iwx_sta_cmd(struct iwx_softc* sc,int update){
    struct iwx_add_sta_cmd c; iwx_zero(&c,sizeof c); c.add_modify=update?1:0; c.sta_id=0; c.station_type=0;
    if(!update)iwx_cpy(c.addr,iwx_lk.bssid,6);
    c.station_flags_msk=(3u<<26)|(3u<<28);
    u32 st=0; int r=iwx_send_cmd_status(sc,IWX_ADD_STA,&c,sizeof c,&st);
    return r<0?r:((st&0xFF)!=1?-5:0);
}
static int iwx_txq_cmd(struct iwx_softc* sc,int add){
    u64 dp=0,bp=0; int v=iwx_cver(sc,IWX_DATA_PATH_GROUP,IWX_SCD_QUEUE_CONFIG_CMD);
    if(!iwx_txq_info||!iwx_txq_reset)return -1;
    if(add){ iwx_txq_reset(sc,1); if(iwx_txq_info(sc,1,&dp,&bp)<0)return -1; }
    struct iwx_txq_cfg_rsp rsp; iwx_zero(&rsp,sizeof rsp); u32 rl=0; int r;
    if(v==0||v==99){
        if(!add)return 0;
        struct iwx_txq_cfg_v0 c; iwx_zero(&c,sizeof c); c.sta_id=0; c.tid=IWX_MGMT_TID; c.flags=1; c.cb_size=5; c.byte_cnt_addr=bp; c.tfdq_addr=dp;
        r=iwx_send_cmd_sync(sc,IWX_SCD_QUEUE_CFG,&c,sizeof c,&rsp,sizeof rsp,&rl,1000);
    } else if(v==3){
        struct iwx_txq_cfg_v3 c; iwx_zero(&c,sizeof c); c.operation=add?0:1; c.sta_mask=1; c.tid=IWX_MGMT_TID;
        if(add){ c.cb_size=5; c.bc_dram_addr=bp; c.tfdq_dram_addr=dp; }
        else return iwx_cmd_ok(sc,IWX_WIDE_ID(IWX_DATA_PATH_GROUP,IWX_SCD_QUEUE_CONFIG_CMD),&c,12);
        r=iwx_send_cmd_sync(sc,IWX_WIDE_ID(IWX_DATA_PATH_GROUP,IWX_SCD_QUEUE_CONFIG_CMD),&c,sizeof c,&rsp,sizeof rsp,&rl,1000);
    } else { klog("iwx: unsupported SCD_QUEUE_CFG command version"); return -1; }
    if(r<0)return r;
    if(rl!=sizeof rsp||rsp.queue_number!=1||rsp.write_pointer!=0){ klog("iwx: firmware gave an unexpected TX queue"); return -1; }
    return 0;
}
static int iwx_sp_cmd(struct iwx_softc* sc,u32 act,u32 tu){
    struct iwx_session_prot_cmd c; iwx_zero(&c,sizeof c); c.action=act; c.conf_id=0; c.duration_tu=tu; iwx_sp_state=0;
    return iwx_cmd_ok(sc,IWX_WIDE_ID(IWX_MAC_CONF_GROUP,IWX_SESSION_PROTECTION_CMD),&c,sizeof c);
}
static void iwx_teardown(struct iwx_softc* sc){
    if(iwx_set_ready)iwx_set_ready(sc,0);
    iwx_jwant=0; iwx_hwkeys=0;
    if(iwx_lk.txq){ iwx_txq_cmd(sc,0); iwx_lk.txq=0; }
    if(iwx_lk.sta){ u8 rm[4]={0,0,0,0}; iwx_cmd_ok(sc,IWX_REMOVE_STA,rm,4); iwx_lk.sta=0; }
    if(iwx_lk.bind){ iwx_bind_cmd(sc,IWX_ACT_REMOVE); iwx_lk.bind=0; }
    if(iwx_lk.mac){ iwx_mac_cmd(sc,IWX_ACT_REMOVE,0,0); iwx_lk.mac=0; }
    if(iwx_lk.phy){ iwx_phy_cmd(sc,IWX_ACT_REMOVE,iwx_lk.ch); iwx_lk.phy=0; }
}
static int iwx_mgmt_tx(struct iwx_softc* sc,u8 fc,const u8* body,u32 bl){
    u8 f[320]; if(bl>sizeof f-24)return -1;
    iwx_zero(f,24); f[0]=fc; iwx_cpy(f+4,iwx_lk.bssid,6); iwx_cpy(f+10,iwx_lk.me,6); iwx_cpy(f+16,iwx_lk.bssid,6);
    u16 sq=(u16)(iwx_lk.seq++<<4); f[22]=(u8)sq; f[23]=(u8)(sq>>8);
    iwx_cpy(f+24,body,bl);
    return iwx_tx_frame(sc,f,24+bl);
}
static int iwx_wait_st(struct iwx_softc* sc,volatile int* st,u32 ms){ u64 t0=iwx_now_ms(); while(*st<0&&iwx_now_ms()-t0<ms)iwx_rx_poll(sc); return *st; }
static void iwx_jfail(const char* m){ klog(m); }
int iwx_join_ssid(struct iwx_softc* sc,const u8* bssid,int ch,const char* ssid){
    if(!sc||!bssid||ch<=0||ch>196)return -1;
    if(iwx_use_mld&&iwx_use_mld(sc)){ static int s; if(!s){ s=1; klog("iwx: this card's firmware uses the MLD API, which is not ported yet; cannot join"); } return -1; }
    if(!iwx_txq_info||!iwx_set_ready){ static int s2; if(!s2){ s2=1; klog("iwx: iwx_pnvm.c is not built in; join is unavailable"); } return -1; }
    struct iwx_bss b; iwx_zero(&b,sizeof b); int have=iwx_scan_find(bssid,&b)==0;
    u8 sl=0; const u8* sp=0;
    if(ssid){ while(sl<32&&ssid[sl])sl++; sp=(const u8*)ssid; } else if(have){ sl=b.ssid_len; sp=b.ssid; }
    iwx_teardown(sc);
    iwx_cpy(iwx_lk.bssid,bssid,6); iwx_hw_mac(sc,iwx_lk.me); iwx_lk.ch=ch;
    if(iwx_phy_cmd(sc,IWX_ACT_ADD,ch)<0){ iwx_jfail("iwx: could not add PHY context"); return -1; }
    iwx_lk.phy=1;
    if(iwx_rlc_cmd(sc)<0){ iwx_jfail("iwx: could not configure RLC"); iwx_teardown(sc); return -1; }
    if(iwx_mac_cmd(sc,IWX_ACT_ADD,0,0)<0){ iwx_jfail("iwx: could not add MAC context"); iwx_teardown(sc); return -1; }
    iwx_lk.mac=1;
    if(iwx_bind_cmd(sc,IWX_ACT_ADD)<0){ iwx_jfail("iwx: could not add binding"); iwx_teardown(sc); return -1; }
    iwx_lk.bind=1;
    if(iwx_sta_cmd(sc,0)<0){ iwx_jfail("iwx: could not add station"); iwx_teardown(sc); return -1; }
    iwx_lk.sta=1;
    if(iwx_txq_cmd(sc,1)<0){ iwx_jfail("iwx: could not enable TX queue"); iwx_teardown(sc); return -1; }
    iwx_lk.txq=1; iwx_set_ready(sc,1);
    if(iwx_sp_cmd(sc,IWX_ACT_ADD,900)<0){ iwx_jfail("iwx: could not schedule session protection"); iwx_teardown(sc); return -1; }
    { u64 t0=iwx_now_ms(); while(!iwx_sp_state&&iwx_now_ms()-t0<500)iwx_rx_poll(sc); }
    iwx_cpy(iwx_jbss,bssid,6); iwx_jwant=1;
    int st=-1;
    for(int t=0;t<3&&st<0;t++){ static const u8 ab[6]={0,0,1,0,0,0}; iwx_auth_st=-1; if(iwx_mgmt_tx(sc,0xB0,ab,6)<0)break; st=iwx_wait_st(sc,&iwx_auth_st,300); }
    if(st<0){ iwx_jfail("iwx: no authentication reply"); iwx_teardown(sc); return 1; }
    if(st!=0){ iwx_jfail("iwx: authentication refused"); iwx_teardown(sc); return 2; }
    u8 body[120]; u32 n=0; u16 cap=1; if(have)cap|=(u16)(b.capab&0x0420);
    body[n++]=(u8)cap; body[n++]=(u8)(cap>>8); body[n++]=10; body[n++]=0;
    body[n++]=0; body[n++]=sl; for(u8 i=0;i<sl;i++)body[n++]=sp[i];
    if(ch>14){ static const u8 r5[]={1,8,0x8C,0x12,0x98,0x24,0xB0,0x48,0x60,0x6C}; for(u32 i=0;i<sizeof r5;i++)body[n++]=r5[i]; }
    else { static const u8 r2[]={1,8,0x82,0x84,0x8B,0x96,0x0C,0x12,0x18,0x24,50,4,0x30,0x48,0x60,0x6C}; for(u32 i=0;i<sizeof r2;i++)body[n++]=r2[i]; }
    st=-1;
    for(int t=0;t<3&&st<0;t++){ iwx_assoc_st=-1; if(iwx_mgmt_tx(sc,0x00,body,n)<0)break; st=iwx_wait_st(sc,&iwx_assoc_st,500); }
    iwx_jwant=0;
    if(st<0){ iwx_jfail("iwx: no association reply"); iwx_teardown(sc); return 3; }
    if(st!=0){ iwx_jfail("iwx: association refused"); iwx_teardown(sc); return 2; }
    if(iwx_sta_cmd(sc,1)<0||iwx_mac_cmd(sc,IWX_ACT_MODIFY,1,iwx_assoc_aid)<0){ iwx_jfail("iwx: could not mark the station associated"); iwx_teardown(sc); return -1; }
    klog("iwx: associated");
    return 0;
}
int iwx_join(struct iwx_softc* sc,const u8* bssid,int ch){ return iwx_join_ssid(sc,bssid,ch,0); }
void iwx_leave(struct iwx_softc* sc){
    if(!sc)return;
    if(iwx_lk.txq){ static const u8 r[2]={3,0}; iwx_mgmt_tx(sc,0xC0,r,2); u64 t0=iwx_now_ms(); while(iwx_now_ms()-t0<20)iwx_rx_poll(sc); }
    if(iwx_lk.mac)iwx_sp_cmd(sc,IWX_ACT_REMOVE,0);
    iwx_teardown(sc);
}
int iwx_set_key(struct iwx_softc* sc,int idx,int type,const u8* key,int len,int pairwise){
    if(!sc||!key||!iwx_lk.sta)return -1;
    if(type!=2||len!=16||idx<0||idx>3)return -1;
    struct iwx_add_sta_key_cmd c; iwx_zero(&c,sizeof c);
    c.sta_id=0; c.key_flags=(u16)(2u|(1u<<3)|(((u32)idx<<8)&(3u<<8)));
    if(pairwise)c.key_offset=0; else { c.key_offset=1; c.key_flags|=(u16)(1u<<14); }
    iwx_cpy(c.key,key,16);
    u32 st=0; int r=iwx_send_cmd_status(sc,IWX_ADD_STA_KEY,&c,sizeof c,&st);
    if(r<0||(st&0xFF)!=1){ klog("iwx: firmware refused the key"); return -1; }
    iwx_hwkeys=1; return 0;
}
