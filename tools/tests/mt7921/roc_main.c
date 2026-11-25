#include <stdio.h>
#include <string.h>
#include "drivers/net/wifi/mediatek/mt7921/mt7921.h"
#include "drivers/net/wifi/mediatek/mt7921/mcu/mcu.h"
#include "drivers/net/wifi/mediatek/mt7921/mcu/rate.h"

struct mt7921_softc g_mt7921;
void (*mt7921_rx_deliver)(const u8* frame,u32 len,int rssi);
static u64 now; static int fail_n, grant_mode, nfreed, freed[64], ncmd; static u8 last[512]; static u32 last_len, last_cid;
#define CHECK(c) do{ if(!(c)){ printf("FAIL %s:%d %s\n",__FILE__,__LINE__,#c); fail_n++; } }while(0)
u64 hr_ns(void){ now+=1000000; return now; }
int mt7921_tx_token_release(struct mt7921_softc* sc,u32 id){ (void)sc; if(nfreed<64)freed[nfreed++]=(int)id; return 0; }
int mt7921_dma_tx(struct mt7921_softc* sc,int qid,const u8* buf,u32 len){
    (void)sc; if(qid!=MT_Q_MCU)return 0;
    ncmd++; last_len=len>sizeof last?sizeof last:len; memcpy(last,buf,last_len);
    last_cid=(buf[4+32+2-4]|0);
    return 0;
}
static int replay,depth,maxdepth,rx_st; static u8 rf[128]; static u32 rfl;
static void replay_poll(struct mt7921_softc* sc){
    depth++; if(depth>maxdepth)maxdepth=depth;
    if(depth<6)mt7921_rx_data(sc,rf,rfl);
    const struct mt_mcu_uni_txd* u=(const struct mt_mcu_uni_txd*)last;
    u8 ev[64]; memset(ev,0,sizeof ev); u32 d0=sizeof ev|((u32)PKT_TYPE_RX_EVENT<<27); memcpy(ev,&d0,4);
    struct mt_mcu_rxd* r=(struct mt_mcu_rxd*)ev; r->eid=1; r->seq=u->seq; u8* p=ev+sizeof(struct mt_mcu_rxd); p[0]=(u8)u->cid; mt_put32(p+4,(u32)rx_st);
    mt7921_mcu_rx_event(sc,ev,sizeof ev); depth--;
}
static u32 mkba(const struct mt7921_softc* sc,const u8* a,u32 n){
    memset(rf,0,sizeof rf); u32 l=24+24+n; mt_put32(rf,l|((u32)PKT_TYPE_NORMAL<<27)); rf[24]=0xD0; memcpy(rf+24+4,sc->mac,6); memcpy(rf+24+10,sc->bssid,6); memcpy(rf+24+16,sc->bssid,6); memcpy(rf+48,a,n); return rfl=l;
}
void mt7921_rx_poll(struct mt7921_softc* sc){
    if(replay){ replay_poll(sc); return; }
    const struct mt_mcu_uni_txd* u=(const struct mt_mcu_uni_txd*)last;
    if(!grant_mode||u->cid!=MCU_UNI_CMD_ROC||last[48+4]!=UNI_ROC_ACQUIRE)return;
    u8 ev[36+4+20]; memset(ev,0,sizeof ev);
    u32 d0=sizeof ev|((u32)PKT_TYPE_RX_EVENT<<27); memcpy(ev,&d0,4);
    struct mt_mcu_rxd* r=(struct mt_mcu_rxd*)ev; r->eid=MCU_UNI_EVENT_ROC; r->option=grant_mode==2?0:MCU_UNI_UNSOLICITED;
    u8* g=ev+36+4; g[0]=UNI_EVENT_ROC_GRANT; g[5]=last[48+4+5]; g[6]=0; g[7]=last[48+4+6];
    mt7921_mcu_rx_event(sc,ev,sizeof ev);
}
static const char* cc="DE";
int regdb_ch_allowed(u8 ch){ return ch!=13&&ch!=165; }
int regdb_is_dfs(u8 ch){ return ch>=52&&ch<=140; }
int regdb_no_ir(u8 ch){ return ch==144; }
const char* regdb_country(void){ return cc; }

