static int g_mic_bad, g_eapol;
int wpa_rx_eapol(const u8* eap,int el){ (void)eap; (void)el; g_eapol++; return 0; }
int wpa_rx_eapol_p(const u8* eap,int el,int prot){ (void)eap; (void)el; (void)prot; return 0; }
void ccmp_nonce(u8* n,const u8* a2,u64 pn,u8 prio){ (void)n; (void)a2; (void)pn; (void)prio; }
int aes_ccm_decrypt_verify(const u8* k,const u8* n,const u8* aad,int al,u8* d,int dl,const u8* mic){ (void)k; (void)n; (void)aad; (void)al; (void)d; (void)dl; (void)mic; return g_mic_bad?0:1; }
static int fails;
#define CHK(c) do{ if(!(c)){ printf("FAIL line %d: %s\n",__LINE__,#c); fails++; } }while(0)
static const u8 MAC[6]={2,0,0,0,0,1}, BSS[6]={2,0,0,0,0,9};
static int got[512], ng;
static int mk(u8* f,int seq,u64 pn,int amsdu,int prot,u16 et){ int n=0; f[n++]=0x88; f[n++]=prot?0x42:0x02; f[n++]=0; f[n++]=0;
    for(int i=0;i<6;i++){ f[4+i]=MAC[i]; f[10+i]=BSS[i]; f[16+i]=BSS[i]; } n=22; f[n++]=(u8)(seq<<4); f[n++]=(u8)(seq>>4); f[n++]=amsdu?0x80:0; f[n++]=0;
    if(prot){ f[n++]=(u8)pn; f[n++]=(u8)(pn>>8); f[n++]=0; f[n++]=0x20; for(int i=2;i<6;i++)f[n++]=(u8)(pn>>(8*i)); }
    if(amsdu){ for(int s=0;s<2;s++){ int st=n; for(int i=0;i<6;i++){ f[n++]=MAC[i]; } for(int i=0;i<6;i++)f[n++]=BSS[i]; f[n++]=0; f[n++]=12; f[n++]=0xAA; f[n++]=0xAA; f[n++]=3; f[n++]=0; f[n++]=0; f[n++]=0; f[n++]=8; f[n++]=0; f[n++]=(u8)seq; f[n++]=(u8)(seq>>8); f[n++]=(u8)s; f[n++]=0x55; while((n-st)&3)f[n++]=0; } }
    else { f[n++]=0xAA; f[n++]=0xAA; f[n++]=3; f[n++]=0; f[n++]=0; f[n++]=0; f[n++]=(u8)(et>>8); f[n++]=(u8)et; f[n++]=(u8)seq; f[n++]=(u8)(seq>>8); f[n++]=0; f[n++]=0x55; }
    if(prot)for(int i=0;i<8;i++)f[n++]=0; return n; }
static void take(int r,u8* o){ if(r>=16)got[ng++]=o[14]|(o[15]<<8); }
static void drain(void){ u8 o[2000]; int r; while((r=wnd_amq_pop(o,sizeof o))>0)take(r,o); while((r=wnd_ba_pop(o,sizeof o))>0){ take(r,o); while((r=wnd_amq_pop(o,sizeof o))>0)take(r,o); } }
static void rxf(int seq,u64 pn,int amsdu,int prot,u16 et){ u8 f[200], o[2000]; int n=mk(f,seq,pn,amsdu,prot,et); take(wnd_rx_frame(f,n,0,o,sizeof o),o); drain(); }
static void rx(int seq,u64 pn){ rxf(seq,pn,0,1,0x0800); }
static void setup(int open){ ampdu_reset(); memcpy(g_wnd_mac,MAC,6); memcpy(g_wnd_bssid,BSS,6); memset(g_wnd_rx_pn_uni,0,sizeof g_wnd_rx_pn_uni); g_wnd_open=open; g_amqn=0; g_mic_bad=0; g_eapol=0; ng=0; ampdu_addba(0,100,0,0); }
static int has(int v){ for(int i=0;i<ng;i++)if(got[i]==v)return 1; return 0; }
int main(void){
    setup(0); for(int s=100;s<104;s++)rx(s,s); CHK(ng==4&&got[0]==100&&got[3]==103);
    g_mic_bad=1; u32 d0=g_wnd_rx_drop; rx(103+200,1000); g_mic_bad=0; CHK(g_wnd_rx_drop==d0+1); for(int s=104;s<110;s++)rx(s,s); CHK(ng==10&&got[9]==109);
    rx(110,101); CHK(ng==10); rx(110,110); CHK(ng==11&&got[10]==110);
    rx(112,112); CHK(ng==11); rx(113,112); rx(112+300,112); rx(113,113); rx(111,111); CHK(ng==14&&got[11]==111&&got[12]==112&&got[13]==113);
    for(int s=114;s<120;s++)rx(s,s); CHK(ng==20&&got[19]==119);
    setup(0); for(int s=100;s<102;s++)rx(s,s); ng=0; rx(103,103); rxf(102,102,1,1,0); int a=ng; CHK(a==3); rxf(102,102,1,1,0); CHK(ng==a); rxf(104,104,1,1,0); CHK(ng==a+2); rxf(104,104,1,1,0); CHK(ng==a+2); rx(105,105); CHK(has(103)&&has(105));
    setup(0); rx(100,100); rxf(101,101,0,1,0x888E); CHK(g_eapol==1); rxf(101,101,0,1,0x888E); CHK(g_eapol==1); rx(102,102); CHK(ng==2&&got[1]==102);
    setup(0); rx(100,100); rx(102,102); rx(103,103); CHK(ng==1); ampdu_addba(0,110,0,0); drain(); CHK(ng==3&&got[1]==102&&got[2]==103); rx(110,110); CHK(ng==4&&got[3]==110);
    setup(1); rxf(100,0,0,0,0x0800); rxf(101,0,1,0,0); rxf(102,0,0,0,0x0800); CHK(ng==4&&got[3]==102&&has(101)); rxf(101,0,1,0,0); CHK(ng==4);
    setup(0); g_ticks=0; rx(100,100); g_mic_bad=1; rx(101,101); g_mic_bad=0; rx(102,102); CHK(ng==1); g_ticks=20; ampdu_tick(g_ticks); drain(); CHK(ng==2&&got[1]==102); rx(103,103); CHK(ng==3);
    setup(0); g_wnd_rx_uc=0; rx(100,100); rx(101,101); g_mic_bad=1; rx(102,102); g_mic_bad=0; CHK(g_wnd_rx_uc==2);
    { u8 f[200], o[2000]; int n=mk(f,103,103,0,1,0x0800); for(int i=0;i<6;i++)f[4+i]=0xFF; wnd_rx_frame(f,n,0,o,sizeof o); CHK(g_wnd_rx_uc==2); }
    setup(1); g_wnd_rx_uc=0; rxf(100,0,0,0,0x0800); rxf(101,0,1,0,0); CHK(g_wnd_rx_uc==2);
    printf(fails?"ba: %d FAILED\n":"ba: all ok\n",fails); return fails?1:0; }
