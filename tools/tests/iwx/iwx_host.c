#include <stdio.h>
#include <string.h>
#include <assert.h>
#include <stddef.h>
#include "drivers/net/wifi/intel/iwx/iwx_cmd.h"
#include "drivers/net/wifi/intel/iwx/mvm/iwx_tlc.h"
int g_wifi_ht_enable=1;
static int cver=0; int iwx_cmd_version(struct iwx_softc* sc,u8 g,u8 c){ (void)sc; return (g==IWX_DATA_PATH_GROUP&&c==IWX_TLC_MNG_CONFIG_CMD)?cver:0; }
static u8 last_tx[64];
struct iwx_softc { int ax; };
static struct iwx_softc S;
static u8 last_cmd[8192]; static u32 last_id,last_len; static int respond=1;
void klog(const char* s){ printf("klog: %s\n",s); }
struct iwx_softc* iwx_get_sc(void){ return &S; }
static u8 rxq[8192]; static u32 rxl;
int iwx_tx_hcmd(struct iwx_softc* sc,u32 id,const void* d,u32 len){ (void)sc; last_id=id; last_len=len; if(len<=sizeof last_cmd)memcpy(last_cmd,d,len);
    if(respond){ struct iwx_rx_packet* p=(void*)rxq; memset(rxq,0,64); p->hdr.code=id&0xff; p->hdr.flags=id>>8; p->len_n_flags=4+4; rxl=12; }
    return 0; }