int main(void){
    struct mt7921_softc* sc=&g_mt7921; sc->state=MT_ST_READY; sc->bss_idx=0;
    grant_mode=1; CHECK(mt7921_roc_acquire(sc,36,3000,MT_ROC_REQ_JOIN)==0&&sc->roc_active&&sc->roc_granted&&sc->roc_grants==1);
    const u8* t=last+48+4;
    CHECK(((const struct mt_mcu_uni_txd*)last)->cid==MCU_UNI_CMD_ROC&&last_len==48+28&&t[0]==UNI_ROC_ACQUIRE&&t[2]==24&&t[6]==36&&t[8]==2&&t[10]==36&&t[13]==36&&t[15]==MT_ROC_REQ_JOIN&&mt_get32(t+16)==3000&&t[20]==0xff);
    u8 tok=t[5]; mt7921_roc_abort(sc);
    CHECK(!sc->roc_active&&last_len==48+16&&t[0]==UNI_ROC_ABORT&&t[2]==12&&t[5]==tok&&t[6]==0xff);
    int n0=ncmd; mt7921_roc_abort(sc); CHECK(ncmd==n0);
    CHECK(mt7921_roc_acquire(sc,6,1000,MT_ROC_REQ_ROC)==0&&t[8]==1&&t[5]==(u8)(tok+1));
    mt7921_roc_abort(sc);
    grant_mode=2; CHECK(mt7921_roc_acquire(sc,6,1000,MT_ROC_REQ_JOIN)<0&&sc->roc_timeouts==1&&!sc->roc_active&&mt7921_roc_enable);
    grant_mode=0; mt7921_roc_acquire(sc,6,1000,0); mt7921_roc_acquire(sc,6,1000,0); CHECK(!mt7921_roc_enable&&sc->roc_timeouts==3);
    n0=ncmd; CHECK(mt7921_roc_acquire(sc,6,1000,0)<0&&ncmd==n0); mt7921_roc_enable=1; sc->roc_fails=0;
    { u8 ev[36+8]; memset(ev,0,sizeof ev); u32 d0=sizeof ev|((u32)PKT_TYPE_RX_EVENT<<27); memcpy(ev,&d0,4); ((struct mt_mcu_rxd*)ev)->eid=MCU_UNI_EVENT_ROC; ((struct mt_mcu_rxd*)ev)->option=MCU_UNI_UNSOLICITED; sc->roc_active=1; mt7921_mcu_rx_event(sc,ev,sizeof ev); CHECK(!sc->roc_granted); sc->roc_active=0; }
    { u8 f[8+4*6]; memset(f,0,sizeof f); u32 d0=sizeof f|(4u<<16)|((u32)PKT_TYPE_TXRX_NOTIFY<<27); memcpy(f,&d0,4);
      u32 inf[6]={(1u<<31)|(1u<<14),(5u<<16)|1u,(7u<<16)|(1u<<13),(1u<<31),(0x7fffu<<16),(9u<<16)};
      memcpy(f+8,inf,sizeof inf); mt7921_mcu_rx_event(sc,f,sizeof f);
      CHECK(nfreed==4&&freed[0]==5&&freed[1]==7&&freed[2]==0x7fff&&freed[3]==9&&sc->txfree_events==1);
      nfreed=0; mt7921_rx_data(sc,f,sizeof f); CHECK(nfreed==4&&sc->txfree_events==2);
      nfreed=0; d0=(sizeof f-8)|(60u<<16)|((u32)PKT_TYPE_TXRX_NOTIFY<<27); memcpy(f,&d0,4); mt7921_rx_data(sc,f,sizeof f); CHECK(nfreed==2); mt7921_rx_data(sc,f,4); nfreed=0; d0=12|(1u<<16)|((u32)PKT_TYPE_TXRX_NOTIFY<<27); memcpy(f,&d0,4); inf[0]=3u<<16; memcpy(f+8,inf,4); mt7921_rx_data(sc,f,12); CHECK(nfreed==1&&freed[0]==3); }
    { u8 b[12+8*38]; u32 n=mt_chan_domain_build(b,sizeof b); CHECK(n==12+8u*(12+24)&&b[0]=='D'&&b[1]=='E'&&b[8]==12&&b[9]==24&&b[5]==3);
      int saw52=0,saw36=0,saw144=0,saw165=0;
      for(u32 o=12;o<n;o+=8){ u16 ch=mt_get16(b+o); u32 fl=mt_get32(b+o+4); if(ch==52)saw52=fl==(MT_CHAN_RADAR|MT_CHAN_NO_IR); if(ch==36)saw36=fl==0; if(ch==144)saw144=fl==MT_CHAN_NO_IR; if(ch==165)saw165=1; }
      CHECK(saw52&&saw36&&saw144&&!saw165); CHECK(mt_chan_domain_build(b,20)==0);
      ncmd=0; CHECK(mt_mcu_set_chan_domain(sc)==0&&ncmd==1); CHECK(mt_mcu_chan_domain_sync(sc)==0&&ncmd==1); cc="SE"; mt_mcu_chan_domain_sync(sc); CHECK(ncmd==2); }
    { memcpy(sc->bssid,"\x02\x11\x22\x33\x44\x55",6); memcpy(sc->mac,"\x00\x0c\xe7\x01\x02\x03",6); sc->connected=1; sc->bss_idx=0;
      CHECK(mt_ba_on&&mt_batx_on);
      mt_ba[0].tx_st=1; mt_ba[0].tok=9; mt_ba[0].tx_ssn=5; mt_ba_tx_sess=0;
      u8 a[9]={3,1,9,0,0,(u8)(2|(32<<6)),(u8)((2|(32<<6))>>8),0,0}; mkba(sc,a,9);
      replay=1; maxdepth=0; rx_st=0; ncmd=0;
      mt7921_rx_data(sc,rf,rfl); CHECK(ncmd==0&&mt_ba_pending()==1&&mt_ba[0].tx_st==2&&mt_ba_tx_sess==1&&!mt_ba_in_rx);
      mt_ba_drain(sc); CHECK(ncmd==2&&maxdepth==1&&mt_ba_pending()==0&&mt_ba[0].tx_st==2&&mt_ba_tx_sess==1);
      mt_ba[3].rx_on=1; mt_ba_rx_sess=1; u16 dp=(u16)(0x0800|(3<<12)); u8 d[6]={3,2,(u8)dp,(u8)(dp>>8),37,0}; mkba(sc,d,6);
      ncmd=0; maxdepth=0; mt7921_rx_data(sc,rf,rfl); CHECK(ncmd==0&&!mt_ba[3].rx_on&&mt_ba_rx_sess==0&&mt_ba_pending()==1);
      mt_ba_drain(sc); CHECK(ncmd==2&&maxdepth==1&&mt_ba_pending()==0);
      mt_ba[1].tx_st=1; mt_ba[1].tok=10; mt_ba[1].tx_fail=0; u8 b1[9]={3,1,10,0,0,(u8)(2|(1<<2)|(64<<6)),(u8)((2|(1<<2)|(64<<6))>>8),0,0}; mkba(sc,b1,9);
      mt7921_rx_data(sc,rf,rfl); CHECK(mt_ba[1].tx_st==2&&mt_ba_tx_sess==2);
      rx_st=1; ncmd=0; maxdepth=0; mt_ba_drain(sc); CHECK(maxdepth==1&&mt_ba[1].tx_st==0&&mt_ba[1].tx_fail==1&&mt_ba_tx_sess==1&&mt_ba_on);
      rx_st=0; replay=0; mt_ba_fail=0; }
    { static u8 rq[MT_SCAN_REQ_LEN]; u8 cl[4]={6,36,MT7921_VCH6,MT7921_VCH6+57}; CHECK(mt_scan_req_build(sc,cl,4,rq,sizeof rq)==MT_SCAN_REQ_LEN&&rq[159]==4&&rq[160]==1&&rq[161]==6&&rq[162]==2&&rq[163]==36&&rq[164]==3&&rq[165]==1&&rq[166]==3&&rq[167]==229);
      u8 bb[MT_REQ_MAX]; u8 ap[6]={1,2,3,4,5,6}; sc->band6=1; CHECK(mt_bss_req_build(sc,ap,37,1,bb)==52&&bb[4+25]==0xb1&&bb[4+30]==1&&bb[36+13]==3);
      sc->band6=0; CHECK(mt_bss_req_build(sc,ap,36,1,bb)==52&&bb[4+25]==0x31&&bb[4+30]==0&&bb[36+13]==2);
      u8 ac[80]; sc->band6=1; int an=mt_assoc_caps(sc,37,ac); CHECK(an==29&&ac[0]==255&&ac[2]==35&&ac[3+6]==0x04&&ac[24]==255&&ac[26]==59); mt_he_on=0; CHECK(mt_assoc_caps(sc,37,ac)==0); mt_he_on=1; sc->band6=0; }
    printf(fail_n?"mt7921 roc/txfree: %d failed\n":"mt7921 roc/txfree: all passed\n",fail_n);
    return fail_n?1:0;
}
