#include <stdio.h>
#include <string.h>
#include "net/bluetooth/conn.c"
#include "net/bluetooth/l2cap.c"
#include "net/bluetooth/profiles/rfcomm.c"
#include "net/bluetooth/profiles/hfp.c"
int bt_srv_enabled(void){ return 1; }
static int fails, checks;
#define CHECK(c,m) do{ checks++; if(c)printf("  %s ok\n",m); else { printf("  FAIL %s\n",m); fails++; } }while(0)
struct nvx_snd g_snd; int g_snd_state=1, g_snd_vol=80;
static int hda_rs(void){ return 7; }
volatile u32 g_ml_beat; static u64 now; static int bonded=1;
btkey_t* bt_keys_find(const u8* a,int le){ static btkey_t k; (void)a; return bonded&&!le?&k:0; }
int snd_init(void){ return 1; }
void ui_pump(void){}
u64 hr_ns(void){ return now+=100000; }
void hr_delay_us(u32 us){ now+=(u64)us*1000; }
void klog(const char* s){ printf("    log: %s\n",s); }
void klog2(const char* a,const char* b){ printf("    log: %s%s\n",a,b); }
static int fg_fails, fg_oks;
void fg_boot(struct featguard* g){ (void)g; } int fg_begin(struct featguard* g){ return g->on&&!g->dead; } void fg_ok(struct featguard* g){ (void)g; fg_oks++; } void fg_fail(struct featguard* g,const char* w){ (void)g; (void)w; fg_fails++; } void fg_enable(struct featguard* g,int on){ g->on=on; }
char* scopy(char* p,const char* s){ while(*s)*p++=*s++; *p=0; return p; }
char* udec(char* p,u32 v){ char t[12]; int n=0; do{ t[n++]=(char)('0'+v%10); v/=10; }while(v); while(n)*p++=t[--n]; *p=0; return p; }
char* hx(char* p,u32 v,int d){ for(int i=d-1;i>=0;i--)*p++="0123456789ABCDEF"[(v>>(i*4))&15]; *p=0; return p; }
static u8 evq[16][64]; static int evn[16], evh, evt;
static void ev(const u8* e,int n){ memcpy(evq[evt],e,n); evn[evt]=n; evt=(evt+1)&15; }
int bt_hci_up(void){ return 1; } int bt_hci_le(void){ return 1; } int bt_hci_bredr(void){ return 1; }
static u16 last_op; static u8 last_cmd[32]; static int last_n;
int bt_hci_send(u16 op,const u8* p,int n){ last_op=op; last_n=n<32?n:32; memcpy(last_cmd,p,last_n); return 0; }
int bt_hci_cmd(u16 op,const u8* p,int n,u8* rp,int rcap,int ms){ (void)p; (void)n; (void)rcap; (void)ms;
    if(op==0x1005){ u8 r[8]={0,0xFD,0x03,0,8,0,0,0}; memcpy(rp,r,8); return 8; } rp[0]=0; return 1; }
