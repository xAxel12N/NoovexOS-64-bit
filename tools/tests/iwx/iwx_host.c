#include <stdio.h>
#include <string.h>
#include <assert.h>
#include "drivers/net/wifi/iwx/iwx_cmd.h"
struct iwx_softc { int ax; };
static struct iwx_softc S;
static u8 last_cmd[8192]; static u32 last_id,last_len; static int respond=1;
void klog(const char* s){ printf("klog: %s\n",s); }
struct iwx_softc* iwx_get_sc(void){ return &S; }
static u8 rxq[8192]; static u32 rxl;
int iwx_tx_hcmd(struct iwx_softc* sc,u32 id,const void* d,u32 len){ (void)sc; last_id=id; last_len=len; if(len<=sizeof last_cmd)memcpy(last_cmd,d,len);
    if(respond){ struct iwx_rx_packet* p=(void*)rxq; memset(rxq,0,64); p->hdr.code=id&0xff; p->hdr.flags=id>>8; p->len_n_flags=4+4; rxl=12; }
    return 0; }
int iwx_tx_data(struct iwx_softc* sc,int q,const void* t,u32 tl,const void* f,u32 fl){ (void)sc;(void)q;(void)t;(void)f; return (int)(tl+fl); }
void iwx_rx_poll(struct iwx_softc* sc){ if(rxl){ u32 l=rxl; rxl=0; iwx_rx_pkt(sc,rxq,l); } }
int iwx_is_ax210(struct iwx_softc* sc){ return sc->ax; }
int iwx_fw_api(struct iwx_softc* sc,int b){ (void)sc; return b==IWX_UCODE_TLV_API_SCAN_EXT_CHAN_VER; }
int iwx_fw_capa(struct iwx_softc* sc,int b){ (void)sc;(void)b; return 0; }
void iwx_hw_mac(struct iwx_softc* sc,u8* m){ (void)sc; u8 a[6]={0x02,1,2,3,4,5}; memcpy(m,a,6); }
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
    printf("iwx host test OK\n"); return 0;
}