int iwx_tx_data(struct iwx_softc* sc,int q,const void* t,u32 tl,const void* f,u32 fl){ (void)sc;(void)q;(void)f; if(tl<=sizeof last_tx)memcpy(last_tx,t,tl); return (int)(tl+fl); }
void iwx_rx_poll(struct iwx_softc* sc){ if(rxl){ u32 l=rxl; rxl=0; iwx_rx_pkt(sc,rxq,l); } }
int iwx_is_ax210(struct iwx_softc* sc){ return sc->ax; }
int iwx_fw_api(struct iwx_softc* sc,int b){ (void)sc; return b==IWX_UCODE_TLV_API_SCAN_EXT_CHAN_VER; }
int iwx_fw_capa(struct iwx_softc* sc,int b){ (void)sc;(void)b; return 0; }
void iwx_hw_mac(struct iwx_softc* sc,u8* m){ (void)sc; u8 a[6]={0x02,1,2,3,4,5}; memcpy(m,a,6); }
int wifi_hw_assoc_ie(u8* o,int c){ (void)o;(void)c; return 0; }
int g_wifi_scanning; int wifi_scan_parse(const u8* f,int l,u8 c,int r,int o){ (void)f;(void)l;(void)c;(void)r;(void)o; return 0; }
char* scopy(char* p,const char* s){ while(*s)*p++=*s++; *p=0; return p; }
char* udec(char* p,u32 v){ char t[12]; int n=0; do t[n++]=(char)(48+v%10); while(v/=10); while(n)*p++=t[--n]; *p=0; return p; }
static u64 clk; u64 iwx_now_ms(void){ return clk+=10; }
int main(void){
    assert(sizeof(struct iwx_rx_mpdu_desc)==64); assert(IWX_RX_DESC_SIZE_V1==48);
    assert(sizeof(struct iwx_nvm_get_info_rsp)==28+4*IWX_NUM_CHANNELS);
    assert(sizeof(struct iwx_tx_cmd_gen2)==20&&sizeof(struct iwx_tx_cmd_gen3)==28);
    assert(iwx_send_cmd_sync(&S,0x98,"abcd",4,0,0,0,100)==0);
    respond=0; assert(iwx_send_cmd_sync(&S,0x98,"abcd",4,0,0,0,100)==-3); respond=1;
    assert(iwx_scan_start(&S,0,0)==0); assert(last_id==0x10d&&last_len==sizeof(struct iwx_scan_req_umac_v14));
    assert(iwx_scan_busy());
    static u8 pk[600]; struct iwx_rx_packet* p=(void*)pk; memset(pk,0,sizeof pk);
    p->hdr.code=IWX_REPLY_RX_MPDU_CMD; p->hdr.qid=0x80;
    for(int ax=0;ax<2;ax++){
        S.ax=ax; u32 ds=ax?64:48; struct iwx_rx_mpdu_desc* d=(void*)p->data; memset(d,0,ds);
        d->status=3; if(ax)d->v3.energy_a=40; else d->v1.energy_a=40;
        u8* f=p->data+ds; memset(f,0,36); f[0]=0x80; memset(f+16,0xAA+ax,6);
        u8 ie[]={0,4,'t','e','s','t',3,1,6}; memcpy(f+36,ie,sizeof ie);
        d->mpdu_len=36+sizeof ie; p->len_n_flags=4+ds+d->mpdu_len;
        iwx_rx_pkt(&S,pk,8+ds+d->mpdu_len);
    }
    struct iwx_bss b[4]; int n=iwx_scan_results(b,4); assert(n==2);
    assert(!strcmp((char*)b[0].ssid,"test")&&b[0].channel==6&&b[0].rssi==-40);
    struct iwx_umac_scan_complete sc={0}; memset(pk,0,64); p->hdr.code=IWX_SCAN_COMPLETE_UMAC; p->hdr.qid=0x80; memcpy(p->data,&sc,sizeof sc); p->len_n_flags=4+sizeof sc;
    iwx_rx_pkt(&S,pk,8+sizeof sc); assert(!iwx_scan_busy());
    pk[0]=0; memset(pk,0xff,8); iwx_rx_pkt(&S,pk,8); iwx_rx_pkt(&S,pk,3);
    u8 fr[30]={0}; assert(iwx_tx_frame(&S,fr,30)==28+30);
    assert(offsetof(struct iwx_tlc_config_cmd_v3,non_ht_rates)==10&&offsetof(struct iwx_tlc_config_cmd_v3,max_mpdu_len)==20&&offsetof(struct iwx_tlc_config_cmd_v3,max_tx_op)==24);
    assert(offsetof(struct iwx_tlc_config_cmd_v4,sgi_ch_width_supp)==7&&offsetof(struct iwx_tlc_config_cmd_v4,ht_rates)==12&&offsetof(struct iwx_tlc_config_cmd_v4,max_mpdu_len)==24);
    static const u8 ies[]={1,8,0x8C,0x12,0x98,0x24,0xB0,0x48,0x60,0x6C,
        45,26,0x6F,0x01,0x17,0xFF,0xFF,0,0,0,0,0,0,0,0,0,0,1,0,0,0, 0,0,0,0,0,0,0,
        61,22,36,0x05,0x03,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
        191,12,0x20,0x01,0x80,0x03,0xFA,0xFF,0,0,0xFA,0xFF,0,0,
        192,5,1,42,0,0,0, 221,7,0x00,0x50,0xF2,0x02,0x01,0x01,0x00, 255,3,35,0,0, 7,2};
    struct iwx_peer pp; iwx_peer_parse(ies,sizeof ies,&pp);
    assert(pp.rates==0xFF0&&pp.ht&&pp.htcap==0x016F&&pp.ampdu==0x17&&pp.mcs[0]==0xFF&&pp.mcs[1]==0xFF);
    assert(pp.hop&&pp.htop0==5&&pp.htop1==3&&pp.vht&&pp.vht_rxmcs==0xFFFA&&((pp.vhtcap>>23)&7)==7&&pp.vop&&pp.vw==1&&pp.vc0==42&&pp.wmm&&pp.he);
    struct iwx_peer sp=pp; iwx_peer_merge(&pp,&sp,1,36); assert(pp.ht&&pp.vht&&!pp.he);
    struct iwx_link lk; iwx_link_pick(&pp,36,3,3,&lk);
    assert(lk.ht&&lk.vht&&lk.mimo&&lk.chains==2&&lk.width==2&&lk.ctrl_pos==1);
    assert((lk.sta_flags&(3u<<26))==(2u<<26)&&(lk.sta_flags&(1u<<28))&&((lk.sta_flags>>19)&0xF)==7&&((lk.sta_flags>>23)&7)==5);
    assert(lk.mac_qos==IWX_MAC_QOS_FLG_TGN&&lk.mac_prot==(IWX_MAC_PROT_FLG_HT_PROT|IWX_MAC_PROT_FLG_FAT_PROT));
    struct iwx_tlc_config_cmd_v3 c3; assert(iwx_tlc_build(&pp,&lk,3,3,&c3)==28);
    assert(c3.sta_id==0&&c3.mode==IWX_TLC_MODE_VHT&&c3.max_ch_width==2&&c3.chains==3&&c3.non_ht_rates==0xFF0&&c3.ht_rates[0][0]==0x3FF&&c3.ht_rates[1][0]==0x3FF&&c3.ht_rates[0][1]==0&&c3.max_mpdu_len==3895&&c3.flags==IWX_TLC_CFG_STBC&&c3.sgi_ch_width_supp==7);
    struct iwx_tlc_config_cmd_v4 c4; assert(iwx_tlc_build(&pp,&lk,3,4,&c4)==28);
    assert(c4.mode==2&&c4.max_ch_width==2&&c4.chains==3&&c4.sgi_ch_width_supp==7&&c4.ht_rates[1][0]==0x3FF&&c4.max_mpdu_len==3895&&c4.max_tx_op==0);
    iwx_link_pick(&pp,36,1,1,&lk); assert(!lk.mimo&&lk.chains==1&&!(lk.sta_flags&(1u<<28)));
    assert(iwx_tlc_build(&pp,&lk,1,3,&c3)==28&&c3.chains==1&&c3.ht_rates[1][0]==0&&c3.flags==0);
    struct iwx_peer p2=pp; p2.vw=0; iwx_link_pick(&p2,36,3,3,&lk); assert(lk.width==1&&lk.ctrl_pos==0);
    p2.htop0=7; iwx_link_pick(&p2,40,3,3,&lk); assert(lk.width==1&&lk.ctrl_pos==4);
    p2=pp; iwx_peer_merge(&p2,&sp,1,6); iwx_link_pick(&p2,6,3,3,&lk); assert(lk.ht&&!lk.vht&&lk.width==0);
    assert(iwx_tlc_build(&p2,&lk,3,3,&c3)==28&&c3.mode==IWX_TLC_MODE_HT&&c3.ht_rates[0][0]==0xFF&&c3.ht_rates[1][0]==0xFF&&c3.max_mpdu_len==3839&&c3.max_ch_width==0);
    p2=pp; iwx_peer_merge(&p2,&sp,0,36); iwx_link_pick(&p2,36,3,3,&lk); assert(!lk.ht&&lk.sta_flags==0&&lk.mac_qos==0);
    assert(iwx_tlc_build(&p2,&lk,3,3,&c3)==28&&c3.mode==0&&c3.chains==1&&c3.max_mpdu_len==2312&&c3.non_ht_rates==0xFF0);
    u8 ab[96]; u32 an=iwx_assoc_ht_ies(ab,&sp,36,3,3);
    assert(an==28+14+9&&ab[0]==45&&ab[1]==26&&ab[4]==0x17&&ab[5]==0xFF&&ab[6]==0xFF&&ab[17]==1&&(ab[2]&(1u<<1))&&ab[28]==191&&ab[29]==12&&ab[34]==0xFA&&ab[35]==0xFF&&ab[42]==221&&ab[47]==0x02);
    an=iwx_assoc_ht_ies(ab,&sp,6,1,1); assert(an==28+9&&!(ab[2]&(1u<<1))&&ab[6]==0&&ab[28]==221);
    p2=sp; p2.wmm=0; assert(iwx_assoc_ht_ies(ab,&p2,36,3,3)==0);
    cver=3; assert(iwx_tlc_send(&S,&pp,&lk)==0&&last_id==0x50f&&last_len==28);
    cver=4; assert(iwx_tlc_send(&S,&pp,&lk)==0&&last_id==0x50f&&last_len==28);
    cver=5; last_id=0; assert(iwx_tlc_send(&S,&pp,&lk)<0&&last_id==0); cver=0;
    struct iwx_tlc_update_notif un={0,{0},1,0x1234,0,0}; memset(pk,0,64); p->hdr.code=IWX_TLC_MNG_UPDATE_NOTIF; p->hdr.flags=IWX_DATA_PATH_GROUP; p->hdr.qid=0x80; memcpy(p->data,&un,sizeof un); p->len_n_flags=4+sizeof un;
    iwx_rx_pkt(&S,pk,8+sizeof un); assert(iwx_tlc_rate==0x1234);
    S.ax=0; u8 df[40]={0}; df[0]=0x08; df[1]=0x01; df[4]=0x02;
    iwx_tlc_on=1; assert(iwx_tx_frame(&S,df,40)==20+40);
    { struct iwx_tx_cmd_gen2 t; memcpy(&t,last_tx,sizeof t); assert(t.rate_n_flags==0&&!(t.flags&IWX_TX_FLAGS_CMD_RATE)&&(t.flags&IWX_TX_FLAGS_ENCRYPT_DIS)); }
    df[4]=0xFF; iwx_tx_frame(&S,df,40);
    { struct iwx_tx_cmd_gen2 t; memcpy(&t,last_tx,sizeof t); assert((t.flags&IWX_TX_FLAGS_CMD_RATE)&&t.rate_n_flags==(IWX_RATE_MCS_ANT_A_MSK|IWX_RATE_6M_PLCP)); }
    df[0]=0x00; df[4]=0x02; S.ax=1; iwx_tx_frame(&S,df,40);
    { struct iwx_tx_cmd_gen3 t; memcpy(&t,last_tx,sizeof t); assert((t.flags&IWX_TX_FLAGS_CMD_RATE)&&t.rate_n_flags==(IWX_RATE_MCS_ANT_A_MSK|IWX_RATE_6M_PLCP)); }
    iwx_tlc_on=0; df[0]=0x08; S.ax=0; iwx_tx_frame(&S,df,40);
    { struct iwx_tx_cmd_gen2 t; memcpy(&t,last_tx,sizeof t); assert(t.flags&IWX_TX_FLAGS_CMD_RATE); }
    printf("iwx host test OK\n"); return 0;
}
