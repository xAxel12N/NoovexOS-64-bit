#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "include/nvx/base.h"
void* kmalloc(unsigned long n){ return malloc(n); }
void kfree(void* p){ free(p); }
#include "net/wifi/wifi_ampdu.c"
static int fails=0;
#define CHK(c) do{ if(!(c)){ printf("FAIL line %d: %s\n",__LINE__,#c); fails++; } }while(0)
static int got[256], ng;
static void drain(void){ u8 b[64]; u64 pn; int tid,r; while((r=ampdu_pop(b,sizeof b,&pn,&tid))>=0)got[ng++]=b[0]|(b[1]<<8); }
static void rx(int seq){ u8 f[4]; f[0]=(u8)seq; f[1]=(u8)(seq>>8); ampdu_rx(0,(u16)seq,f,4,0,0); drain(); }
int main(void){ CHK(g_ampdu_tx_on==1); g_ampdu_tx_on=1;
    ampdu_tx_reset(0); CHK(!ampdu_tx_want(0)); CHK(!ampdu_tx_want(AMPDU_TX_DELAY-1)); CHK(ampdu_tx_want(AMPDU_TX_DELAY));
    u8 t=ampdu_tx_sent(300); CHK(t==1); CHK(!ampdu_tx_want(301)); CHK(!ampdu_tx_active());
    CHK(ampdu_tx_resp((u8)(t+1),0,2|(64<<6),310)==0); CHK(!ampdu_tx_active());
    CHK(ampdu_tx_resp(t,0,2|(4<<2)|(64<<6),310)==0); CHK(!ampdu_tx_active());
    CHK(ampdu_tx_resp(t,0,2|(32<<6),310)==32); CHK(ampdu_tx_active()); CHK(ampdu_tx_win()==32); CHK(g_ampdu_tx_ok==1);
    CHK(ampdu_tx_seq()==0); CHK(ampdu_tx_seq()==1);
    ampdu_tx_peer_delba(400); CHK(!ampdu_tx_active()); CHK(g_ampdu_tx_fail==1);
    CHK(!ampdu_tx_want(401)); CHK(ampdu_tx_want(400+AMPDU_TX_RETRY));
    t=ampdu_tx_sent(4000); ampdu_tx_tick(4000+AMPDU_TX_WAIT-1); CHK(g_ampdu_tx_fail==1); ampdu_tx_tick(4000+AMPDU_TX_WAIT); CHK(g_ampdu_tx_fail==2); CHK(g_ampdu_tx_on);
    t=ampdu_tx_sent(8000); CHK(ampdu_tx_resp(t,37,0,8001)==0); CHK(g_ampdu_tx_fail==3); CHK(!g_ampdu_tx_on); CHK(g_ampdu_tx_auto); CHK(!ampdu_tx_want(100000));
    g_ampdu_tx_on=1; g_ampdu_tx_auto=0; ampdu_tx_reset(0); t=ampdu_tx_sent(AMPDU_TX_DELAY); CHK(ampdu_tx_resp(t,0,2,310)==AMPDU_WIN);
    ampdu_tx_peer_delba(310+AMPDU_TX_EARLY); CHK(g_ampdu_tx_fail==3); CHK(!ampdu_tx_active());
    t=ampdu_tx_sent(310+AMPDU_TX_EARLY+AMPDU_TX_RETRY); CHK(ampdu_tx_resp(t,0,0|(64<<6),5000)==0); CHK(g_ampdu_tx_fail==4);
    ampdu_tx_reset(0); t=ampdu_tx_sent(300); ampdu_tx_resp(t,0,2,301); CHK(ampdu_tx_stop()==1); CHK(!ampdu_tx_active()); CHK(ampdu_tx_stop()==0); CHK(!ampdu_tx_want(1000000));
    g_ampdu_tx_on=1; g_ampdu_tx_auto=0; g_ampdu_tx_hard=0; ampdu_tx_reset(0); t=ampdu_tx_sent(300); ampdu_tx_resp(t,0,2,301); u32 f0=g_ampdu_tx_fail;
    CHK(!ampdu_tx_watch(100,10,302)); CHK(!ampdu_tx_watch(200,10,302+AMPDU_TX_STALL-1)); CHK(!ampdu_tx_watch(140,11,400)); CHK(!ampdu_tx_watch(180,11,400+AMPDU_TX_STALL)); CHK(ampdu_tx_watch(191,11,400+AMPDU_TX_STALL));
    CHK(ampdu_tx_early(400)); CHK(!ampdu_tx_early(301+AMPDU_TX_LOSTWIN)); ampdu_tx_stop(); ampdu_tx_bad(1000); CHK(g_ampdu_tx_fail==f0+1); CHK(g_ampdu_tx_on); CHK(!ampdu_tx_watch(500,11,9000)); CHK(!ampdu_tx_early(1001));
    for(int i=0;i<2;i++){ ampdu_tx_reset(0); t=ampdu_tx_sent(300); ampdu_tx_resp(t,0,2,301); CHK(ampdu_tx_early(302)); ampdu_tx_stop(); ampdu_tx_bad(302); } CHK(!g_ampdu_tx_on); CHK(g_ampdu_tx_auto);
    ampdu_reset(); CHK(ampdu_bar(0,5)==0); ampdu_addba(0,10,0,0); ng=0;
    rx(12); rx(13); CHK(ng==0); CHK(ampdu_bar(0,12)==1); drain(); CHK(ng==2&&got[0]==12&&got[1]==13);
    rx(15); CHK(ampdu_bar(0,14)==0); drain(); CHK(ng==2); CHK(ampdu_bar(0,16)==1); drain(); CHK(ng==3&&got[2]==15);
    CHK(ampdu_bar(0,3)==0); CHK(ampdu_bar(0,16)==0); rx(16); CHK(ng==4&&got[3]==16);
    ampdu_reset(); ampdu_addba(0,4094,0,0); ng=0; rx(4095); rx(0); CHK(ampdu_bar(0,1)==1); drain(); CHK(ng==2&&got[0]==4095&&got[1]==0); rx(1); CHK(ng==3);
    printf(fails?"ampdu_tx: %d FAILED\n":"ampdu_tx: all passed\n",fails); return fails!=0; }