int bt_hci_wait_evt(int code,int sub,u8* o,int cap,int ms){ (void)code; (void)sub; (void)o; (void)cap; (void)ms; bt_poll(); return -1; }
static u8 pend[64][300]; static int pendn[64], ph, pt;
static void l2rx(u16 cid,const u8* d,int n){ u8* b=pend[pt]; pendn[pt]=n+8; pt=(pt+1)&63; b[0]=0x42; b[1]=0x20; b[2]=(u8)(n+4); b[3]=(u8)((n+4)>>8); b[4]=(u8)n; b[5]=(u8)(n>>8); b[6]=(u8)cid; b[7]=(u8)(cid>>8); memcpy(b+8,d,n); }
void bt_poll(void){ while(evh!=evt){ int k=evh; evh=(evh+1)&15; bt_host_event(evq[k],evn[k]); } while(ph!=pt){ int k=ph; ph=(ph+1)&63; bt_acl_rx(pend[k],pendn[k]); } }
static u16 pcid=0x70, rem[8], loc[8], psmof[8]; static int nch;
static int p_cfc, p_cr, p_dlc, p_msc, p_step, p_bad, p_frames, refuse_dlc; static char p_got[2048]; static int p_gotn;
static const char* hf_cmds0[]={"AT+BRSF=16\r","AT+CIND=?\r","AT+CIND?\r","AT+CMER=3,0,0,1\r","AT+VGS=11\r",0};
static const char* hf_cmds1[]={"AT+BRSF=144\r","AT+BAC=1,2\r","AT+CIND=?\r","AT+CIND?\r","AT+CMER=3,0,0,1\r",0};
static const char** hf_cmds=hf_cmds0;
static void p_rfc(u16 to,u8 dlci,int cr,u8 ctl,int credits,const u8* d,int n){ u8 b[300]; int k=rfc_build(dlci,cr,ctl,credits,d,n,b,(int)sizeof b); l2rx(to,b,k); }
static void p_send_at(u16 to){ const char* s=hf_cmds[p_step]; if(!s)return; p_step++; int g=p_cr?-1:7; if(!p_cr)p_cr=1; p_rfc(to,10,0,RFC_UIH,g,(const u8*)s,(int)strlen(s)); }
static void peer_rfcomm(u16 to,const u8* s,int n){
    rfcframe_t f; if(!rfc_parse(s,n,p_cfc,&f)){ p_bad++; return; } p_frames++;
    if(f.dlci==0&&f.type==RFC_SABM){ if(!f.cr||!f.pf)p_bad++; p_rfc(to,0,1,RFC_UA|RFC_PF,-1,0,0); return; }
    if(f.dlci==0&&f.type==RFC_UIH&&f.len>=2){ const u8* m=f.info; u8 t=m[0]&0xFC;
        if(t==0x80&&f.len>=10){ u8 r[10]={0x81,0x11,m[2],0xE0,0,0,0x7F,0,0,7}; if(m[3]!=0xF0)p_bad++; p_cfc=1; p_rfc(to,0,1,RFC_UIH,-1,r,10); }
        else if(t==0xE0&&(m[0]&2)){ u8 r[4]={0xE1,0x05,m[2],m[3]}; p_rfc(to,0,1,RFC_UIH,-1,r,4); p_rfc(to,0,0,RFC_UIH,-1,(u8[]){0xE3,0x05,(u8)((10<<2)|3),0x8D},4); p_msc=1; p_send_at(to); }
        return; }
    if(f.dlci==10&&f.type==RFC_SABM){ if(refuse_dlc){ p_rfc(to,10,1,RFC_DM|RFC_PF,-1,0,0); return; } p_dlc=1; p_rfc(to,10,1,RFC_UA|RFC_PF,-1,0,0); return; }
    if(f.dlci==10&&f.type==RFC_UIH){ int o0=p_gotn;
        if(f.len&&p_gotn+f.len<(int)sizeof p_got){ memcpy(p_got+p_gotn,f.info,f.len); p_gotn+=f.len; p_got[p_gotn]=0; }
        if(f.len&&strstr(p_got+o0,"+BCS: 2")){ p_rfc(to,10,0,RFC_UIH,-1,(const u8*)"AT+BCS=2\r",9); }
        else if(f.len&&(strstr(p_got+o0,"OK")||strstr(p_got+o0,"ERROR"))){ p_send_at(to); }
        return; }
    if(f.type==RFC_DISC)p_rfc(to,f.dlci,1,RFC_UA|RFC_PF,-1,0,0);
}
static void peer_l2(u16 cid,const u8* s,int n){
    if(cid==1){
        if(s[0]==0x02){ u16 psm=s[4]|(s[5]<<8), sc=s[6]|(s[7]<<8), my=pcid++; rem[nch]=my; loc[nch]=sc; psmof[nch]=psm; nch=(nch+1)&7;
            u8 r[12]={0x03,s[1],8,0,(u8)my,(u8)(my>>8),(u8)sc,(u8)(sc>>8),0,0,0,0}; l2rx(1,r,12); u8 c[12]={0x04,0x77,8,0,(u8)sc,(u8)(sc>>8),0,0,0x01,2,0x9F,0x02}; l2rx(1,c,12); }
        else if(s[0]==0x04){ u16 d=s[4]|(s[5]<<8), lcid=0; for(int k=0;k<BT_MAX_CHAN;k++) if(lc[k].state!=L2_CLOSED&&lc[k].rcid==d)lcid=lc[k].lcid; u8 r[10]={0x05,s[1],6,0,(u8)lcid,(u8)(lcid>>8),0,0,0,0}; l2rx(1,r,10); }
        else if(s[0]==0x06){ u8 r[8]={0x07,s[1],4,0,s[4],s[5],s[6],s[7]}; l2rx(1,r,8); }
        return; }
    for(int k=0;k<8;k++) if(rem[k]==cid&&cid){
        if(psmof[k]==1){ u8 r[]={0x07,s[1],s[2],0,20,0,17, 0x35,15,0x35,13,0x09,0x00,0x04,0x35,8,0x35,3,0x19,0x01,0x00,0x35,3,0x19,0x00,0x03,0x08,5,0}; r[4]=(u8)(sizeof r-5); l2rx(loc[k],r,(int)sizeof r); }
        else if(psmof[k]==3)peer_rfcomm(loc[k],s,n);
        return; }
}
int btusb_bulk_out(const u8* p,int n,int ms){ (void)ms; u8 e[7]={0x13,5,1,p[0],(u8)(p[1]&15),1,0}; ev(e,7); if(n>=8){ u16 cid=p[6]|(p[7]<<8); peer_l2(cid,p+8,n-8); } return n; }
static int iso_ok, iso_alt, iso_stops;
int btusb_sco_start(int alt){ iso_alt=alt; return iso_ok; }
void btusb_sco_stop(void){ iso_stops++; }
int btusb_sco_mps(void){ return iso_ok?(iso_alt==6?63:17):0; }
static u8 rq_out[512]; static int rq_n;
static void rq_put(const u8* p,int n){ memcpy(rq_out+rq_n,p,n); rq_n+=n; }
static int has(const char* s){ return strstr(p_got,s)!=0; }
int main(void){
    setvbuf(stdout,0,_IONBF,0); bt_conn_init(); l2cap_init();
    { u8 a[3]={0x03,0x3F,0x01}; u8 b[3]={0x03,0x73,0x01}; CHECK(rfc_fcs(a,3)==0x1C&&rfc_fcs(b,3)==0xD7,"RFCOMM FCS: SABM DLCI 0 = 0x1C, UA DLCI 0 = 0xD7 (TS 07.10 vectors)"); }
    { u8 o[16]; int k=rfc_build(0,1,RFC_SABM|RFC_PF,-1,0,0,o,16); CHECK(k==4&&o[0]==0x03&&o[1]==0x3F&&o[2]==0x01&&o[3]==0x1C,"SABM frame bytes 03 3F 01 1C"); }
    { u8 o[300], d[200]; for(int i=0;i<200;i++)d[i]=(u8)i; int k=rfc_build(11,1,RFC_UIH,5,d,200,o,300); rfcframe_t f;
      CHECK(k==206&&o[2]==(u8)(200<<1)&&o[3]==1&&rfc_parse(o,k,1,&f)&&f.dlci==11&&f.cr&&f.type==RFC_UIH&&f.pf&&f.hascr&&f.credits==5&&f.len==200&&f.info[199]==199,"UIH with 2-byte length and credit byte round-trips");
      CHECK(o[k-1]==rfc_fcs(o,2),"UIH FCS covers address and control only");
      o[10]^=1; CHECK(rfc_parse(o,k,1,&f),"UIH payload change keeps FCS valid"); o[1]^=0x40; CHECK(!rfc_parse(o,k,1,&f),"corrupted control byte rejected by FCS");
      CHECK(!rfc_parse(o,k-1,1,&f)&&!rfc_parse(o,3,1,&f),"truncated frames rejected"); }
    { u8 o[16]; int k=rfc_build(0,1,RFC_UA,-1,0,0,o,16); o[2]=0x03; rfcframe_t f; CHECK(!rfc_parse(o,k,0,&f),"length field disagreeing with frame size rejected"); }
    { hfpag_t s; char o[512]; hfp_ag_reset(&s); int k;
      k=hfp_ag_line(&s,"AT+CMER=3,0,0,1",15,o,sizeof o); o[k]=0; CHECK(strstr(o,"ERROR")&&s.st==HAG_BRSF,"SLC: CMER before BRSF answered ERROR");
      k=hfp_ag_line(&s,"AT+BRSF=191\r",12,o,sizeof o); o[k]=0; CHECK(!strcmp(o,"\r\n+BRSF: 0\r\n\r\nOK\r\n")&&s.hff==191&&s.st==HAG_CINDT,"SLC: BRSF stores HF features, answers +BRSF and OK");
      k=hfp_ag_line(&s,"AT+CIND?",8,o,sizeof o); o[k]=0; CHECK(strstr(o,"ERROR")&&s.st==HAG_CINDT,"SLC: CIND? before CIND=? refused");
      k=hfp_ag_line(&s,"at+cind=?",9,o,sizeof o); o[k]=0; CHECK(strstr(o,"+CIND: (\"service\",(0,1)),(\"call\",(0,1))")&&strstr(o,"OK")&&s.st==HAG_CINDR,"SLC: CIND=? (lower case) lists indicators");
      k=hfp_ag_line(&s,"AT+CIND?",8,o,sizeof o); o[k]=0; CHECK(strstr(o,"+CIND: 1,0,0,0,5,0,5")&&s.st==HAG_CMER,"SLC: CIND? gives the values");
      k=hfp_ag_line(&s,"AT+CMER=3,0,0,1",15,o,sizeof o); o[k]=0; CHECK(!strcmp(o,"\r\nOK\r\n")&&s.st==HAG_SLC&&s.cmer,"SLC: CMER=3,0,0,1 completes the service level connection");
      k=hfp_ag_line(&s,"AT+VGM=22",9,o,sizeof o); CHECK(s.vgm==15&&k==6,"VGM clamped to 15");
      k=hfp_ag_line(&s,"AT+XYZ",6,o,sizeof o); o[k]=0; CHECK(strstr(o,"ERROR")!=0,"unknown AT command answered ERROR");
      CHECK(hfp_ag_line(&s,"\r\n",2,o,sizeof o)==0,"empty line ignored");
      k=hfp_ag_line(&s,"AT+CHLD=?",9,o,4); CHECK(k==4,"answer clipped to the buffer"); }
    { u8 r[]={0x35,8,0x35,3,0x19,0x01,0x00,0x35,3,0x19,0x00,0x03,0x08,7}; CHECK(hfp_sdp_channel(r,(int)sizeof r)==7&&hfp_sdp_channel(r,5)==0,"SDP protocol list: RFCOMM channel found"); }
    u8 cc[13]={0x03,11,0,0x42,0x00,1,2,3,4,5,6,0x01,0}; bt_host_event(cc,13);
    btconn_t* c=bt_conn_by_addr((u8[]){1,2,3,4,5,6},0); CHECK(c&&c->state==BTC_UP,"ACL link to headset up");
    CHECK(hfp_connect(c,3000)==-1&&!nch,"unencrypted link: no Hands-Free connection attempted");
    c->enc=1; c->keysz=16; bonded=0; CHECK(hfp_connect(c,3000)==-1&&!nch,"no stored bond: refused"); bonded=1;
    g_snd.rec_start=hda_rs; g_snd.rec_name="HDA mic";
    int r=hfp_connect(c,3000);
    CHECK(r==1&&hfp_slc_up()&&!p_bad&&p_dlc&&p_msc,"SDP, RFCOMM mux/PN/DLC/MSC, AT SLC: Hands-Free link up");
    CHECK(R.cfc&&R.dlci==10&&R.mtu==127,"credit-based flow control negotiated, DLCI 10 for channel 5 (initiator, direction bit 0), MTU 127");
    CHECK(has("+BRSF: 512")&&has("+CIND: 1,0,0,0,5,0,5")&&fg_oks==1,"headset received BRSF/CIND answers; featguard ok");
    for(int i=0;i<20;i++){ volatile u8 f=3; bt_conn_wait(c,&f,1,10); }
    CHECK(H.vgs==11,"post-SLC AT+VGS handled");
    { int tx=R.tx_cr; u8 big[400]; memset(big,'x',sizeof big); int s=rfcomm_send(big,(int)sizeof big); CHECK(s==(tx>=4?400:tx*127)&&R.tx_cr==tx-(s+126)/127,"send splits at MTU and spends one credit per frame"); }
    { int tx=R.tx_cr; R.tx_cr=0; CHECK(rfcomm_send((const u8*)"A",1)==0,"no credits: nothing sent"); R.tx_cr=tx; }
    { u8 e[12]={0x04,10,9,9,9,9,9,9,0,0,0,0}; last_op=0; bt_host_event(e,12); CHECK(last_op==0x042A&&!bt_sco_up(),"SCO request from another device refused (0x042A)"); }
    CHECK(hfp_mic_start()==1&&last_op==0x0428&&last_cmd[0]==0x42&&last_cmd[2]==0x40&&last_cmd[3]==0x1F&&last_cmd[14]==0xFF,"mic start: Setup Synchronous Connection on the headset's ACL, CVSD 8 kHz");
    { u8 e[19]={0x2C,17,0,0x55,0x00,1,2,3,4,5,6,0x02,6,4,60,0,60,0,2}; bt_host_event(e,19); CHECK(bt_sco_up(),"Synchronous Connection Complete: voice link up"); }
    { char t[160]; int k=hfp_text(t,t+159); t[k]=0; CHECK(strstr(t,"no audio")!=0&&!bt_sco_audio(),"status says voice link up with no audio path"); }
    { u8 e[6]={0x05,4,0,0x55,0x00,0x13}; bt_host_event(e,6); CHECK(!bt_sco_up(),"voice link disconnect tracked"); }
    { u8 e[12]={0x04,10,1,2,3,4,5,6,0,0,0,0}; last_op=0; bt_host_event(e,12); CHECK(last_op==0x0429&&last_cmd[0]==1&&last_cmd[16]==0x60&&last_cmd[18]==0xFF,"headset-initiated SCO from the bonded headset accepted (0x0429)"); }
    { scoreasm_t ra; memset(&ra,0,sizeof ra); u8 st[200]; int k=0; st[k++]=0x77; for(int p=0;p<3;p++){ st[k++]=0x55; st[k++]=0x00; st[k++]=48; for(int i=0;i<48;i++)st[k++]=(u8)(p*48+i); }
      int pk=0; for(int o=0;o<k;o+=17)pk+=bt_sco_reasm(&ra,st+o,k-o<17?k-o:17,0x55,rq_put);
      CHECK(pk==3&&rq_n==144&&rq_out[0]==0&&rq_out[143]==143&&ra.bad==1,"SCO reassembly over 17-byte isochronous frames: 3 packets, leading junk byte skipped");
      memset(&ra,0,sizeof ra); rq_n=0; u8 bad[6]={0x56,0x00,48,0x55,0x00,0}; CHECK(bt_sco_reasm(&ra,bad,6,0x55,rq_put)==1&&rq_n==0,"SCO packet for another handle resynchronised, empty packet skipped"); }
    iso_ok=1;
    { u8 e[12]={0x04,10,1,2,3,4,5,6,0,0,0,0}; bt_host_event(e,12); u8 c2[19]={0x2C,17,0,0x56,0x00,1,2,3,4,5,6,0x00,6,4,60,0,60,0,2}; bt_host_event(c2,19);
      CHECK(bt_sco_up()&&bt_sco_audio()&&iso_alt==2,"voice link up with isochronous endpoints (alt 2): audio path active");
      u8 st[51]; st[0]=0x56; st[1]=0; st[2]=48; for(int i=0;i<48;i++)st[3+i]=(u8)(i+1); bt_sco_rx_stream(st,17); bt_sco_rx_stream(st+17,34);
      u8 pcm[64]; int n=bt_sco_read(pcm,64); CHECK(n==48&&pcm[0]==1&&pcm[47]==48,"isochronous IN bytes become 48 bytes of 8 kHz PCM");
      u8 tx[60]; bt_sco_tx_stream(tx,60); CHECK(tx[0]==0x56&&tx[1]==0&&tx[2]==48&&tx[3]==0&&tx[51]==0x56&&tx[53]==48,"isochronous OUT stream: SCO packets with handle and 48-byte payload");
      char t[160]; int k=hfp_text(t,t+159); t[k]=0; CHECK(strstr(t,"audio over USB")!=0,"status says audio over USB");
      CHECK(g_snd.rec_read&&!strcmp(g_snd.rec_name,"Bluetooth headset microphone (Hands-Free)"),"system microphone routed to the headset while Hands-Free is up");
      CHECK(g_snd.rec_start()==1,"recorder start goes to the headset"); bt_sco_rx_stream(st,51); u8 rb[48*24]; int rn=g_snd.rec_read(rb,(int)sizeof rb); CHECK(rn==24*24&&rb[0]==1&&rb[2]==1&&rb[4]==1&&rb[24]==3,"rec_read: 8 kHz mono upsampled to 48 kHz stereo frames");
      hfp_mic_stop(); CHECK(!bt_sco_up()&&iso_stops==1,"mic stop closes the voice link and the isochronous endpoints"); }
    { int sv=h_slc; h_slc=0; CHECK(g_snd.rec_start()==7,"Hands-Free down but hook still installed: recorder falls back to the previous microphone"); g_snd.rec_stop(); h_slc=(u8)sv; }
    hfp_disconnect(); CHECK(!hfp_slc_up()&&!R.l2&&g_snd.rec_start==hda_rs&&!strcmp(g_snd.rec_name,"HDA mic"),"disconnect closes RFCOMM and gives the microphone back");
    { u8 e[12]={0x04,10,1,2,3,4,5,6,0,0,0,0}; last_op=0; bt_host_event(e,12); CHECK(last_op==0x042A,"SCO refused once Hands-Free is down"); }
    refuse_dlc=1; p_step=0; p_cr=0; p_cfc=0; int f0=fg_fails; r=hfp_connect(c,3000); CHECK(r==0&&!hfp_slc_up()&&fg_fails==f0+1,"headset refuses the RFCOMM channel (DM): fails cleanly, featguard counts it");
    { hfpag_t s; char o[256]; hfp_ag_reset(&s); s.agf=HFP_AG_CODEC_NEG; hfp_ag_line(&s,"AT+BRSF=128",11,o,sizeof o); int k=hfp_ag_line(&s,"AT+BAC=1,2",10,o,sizeof o); o[k]=0;
      CHECK(s.bac==6&&strstr(o,"OK"),"AT+BAC=1,2 stores CVSD+mSBC"); k=hfp_ag_line(&s,"AT+BCS=2",8,o,sizeof o); o[k]=0; CHECK(strstr(o,"ERROR")&&s.codec==1,"AT+BCS without a proposal refused");
      s.bcs=2; k=hfp_ag_line(&s,"AT+BCS=2",8,o,sizeof o); CHECK(s.codec==2&&!s.bcs,"AT+BCS=2 confirms the proposed mSBC codec");
      hfp_ag_reset(&s); hfp_ag_line(&s,"AT+BRSF=128",11,o,sizeof o); k=hfp_ag_line(&s,"AT+BAC=1,2",10,o,sizeof o); o[k]=0; CHECK(strstr(o,"ERROR")!=0,"AT+BAC refused when the AG did not offer codec negotiation"); }
    CHECK(bt_sco_pktlen(17,0)==48&&bt_sco_pktlen(9,0)==24&&bt_sco_pktlen(63,0)==60&&bt_sco_pktlen(25,64)==64&&bt_sco_pktlen(49,60)==60&&bt_sco_pktlen(0,0)==48,"SCO packet size from the isochronous altsetting, capped by the controller SCO MTU");
    refuse_dlc=0; p_step=0; p_cr=0; p_cfc=0; p_gotn=0; p_got[0]=0; hf_cmds=hf_cmds1; iso_ok=1;
    r=hfp_connect(c,3000); CHECK(r==1&&H.codec==2&&hfp_codec()==2&&has("+BRSF: 512")&&has("+BCS: 2"),"codec negotiation: HF offers mSBC, AG sends +BCS: 2, HF confirms, mSBC selected");
    CHECK(hfp_mic_start()==1&&last_op==0x0428&&last_cmd[10]==0x0D&&last_cmd[12]==0x63&&last_cmd[14]==0x02&&last_cmd[15]==0x88&&last_cmd[16]==0x03,"mSBC: eSCO setup with transparent air mode 0x0063, latency 13 ms, retransmission 2, packet types 0x0388");
    { u8 e[19]={0x2C,17,0x0D,0x57,0x00,1,2,3,4,5,6,0x02,6,4,60,0,60,0,3}; int f0=fg_fails; p_gotn=0; p_got[0]=0; bt_host_event(e,19); for(int i=0;i<10;i++){ volatile u8 f=3; bt_conn_wait(c,&f,1,10); }
      CHECK(!bt_sco_up()&&last_op==0x0428&&last_cmd[12]==0x60&&H.codec==1&&has("+BCS: 1")&&fg_fails==f0+1,"mSBC eSCO refused: AG sends +BCS: 1 and retries at once with CVSD"); }
    { u8 e[19]={0x2C,17,0,0x58,0x00,1,2,3,4,5,6,0x02,6,4,60,0,60,0,2}; bt_host_event(e,19); CHECK(bt_sco_up()&&bt_sco_codec()==1,"CVSD fallback voice link up"); hfp_mic_stop(); }
    H.codec=2; CHECK(hfp_mic_start()==1&&last_cmd[12]==0x63,"mSBC selected again: next voice link asks for transparent air mode");
    { u8 e[19]={0x2C,17,0,0x59,0x00,1,2,3,4,5,6,0x02,6,4,60,0,60,0,3}; bt_host_event(e,19); CHECK(bt_sco_up()&&bt_sco_codec()==2&&bt_sco_audio()&&iso_alt==6&&bt_sco_rate()==16000,"mSBC voice link up on isochronous alt 6, 16 kHz");
      static sbcenc_t pe; sbc_enc_init_msbc(&pe); int16_t tone[120]; u8 fr[60], st[3+60]; rq_n=0; int got=0;
      for(int j=0;j<6;j++){ for(int i=0;i<120;i++)tone[i]=(int16_t)((i%16<8)?8000:-8000); msbc_h2_frame(&pe,j,tone,fr); st[0]=0x59; st[1]=0; st[2]=60; memcpy(st+3,fr,60); bt_sco_rx_stream(st,63); }
      u8 pcm[2000]; got=bt_sco_read(pcm,2000); CHECK(got==6*240,"mSBC IN: H2-framed packets decoded to 16 kHz PCM (120 samples per frame)");
      u8 tx[126]; bt_sco_tx_stream(tx,126); msbcdec_t dd; msbc_dec_init(&dd); int16_t o[120]; CHECK(tx[0]==0x59&&tx[2]==60&&tx[3]==0x01&&tx[4]==0x08&&tx[5]==0xAD&&tx[63]==0x59&&tx[65]==60&&tx[66]==0x01&&tx[67]==0x38&&tx[68]==0xAD,"mSBC OUT: 60-byte SCO packets (alt 6), each an H2 header + mSBC frame with rolling sequence");
      u8 fr2[60]; memcpy(fr2,tx+3,60); CHECK(msbc_dec_frame(&dd,fr2+2,57,o)==120,"mSBC OUT frame decodes (valid CRC)");
      int rn=g_snd.rec_read(pcm,12*120); CHECK(rn==0||rn%12==0,"rec_read upsamples 16 kHz by 3"); hfp_mic_stop(); }
    { struct nvx_snd sv=g_snd; struct nvx_snd other=g_snd; other.rec_start=hda_rs; other.rec_name="USB mic"; g_snd=other; hfp_mic_keep(); CHECK(g_snd.rec_start!=hda_rs&&!strcmp(g_snd.rec_name,"Bluetooth headset microphone (Hands-Free)")&&h_prs==hda_rs,"sound output switched: headset microphone stays selected, the new device's mic kept as fallback"); (void)sv; }
    hfp_mic_select(0); CHECK(g_snd.rec_start==hda_rs&&!hfp_mic_selected(),"input selector: built-in microphone chosen"); hfp_mic_keep(); CHECK(g_snd.rec_start==hda_rs,"keep does not override the user's choice");
    hfp_mic_select(1); CHECK(hfp_mic_selected(),"input selector: headset microphone chosen again");
    { int tx=R.tx_cr; for(int i=0;i<40;i++){ u8 b[8]; int k=rfc_build(R.dlci,0,RFC_UIH,255,0,0,b,8); rs_rx(0,b,k); } CHECK(R.tx_cr==RFC_MAXCR&&tx<=RFC_MAXCR,"hostile headset granting 255 credits 40 times: credits capped at 255, no overflow"); }
    { int sv=R.st, mt=R.mtu; R.st=RS_PN; R.ev=3; u8 pn[10]={0x81,0x11,R.dlci,0xE0,0,0,8,0,0,7}; rs_mux_rx(pn,10); CHECK(R.ev==0&&R.mtu==mt,"PN response with MTU 8 (<16) refuses the channel instead of raising the MTU");
      R.ev=3; pn[6]=100; rs_mux_rx(pn,10); CHECK(R.ev==1&&R.mtu==100,"PN response with MTU 100 lowers our MTU to the peer's"); R.st=(u8)sv; R.mtu=mt; }
    CHECK(bt_iso_refill(40,9,1,1,30)==0&&bt_iso_refill(40,19,1,1,30)==10&&bt_iso_refill(5,250,1,1,30)==21&&bt_iso_refill(40,39,1,1,30)==30,"isochronous refill tops the ring up to 30 TDs in flight, across the 255-entry wrap");
    CHECK(bt_iso_refill(40,0,0,23,30)==0&&bt_iso_refill(40,0,0,14,30)==30&&bt_iso_refill(40,0,0,15,30)==30&&bt_iso_refill(40,60,1,1,30)==0,"no TD behind the event: no requeue; ring underrun/overrun: refill all; bogus pointer ahead of enqueue clamped");
    hfp_disconnect();
    printf(fails?"HFP HOST TEST: %d FAIL of %d\n":"HFP HOST TEST: ALL PASS (%d checks)\n",fails?fails:checks,checks);
    return fails!=0;
}
