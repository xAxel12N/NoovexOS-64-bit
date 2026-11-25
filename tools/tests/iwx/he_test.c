#include <stdio.h>
#include <string.h>
#include <assert.h>
#include "drivers/net/wifi/intel/iwx/mvm/iwx_tlc.c"
#include "drivers/net/wifi/intel/iwx/mvm/iwx_he.c"
struct iwx_softc { int x; };
static struct iwx_softc S;
static u8 lc[256]; static u32 lid,llen,stv=1; static int cv=3;
void klog(const char* s){ printf("klog: %s\n",s); }
struct iwx_softc* iwx_get_sc(void){ return &S; }
int iwx_cmd_version(struct iwx_softc* sc,u8 g,u8 c){ (void)sc; return g==IWX_DATA_PATH_GROUP&&c==IWX_STA_HE_CTXT_CMD?cv:0; }
volatile int iwx_in_rx; static int rec,depth,maxdepth,sends,reent,delba_t=-1,adelba_t=-1;
static void fake_rx_poll(void){ if(iwx_in_rx){ reent++; return; } iwx_in_rx=1; if(++depth>maxdepth)maxdepth=depth; if(depth<50)iwx_ba_rx(3,10,64,1); depth--; iwx_in_rx=0; }
void ampdu_delba(int t){ adelba_t=t; }
void wnd_delba_rx(int t){ delba_t=t; }
int iwx_send_cmd_status(struct iwx_softc* sc,u32 id,const void* d,u32 len,u32* st){ (void)sc; lid=id; llen=len; memcpy(lc,d,len); sends++; if(rec)fake_rx_poll(); *st=stv; return 0; }
static int tq_ret=0, tq_calls=0, tq_last=-1, tq_cur=0, at_on=0, dbt=0;
int iwx_txq_tid(struct iwx_softc* sc,int qid,int tid,int add){ (void)sc; tq_calls++; tq_last=add; return qid==2&&tid==0?tq_ret:-1; }
int iwx_txq_info(struct iwx_softc* sc,int qid,u64* d,u64* b){ (void)sc;(void)qid; *d=*b=0; return tq_cur; }
int ampdu_tx_active(void){ return at_on; }
void wnd_delba_tx(void){ dbt++; }
int iwx_send_cmd_sync(struct iwx_softc* sc,u32 id,const void* d,u32 len,void* r,u32 m,u32* rl,u32 t){ (void)sc;(void)r;(void)m;(void)rl;(void)t; lid=id; llen=len; memcpy(lc,d,len); return 0; }
int main(void){ assert(iwx_ba_on==1);
    u8 ie[64]; u32 n=0;
    ie[n++]=255; ie[n++]=1+6+11+8; ie[n++]=35; for(int i=0;i<6;i++)ie[n++]=0;
    u8 ph[11]={0x0C,0x20,0,0,0,0,0,0,0,0,0}; memcpy(ie+n,ph,11); n+=11;
    ie[n++]=0xFA; ie[n++]=0xFF; ie[n++]=0xFA; ie[n++]=0xFF; ie[n++]=0xFE; ie[n++]=0xFF; ie[n++]=0xFE; ie[n++]=0xFF;
    ie[n++]=255; ie[n++]=7; ie[n++]=36; ie[n++]=0xF4; ie[n++]=0x3F; ie[n++]=0; ie[n++]=0x2A; ie[n++]=0; ie[n++]=0;
    struct iwx_hepeer h; iwx_he_parse(ie,n,&h);
    assert(h.he&&h.ldpc&&h.c160&&h.mcs80==0xFFFA&&h.mcs160==0xFFFE&&h.color==0x2A&&!h.cdis&&h.rts==1023);
    u8 cap[40]; u32 cl=iwx_he_cap_ie(cap,36,1,1);
    assert(cl==2+1+6+11+8&&cap[0]==255&&cap[1]==cl-2&&cap[2]==35&&cap[9]==0x0C&&cap[10]==0x20&&cap[18]==0x80&&cap[20]==0xFA&&cap[21]==0xFF);
    struct iwx_hepeer r; iwx_he_parse(cap,cl,&r); assert(r.he&&r.c160&&r.mcs80==0xFFFA);
    assert(iwx_he_cap_ie(cap,6,0,0)==2+1+6+11+4&&cap[9]==0);
    u8 body[80]; for(int i=0;i<40;i++)body[i]=(u8)i; static const u8 wmm[9]={221,7,0x00,0x50,0xF2,0x02,0x00,0x01,0x00}; memcpy(body+40,wmm,9);
    u32 bn=iwx_he_insert(body,49,9,sizeof body,36,0,0); assert(bn==49+24&&body[40]==255&&body[42]==35&&body[64]==221&&body[72]==0);
    assert(iwx_he_insert(body,49,9,60,36,0,0)==49);
    struct iwx_peer p; memset(&p,0,sizeof p); p.ht=1; p.vht=1; p.vop=1; p.vw=1; p.vc0=42; p.vc1=50; p.htcap=2; p.hop=1; p.htop0=1; p.wmm=1; p.mcs[0]=0xFF; p.mcs[1]=0xFF; p.vht_rxmcs=0xFFFA;
    struct iwx_link l; iwx_link_pick(&p,36,3,3,&l); assert(l.width==2);
    assert(iwx_he_link160(&h,&p,36,&l)==1&&l.width==3&&l.ctrl_pos==3&&((l.sta_flags>>26)&3)==3);
    iwx_link_pick(&p,48,3,3,&l); assert(iwx_he_link160(&h,&p,48,&l)==1&&l.ctrl_pos==0);
    p.vc0=58; iwx_link_pick(&p,64,3,3,&l); assert(iwx_he_link160(&h,&p,64,&l)==1&&l.ctrl_pos==7); p.vc0=42;
    struct iwx_hepeer h80=h; h80.c160=0; iwx_link_pick(&p,36,3,3,&l); assert(iwx_he_link160(&h80,&p,36,&l)==0&&l.width==2);
    assert(iwx_he_rates(0xFFFA,1)==0xFFF&&iwx_he_rates(0xFFFA,2)==0xFFF&&iwx_he_rates(0xFFFE,2)==0&&iwx_he_rates(0xFFFD,1)==0x3FF);
    iwx_link_pick(&p,36,3,3,&l); iwx_he_link160(&h,&p,36,&l);
    struct iwx_tlc_config_cmd_v4 c4; assert(iwx_he_tlc_build(&p,&h,&l,3,4,&c4)==28);
    assert(c4.mode==IWX_TLC_MODE_HE&&c4.max_ch_width==3&&c4.ht_rates[0][0]==0xFFF&&c4.ht_rates[1][0]==0xFFF&&c4.ht_rates[0][1]==0xFFF&&c4.ht_rates[1][1]==0&&(c4.flags&IWX_TLC_CFG_LDPC));
    struct iwx_hepeer hn=h; hn.ldpc=0; struct iwx_tlc_config_cmd_v3 c3; iwx_he_tlc_build(&p,&hn,&l,3,3,&c3); assert(c3.mode==3&&c3.ht_rates[0][0]==0x3FF&&!(c3.flags&IWX_TLC_CFG_LDPC));
    struct iwx_hepeer nh; memset(&nh,0,sizeof nh); iwx_he_tlc_build(&p,&nh,&l,3,4,&c4); assert(c4.mode==IWX_TLC_MODE_VHT);
    u8 cx[96]; assert(iwx_he_ctxt_build(&h,1,cx)==80&&iwx_he_ctxt_build(&h,2,cx)==88);
    assert(iwx_he_ctxt_build(&h,3,cx)==92&&cx[1]==8&&cx[24]==7&&cx[43]==7&&cx[44]==0x2A&&cx[46]==0xFF&&cx[47]==3&&(cx[5]&0x20));
    assert(iwx_ba_rx(0,100,64,1)==1);
    iwx_he_nvm_note(IWX_NVM_SKU_11AX,36,IWX_NVM_CH_160); iwx_he_nvm_note(IWX_NVM_SKU_11AX,149,0);
    assert(iwx_he_ch160(40)==0&&iwx_he_ch160(36)==1&&iwx_he_ch160(149)==0&&iwx_he_allowed(&S));
    u8 bss[6]={1,2,3,4,5,6}; iwx_he_scan_note(bss,ie,n);
    struct iwx_hepeer hp; assert(iwx_he_prepare(&S,bss,36,1,&hp)==1&&hp.c160==1);
    assert(iwx_he_prepare(&S,bss,40,1,&hp)==1&&hp.c160==0);
    assert(iwx_he_prepare(&S,bss,36,0,&hp)==0);
    iwx_he_prepare(&S,bss,36,1,&hp); iwx_he_assoc_note(ie,n);
    iwx_link_pick(&p,36,3,3,&l); assert(iwx_he_assoc_done(&S,&hp,1,36,&p,&l)==1&&iwx_he_up&&l.width==3);
    assert(iwx_he_after_assoc(&S,&p,&l)==1&&lid==IWX_WIDE_ID(5,7)&&llen==92);
    assert(iwx_ba_rx(2,0x1234,128,1)==0&&lid==0x18&&llen==48&&lc[1+1+2+4+6+2+1]==IWX_STA_MODIFY_ADD_BA_TID&&lc[28]==2&&(lc[30]|(lc[31]<<8))==0x234&&(lc[44]|(lc[45]<<8))==64);
    assert(iwx_ba_rx(2,0,0,0)==0&&lc[29]==2&&lc[17]==IWX_STA_MODIFY_REMOVE_BA_TID);
    stv=0; for(int i=0;i<3;i++)assert(iwx_ba_rx(1,0,64,1)<0); assert(!iwx_ba_on&&iwx_ba_rx(1,0,64,1)<0); stv=1; iwx_ba_on=1;
    iwx_he_fail=0; iwx_he_pend=0; u8 b2[80]; memcpy(b2+40,wmm,9); iwx_he_insert(b2,49,9,sizeof b2,36,0,0);
    assert(iwx_he_prepare(&S,bss,36,1,&hp)==1&&iwx_he_fail==1); iwx_he_insert(b2,49,9,sizeof b2,36,0,0);
    assert(iwx_he_prepare(&S,bss,36,1,&hp)==0&&iwx_he_fail==2);
    iwx_ba_sta=1; rec=1; sends=0; maxdepth=0; fake_rx_poll(); assert(maxdepth==1&&sends==0&&iwx_baq_t-iwx_baq_h==1);
    rec=0; iwx_ba_drain(&S); assert(sends==1&&iwx_baq_t==iwx_baq_h&&lc[28]==3); iwx_in_rx=1; fake_rx_poll(); assert(reent==1&&iwx_baq_t-iwx_baq_h==0); iwx_in_rx=0;
    rec=1; iwx_in_rx=1; iwx_ba_rx(3,10,64,1); iwx_in_rx=0; sends=0; iwx_ba_drain(&S); assert(sends==8&&maxdepth==1&&iwx_baq_t-iwx_baq_h==1); rec=0; iwx_ba_drain(&S); assert(sends==9&&iwx_baq_t==iwx_baq_h);
    iwx_in_rx=1; for(int i=0;i<8;i++)assert(iwx_ba_rx(i,0,64,i&1)==0); assert(iwx_ba_rx(0,0,64,1)<0&&iwx_baq_drop==1); iwx_in_rx=0;
    sends=0; iwx_ba_drain(&S); assert(sends==8&&lc[28]==7);
    iwx_in_rx=1; iwx_ba_rx(5,0,64,1); iwx_in_rx=0; stv=0; iwx_ba_drain(&S); assert(delba_t==5&&adelba_t==5); stv=1; iwx_ba_on=1; iwx_ba_err=0;
    iwx_in_rx=1; iwx_ba_rx(4,0,64,1); iwx_in_rx=0; iwx_he_reset(); sends=0; iwx_ba_drain(&S); assert(sends==0);
    iwx_he_reset(); assert(!iwx_he_up&&iwx_ba_rx(0,0,64,1)==1);
    { iwx_he_assoc_done(&S,&iwx_he_cur,0,6,0,0); u8 f[40]; memset(f,0,40); f[0]=0x88; f[4]=0x02;
      assert(iwx_ba_tx(0,0,64,1)==IWX_BATX_WIN&&iwx_batx_q==2&&tq_last==1); assert(iwx_ba_tx(3,0,64,1)==0);
      at_on=0; assert(iwx_batx_route(&S,f,40)==1); at_on=1; assert(iwx_batx_route(&S,f,40)==2&&iwx_batx_frames==1);
      f[24]=5; assert(iwx_batx_route(&S,f,40)==1); f[24]=0; f[4]=0xff; assert(iwx_batx_route(&S,f,40)==1); f[4]=2; f[0]=0x08; assert(iwx_batx_route(&S,f,40)==1); f[0]=0x88;
      tq_cur=IWX_BATX_INFLIGHT; assert(iwx_batx_route(&S,f,40)==1&&iwx_batx_full==1); iwx_ba_drain(&S); assert(dbt==1&&!iwx_batx_on&&!iwx_batx_q&&tq_last==0);
      iwx_batx_on=1; tq_cur=0; assert(iwx_ba_tx(0,0,64,1)==IWX_BATX_WIN);
      u8 n[48]; memset(n,0,48); n[28]=1; n[30]=1; n[32]=2; n[34]=20; n[37]=0; iwx_batx_notif(n,40); assert(iwx_batx_notifs==1);
      tq_cur=20+IWX_BATX_INFLIGHT-1; assert(iwx_batx_route(&S,f,40)==2); tq_cur=20+IWX_BATX_INFLIGHT; assert(iwx_batx_route(&S,f,40)==-1&&iwx_batx_on);
      n[4]=1; n[34]=40; iwx_batx_notif(n,48); tq_cur=20+IWX_BATX_INFLIGHT; assert(iwx_batx_route(&S,f,40)==-1); n[4]=0; iwx_batx_notif(n,39); assert(iwx_batx_route(&S,f,40)==-1);
      n[28]=2; n[40]=2; n[42]=0x34; n[43]=0x12; n[45]=0; iwx_batx_notif(n,47); assert(iwx_batx_route(&S,f,40)==-1); iwx_batx_notif(n,48); tq_cur=0x1234+IWX_BATX_INFLIGHT-1; assert(iwx_batx_route(&S,f,40)==2); n[28]=1;
      int c0=tq_calls; assert(iwx_ba_tx(0,0,64,1)==IWX_BATX_WIN&&tq_calls==c0+2);
      iwx_in_rx=1; assert(iwx_ba_tx(0,0,64,1)==0); iwx_in_rx=0;
      iwx_batx_teardown(&S); tq_ret=-1; for(int i=0;i<3;i++)assert(iwx_ba_tx(0,0,64,1)<0); assert(!iwx_batx_on&&iwx_ba_tx(0,0,64,1)==0); tq_ret=0; iwx_batx_on=1;
      assert(iwx_ba_tx(0,0,8,1)==8); iwx_batx_teardown(&S); assert(!iwx_batx_q&&iwx_batx_route(&S,f,40)==1); iwx_he_reset(); assert(iwx_ba_tx(0,0,64,1)==0); }
    puts("iwx he: all tests passed"); return 0;
}
