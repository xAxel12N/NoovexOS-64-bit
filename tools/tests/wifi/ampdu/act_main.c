int g_wifi_assoc_ht=1;
static int fails=0;
#define CHK(c) do{ if(!(c)){ printf("FAIL line %d: %s\n",__LINE__,#c); fails++; } }while(0)
static const u8 AP[6]={2,0x11,0x22,0x33,0x44,0x55}, ME[6]={2,0xAA,0xBB,0xCC,0xDD,0xEE};
static int mk(u8* f,const u8* b,int bl,int prot,u64 pn){ f[0]=0xD0; f[1]=prot?0x40:0; f[2]=f[3]=0; memcpy(f+4,ME,6); memcpy(f+10,AP,6); memcpy(f+16,AP,6); f[22]=0x30; f[23]=0x01;
    if(!prot){ memcpy(f+24,b,(size_t)bl); return 24+bl; }
    f[24]=(u8)pn; f[25]=(u8)(pn>>8); f[26]=0; f[27]=0x20; f[28]=(u8)(pn>>16); f[29]=(u8)(pn>>24); f[30]=(u8)(pn>>32); f[31]=(u8)(pn>>40);
    u8 aad[22]; int al=0; aad[al++]=f[0]; aad[al++]=f[1]&0xC7; for(int i=0;i<18;i++)aad[al++]=f[4+i]; aad[al++]=f[22]&0x0F; aad[al++]=0;
    u8 n[13]; n[0]=0x10; memcpy(n+1,AP,6); for(int i=0;i<6;i++)n[7+i]=(u8)(pn>>(40-8*i)); memcpy(f+32,b,(size_t)bl); u8 mic[8]; aes_ccm_auth_encrypt(g_wnd_tk,n,aad,al,f+32,bl,mic); memcpy(f+32+bl,mic,8); return 32+bl+8; }
static int open_tx(int k,u8* body){ u8* f=g_txf[k]; int n=g_txl[k]; if(!(f[1]&0x40)){ memcpy(body,f+24,(size_t)(n-24)); return n-24; }
    u64 pn=(u64)f[24]|((u64)f[25]<<8)|((u64)f[28]<<16)|((u64)f[29]<<24)|((u64)f[30]<<32)|((u64)f[31]<<40);
    u8 aad[22]; int al=0; aad[al++]=f[0]; aad[al++]=f[1]&0xC7; for(int i=0;i<18;i++)aad[al++]=f[4+i]; aad[al++]=0; aad[al++]=0;
    u8 nn[13]; nn[0]=0x10; memcpy(nn+1,ME,6); for(int i=0;i<6;i++)nn[7+i]=(u8)(pn>>(40-8*i)); int dl=n-40; memcpy(body,f+32,(size_t)dl);
    return aes_ccm_decrypt_verify(g_wnd_tk,nn,aad,al,body,dl,f+32+dl)==1?dl:-1; }
int main(void){ g_ampdu_tx_on=1; setvbuf(stdout,0,_IONBF,0);
    memcpy(g_wnd_mac,ME,6); memcpy(g_wnd_bssid,AP,6); for(int i=0;i<16;i++)g_wnd_tk[i]=(u8)(i*7+1);
    u8 f[128], b[64]; int n;
    g_wnd_mmio=1; g_wnd_htp=0x17; g_wnd_ra_seen=1; ampdu_tx_reset(0); g_ticks=AMPDU_TX_DELAY; g_txn=0; wnd_batx();
    CHK(g_txn==1); n=open_tx(0,b); CHK(n==9&&b[0]==3&&b[1]==0&&b[2]==1&&(b[3]|(b[4]<<8))==(2|(64<<6))&&b[7]==0&&b[8]==0);
    u8 rsp[9]={3,1,1,0,0,(u8)(2|(32<<6)),(u8)((2|(32<<6))>>8),0,0};
    n=mk(f,rsp,9,0,0); wnd_rx_action(f,n); CHK(ampdu_tx_active()&&ampdu_tx_win()==32);
    ampdu_tx_reset(0); g_wpa_mfp=1; g_ticks=AMPDU_TX_DELAY; g_txn=0; wnd_batx(); CHK(g_txn==1&&(g_txf[0][1]&0x40)); n=open_tx(0,b); CHK(n==9&&b[1]==0&&b[2]==2);
    rsp[2]=2; n=mk(f,rsp,9,0,0); wnd_rx_action(f,n); CHK(!ampdu_tx_active());
    n=mk(f,rsp,9,1,5); f[40]^=1; wnd_rx_action(f,n); CHK(!ampdu_tx_active());
    n=mk(f,rsp,9,1,5); wnd_rx_action(f,n); CHK(ampdu_tx_active()); CHK(g_wnd_mpn==5);
    u8 dl[6]={3,2,0x00,0x00,37,0}; n=mk(f,dl,6,1,5); wnd_rx_action(f,n); CHK(ampdu_tx_active());
    n=mk(f,dl,6,1,6); g_ticks+=AMPDU_TX_EARLY; wnd_rx_action(f,n); CHK(!ampdu_tx_active());
    u8 rq[9]={3,0,9,(u8)(2|(0<<2)|(16<<6)),(u8)((2|(16<<6))>>8),0,0,0x50,0x00}; g_txn=0; n=mk(f,rq,9,1,7); wnd_rx_action(f,n);
    CHK(ampdu_active(0)); CHK(g_txn==1&&(g_txf[0][1]&0x40)); n=open_tx(0,b); CHK(n==9&&b[1]==1&&b[2]==9&&b[3]==0&&((b[5]|(b[6]<<8))>>6)==16);
    g_wifi_assoc_ht=0; g_txn=0; ampdu_tx_reset(0); g_ticks=AMPDU_TX_DELAY; wnd_batx(); CHK(g_txn==0); g_wifi_assoc_ht=1;
    g_txn=0; ampdu_tx_reset(0); g_ticks=AMPDU_TX_DELAY; wnd_batx(); u8 r2[9]={3,1,0,0,0,2,0,0,0}; r2[2]=at_tok; n=mk(f,r2,9,1,8); wnd_rx_action(f,n); CHK(ampdu_tx_active());
    { u32 tx0=g_wnd_tx_frames, rx0=g_wnd_rx_frames; wnd_batx(); g_wnd_tx_frames+=60; g_ticks+=AMPDU_TX_STALL; g_txn=0; wnd_batx(); CHK(!ampdu_tx_active()); CHK(g_txn==1); g_wnd_tx_frames=tx0; g_wnd_rx_frames=rx0; g_ticks+=AMPDU_TX_RETRY; }
    g_txn=0; ampdu_tx_reset(0); g_ticks=AMPDU_TX_DELAY; wnd_batx(); r2[2]=at_tok; n=mk(f,r2,9,1,9); wnd_rx_action(f,n); CHK(ampdu_tx_active());
    g_txn=0; wnd_delba_all(); CHK(g_txn==2); CHK(!ampdu_tx_active()&&!ampdu_active(0));
    n=open_tx(0,b); CHK(n==6&&b[1]==2&&(b[3]&0x08)&&b[4]==37); n=open_tx(1,b); CHK(n==6&&b[1]==2&&!(b[3]&0x08)&&(b[3]>>4)==0);
    g_wpa_mfp=0; n=mk(f,rq,9,1,10); g_txn=0; wnd_rx_action(f,n); CHK(g_txn==0);
    u8 bar[20]={0x84,0,0,0}; memcpy(bar+4,ME,6); memcpy(bar+10,AP,6); bar[16]=0x04; bar[17]=0x00; bar[18]=(u8)(100<<4); bar[19]=(u8)(100>>4);
    ampdu_reset(); ampdu_addba(0,90,0,0); { u8 d[4]={1,2,3,4}; ampdu_rx(0,95,d,4,0,0); } wnd_rx_bar(bar,20); { u8 o[64]; u64 pn; int t; CHK(ampdu_pop(o,64,&pn,&t)==4); }
    bar[16]=0x06; ampdu_addba(0,90,0,0); u32 bb=g_ampdu_bar; wnd_rx_bar(bar,20); CHK(g_ampdu_bar==bb);
    g_wpa_mfp=1; g_wnd_mpn=20; { u8 sq[4]={8,0,0x34,0x12}; g_txn=0; n=mk(f,sq,4,1,21); wnd_rx_action(f,n); CHK(g_txn==1&&(g_txf[0][1]&0x40)); n=open_tx(0,b); CHK(n==4&&b[0]==8&&b[1]==1&&b[2]==0x34&&b[3]==0x12);
      g_txn=0; n=mk(f,sq,4,0,0); wnd_rx_action(f,n); CHK(g_txn==0);
      g_wsq_on=1; g_wsq_id=0x5678; u8 sr[4]={8,1,0x78,0x56}; n=mk(f,sr,4,0,0); wnd_rx_action(f,n); CHK(g_wsq_on==1);
      sr[2]=0x79; n=mk(f,sr,4,1,22); wnd_rx_action(f,n); CHK(g_wsq_on==1);
      sr[2]=0x78; n=mk(f,sr,4,1,22); wnd_rx_action(f,n); CHK(g_wsq_on==1);
      n=mk(f,sr,4,1,23); wnd_rx_action(f,n); CHK(g_wsq_on==0&&g_wnd_saq_ok==1);
      g_wsq_on=1; g_wsq_id=0x1111; g_wsq_reason=7; g_wsq_t0=g_wsq_last=g_ticks; g_txn=0; g_ticks+=20; wnd_saq_tick(); CHK(g_txn==1&&g_wsq_on); n=open_tx(0,b); CHK(n==4&&b[0]==8&&b[1]==0&&b[2]==0x11);
      g_ticks+=80; wnd_saq_tick(); CHK(!g_wsq_on&&!g_wnd_up&&g_wnd_lost==7&&g_downs==1&&g_wnd_saq_lost==1); g_wnd_up=1;
      g_wsq_on=1; g_wsq_t0=g_wsq_last=g_ticks; u8 s2[4]={8,1,0x11,0x11}; n=mk(f,s2,4,1,30); wnd_rx_action(f,n); g_ticks+=200; wnd_saq_tick(); CHK(g_wnd_up&&g_downs==1);
      g_wpa_mfp=0; g_txn=0; n=mk(f,sq,4,0,0); wnd_rx_action(f,n); CHK(g_txn==0); }
    { g_wpa_mfp=0; g_wnd_up=1; g_wnd_mmio=0; g_gen=0; g_ampdu_tx_on=1; g_ampdu_tx_hard=0; ampdu_tx_reset(0); g_ticks=AMPDU_TX_DELAY+1000; g_txn=0; wnd_batx(); CHK(g_txn==0);
      g_gen=1; g_wnd_htp=0x17; g_wnd_ra_seen=0; g_batx_ret=0; g_batx_calls=0; wnd_batx(); CHK(g_txn==0&&g_batx_calls==1&&ampdu_tx_want(g_ticks));
      g_batx_ret=32; wnd_batx(); CHK(g_txn==1); n=open_tx(0,b); CHK(n==9&&b[1]==0&&((b[3]|(b[4]<<8))>>6)==32&&b[7]==0&&b[8]==0);
      u8 r3[9]={3,1,0,0,0,(u8)(2|(32<<6)),(u8)((2|(32<<6))>>8),0,0}; r3[2]=at_tok; n=mk(f,r3,9,0,0); wnd_rx_action(f,n); CHK(ampdu_tx_active()&&ampdu_tx_win()==32);
      ampdu_tx_reset(0); g_ticks+=AMPDU_TX_DELAY+10; g_batx_ret=-1; u32 hb=g_ampdu_tx_hard; g_txn=0; wnd_batx(); CHK(g_txn==0&&g_ampdu_tx_hard==hb+1); g_gen=0; g_wnd_mmio=1; }
    printf(fails?"act: %d FAILED\n":"act: all passed\n",fails); return fails!=0; }
