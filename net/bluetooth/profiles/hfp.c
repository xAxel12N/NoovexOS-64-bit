#include "include/nvx/base.h"
#include "include/nvx/core/klog.h"
#include "include/nvx/core/sysfeat.h"
#include "include/nvx/core/sound.h"
#include "include/drivers.h"
#include "net/bluetooth/bt_host.h"
#include "net/bluetooth/bt_sec.h"
#include "net/bluetooth/profiles/rfcomm.h"
#include "net/bluetooth/profiles/hfp.h"
#include "net/bluetooth/profiles/sdp_srv.h"
static int hs_pre(const char* l,int n,const char* s){ int i=0; for(;s[i];i++){ if(i>=n)return 0; char a=l[i]; if(a>='a'&&a<='z')a=(char)(a-32); if(a!=s[i])return 0; } return i; }
static int hs_put(char* o,int k,int cap,const char* s){ while(*s&&k<cap)o[k++]=*s++; return k; }
static int hs_num(char* o,int k,int cap,u32 v){ char t[12]; int n=0; do{ t[n++]=(char)('0'+v%10); v/=10; }while(v); while(n&&k<cap)o[k++]=t[--n]; return k; }
static u32 hs_atoi(const char* p,int n){ u32 v=0; for(int i=0;i<n&&p[i]>='0'&&p[i]<='9';i++)v=v*10+(u32)(p[i]-'0'); return v; }
void hfp_ag_reset(hfpag_t* s){ s->st=HAG_BRSF; s->cmer=0; s->hff=0; s->vgs=8; s->vgm=8; s->bac=2; s->bcs=0; s->codec=1; s->agf=0; }
int hfp_ag_line(hfpag_t* s,const char* l,int n,char* o,int cap){
    while(n&&(l[0]==' '||l[0]=='\n'||l[0]=='\r')){ l++; n--; }
    while(n&&(l[n-1]==' '||l[n-1]=='\n'||l[n-1]=='\r'))n--;
    if(!n)return 0;
    int k=0, m;
    if((m=hs_pre(l,n,"AT+BRSF="))){ s->hff=hs_atoi(l+m,n-m); if(s->st==HAG_BRSF)s->st=HAG_CINDT; k=hs_put(o,k,cap,"\r\n+BRSF: "); k=hs_num(o,k,cap,s->agf); k=hs_put(o,k,cap,"\r\n"); }
    else if(s->st==HAG_BRSF)return hs_put(o,0,cap,"\r\nERROR\r\n");
    else if(hs_pre(l,n,"AT+CIND=?")){ k=hs_put(o,k,cap,"\r\n+CIND: (\"service\",(0,1)),(\"call\",(0,1)),(\"callsetup\",(0-3)),(\"callheld\",(0-2)),(\"signal\",(0-5)),(\"roam\",(0,1)),(\"battchg\",(0-5))\r\n"); if(s->st==HAG_CINDT)s->st=HAG_CINDR; }
    else if(hs_pre(l,n,"AT+CIND?")){ if(s->st<HAG_CINDR)return hs_put(o,0,cap,"\r\nERROR\r\n"); k=hs_put(o,k,cap,"\r\n+CIND: 1,0,0,0,5,0,5\r\n"); if(s->st==HAG_CINDR)s->st=HAG_CMER; }
    else if((m=hs_pre(l,n,"AT+CMER="))){ if(s->st<HAG_CMER)return hs_put(o,0,cap,"\r\nERROR\r\n"); int c=0; for(int i=m;i<n;i++)if(l[i]==',')c++; s->cmer=c>=3&&l[n-1]=='1'; s->st=HAG_SLC; }
    else if((m=hs_pre(l,n,"AT+BAC="))){ if(!(s->agf&HFP_AG_CODEC_NEG))return hs_put(o,0,cap,"\r\nERROR\r\n"); u8 b=0; for(int i=m;i<n;){ u32 v=hs_atoi(l+i,n-i); if(v&&v<8)b|=(u8)(1u<<v); while(i<n&&l[i]!=',')i++; i++; } s->bac=b|2; }
    else if((m=hs_pre(l,n,"AT+BCS="))){ u32 v=hs_atoi(l+m,n-m); if(!s->bcs||v!=s->bcs)return hs_put(o,0,cap,"\r\nERROR\r\n"); s->codec=(u8)v; s->bcs=0; }
    else if(hs_pre(l,n,"AT+BCC"));
    else if(hs_pre(l,n,"AT+CHLD=?"))k=hs_put(o,k,cap,"\r\n+CHLD: (0,1,2)\r\n");
    else if((m=hs_pre(l,n,"AT+VGS="))){ u32 v=hs_atoi(l+m,n-m); s->vgs=(u8)(v>15?15:v); }
    else if((m=hs_pre(l,n,"AT+VGM="))){ u32 v=hs_atoi(l+m,n-m); s->vgm=(u8)(v>15?15:v); }
    else if(hs_pre(l,n,"AT+COPS?"))k=hs_put(o,k,cap,"\r\n+COPS: 0,0,\"NoovexOS\"\r\n");
    else if(hs_pre(l,n,"AT+CLCC")||hs_pre(l,n,"AT+NREC=")||hs_pre(l,n,"AT+CLIP=")||hs_pre(l,n,"AT+CCWA=")||hs_pre(l,n,"AT+CMEE=")||hs_pre(l,n,"AT+BIA=")||hs_pre(l,n,"AT+COPS=")||hs_pre(l,n,"AT+XAPL=")||hs_pre(l,n,"AT+IPHONEACCEV=")||hs_pre(l,n,"AT+BIND=")||hs_pre(l,n,"AT+BTRH?"));
    else return hs_put(o,0,cap,"\r\nERROR\r\n");
    return hs_put(o,k,cap,"\r\nOK\r\n");
}
int hfp_sdp_channel(const u8* p,int n){ for(int i=0;i+4<n;i++) if(p[i]==0x19&&p[i+1]==0&&p[i+2]==3&&p[i+3]==0x08&&p[i+4]>=1&&p[i+4]<=30)return p[i+4]; return 0; }
static struct featguard hfg={"Bluetooth headset microphone (HFP)","feat-bthfp.cfg",1,0,3,0,0};
static struct featguard mfg={"Bluetooth wide-band speech (mSBC)","feat-btmsbc.cfg",1,0,3,0,0};
static hfpag_t H; static volatile u8 h_slc, h_bcsev, h_inc; static u8 h_pref=1; static char h_line[128]; static int h_ln; static btconn_t* h_c;
static void h_say(const char* m){ klog2("bluetooth: headset ",m); }
static void h_in_slc(void);
static void h_rx(const u8* p,int n){
    for(int i=0;i<n;i++){ char ch=(char)p[i];
        if(ch=='\r'||ch=='\n'){ if(h_ln){ char o[256]; int k=hfp_ag_line(&H,h_line,h_ln,o,(int)sizeof o); h_ln=0; if(k>0)rfcomm_send((const u8*)o,k); if(H.st==HAG_SLC&&h_slc!=1){ h_slc=1; if(h_inc)h_in_slc(); } if(H.codec==2&&h_bcsev==3)h_bcsev=1; } }
        else if(h_ln<(int)sizeof h_line)h_line[h_ln++]=ch; else h_ln=0; }
}
static volatile u8 sdpg; static u8 sdpb[256]; static int sdpn;
static void sdp_rx(l2chan_t* ch,const u8* p,int n){ (void)ch; if(n>=7&&p[0]==0x07){ int k=n<(int)sizeof sdpb?n:(int)sizeof sdpb; for(int i=0;i<k;i++)sdpb[i]=p[i]; sdpn=k; sdpg=1; } else if(n>=1&&p[0]==0x01)sdpg=0; }
static int sdp_hf_channel(btconn_t* c,int to){
    l2chan_t* s=l2cap_connect(c,0x0001,sdp_rx,to); if(!s)return -1;
    u8 q[]={0x06,0,2,0,13, 0x35,3,0x19,0x11,0x1E, 0,200, 0x35,3,0x09,0x00,0x04, 0};
    sdpg=3; sdpn=0; l2cap_send(s,q,(int)sizeof q);
    int ok=bt_conn_wait(c,&sdpg,1,to); l2cap_close(s,500);
    return ok?hfp_sdp_channel(sdpb+7,sdpn-7):-1;
}
static int h_trusted(const btconn_t* c){ return c&&c->state==BTC_UP&&!c->le&&c->enc&&c->keysz>=BT_BREDR_KEYSZ&&bt_keys_find(c->addr,0)!=0; }
int hfp_enabled(void){ fg_boot(&hfg); return hfg.on&&!hfg.dead; }
void hfp_set_enabled(int on){ fg_enable(&hfg,on); if(!on)hfp_disconnect(); }
int hfp_slc_up(void){ return h_slc==1&&rfcomm_up(); }
int hfp_sco_peer(const u8* addr){ btconn_t* c=rfcomm_conn(); if(!hfp_slc_up()||!h_trusted(c)||c!=h_c)return 0; for(int i=0;i<6;i++)if(c->addr[i]!=addr[i])return 0; return 1; }
static int (*h_prs)(void); static int (*h_prr)(u8*,int); static void (*h_pst)(void); static void (*h_pgn)(int); static const char* h_pnm; static int h_hook;
static int h_bt;
static int h_rec_start(void){ h_bt=hfp_slc_up()&&hfp_mic_start(); return h_bt?1:h_prs?h_prs():0; }
static void h_rec_stop(void){ if(h_bt)hfp_mic_stop(); else if(h_pst)h_pst(); h_bt=0; }
static void h_rec_gain(int p){ if(!h_bt&&h_pgn)h_pgn(p); }
static int h_rec_read(u8* d,int len){
    if(!h_bt)return h_prr?h_prr(d,len):0;
    int rep=bt_sco_rate()==16000?3:6; int want=len/(4*rep); if(want<=0)return 0; if(want>128)want=128; u8 t[256]; int n=bt_sco_read(t,want*2)&~1; int k=0;
    for(int i=0;i+1<n;i+=2) for(int r=0;r<rep;r++){ d[k]=t[i]; d[k+1]=t[i+1]; d[k+2]=t[i]; d[k+3]=t[i+1]; k+=4; }
    return k;
}
static void h_hook_rec(int on){
    if(on&&!h_hook&&g_snd_state==1){ h_prs=g_snd.rec_start; h_prr=g_snd.rec_read; h_pst=g_snd.rec_stop; h_pgn=g_snd.rec_gain; h_pnm=g_snd.rec_name;
        g_snd.rec_start=h_rec_start; g_snd.rec_read=h_rec_read; g_snd.rec_stop=h_rec_stop; g_snd.rec_gain=h_rec_gain; g_snd.rec_name="Bluetooth headset microphone (Hands-Free)"; h_hook=1; }
    else if(!on&&h_hook){ h_hook=0; if(g_snd.rec_start==h_rec_start){ g_snd.rec_start=h_prs; g_snd.rec_read=h_prr; g_snd.rec_stop=h_pst; g_snd.rec_gain=h_pgn; g_snd.rec_name=h_pnm; } }
}
int hfp_msbc_enabled(void){ fg_boot(&mfg); return mfg.on&&!mfg.dead; }
void hfp_set_msbc(int on){ fg_enable(&mfg,on); }
static void h_codec_select(btconn_t* c){
    if(!(H.agf&HFP_AG_CODEC_NEG)||!(H.hff&HFP_HF_CODEC_NEG)||!(H.bac&4)){ H.codec=1; return; }
    if(!fg_begin(&mfg)){ H.codec=1; return; }
    H.bcs=2; h_bcsev=3; rfcomm_send((const u8*)"\r\n+BCS: 2\r\n",11);
    if(bt_conn_wait(c,&h_bcsev,1,1500)&&H.codec==2){ h_say("wide-band speech (mSBC) selected"); return; }
    H.codec=1; H.bcs=0; fg_fail(&mfg,"headset did not confirm the mSBC codec");
}
int hfp_codec(void){ return hfp_slc_up()&&H.codec==2?2:1; }
void hfp_codec_fallback(void){ if(!hfp_slc_up()||H.codec!=2)return; H.codec=1; H.bcs=1; rfcomm_send((const u8*)"\r\n+BCS: 1\r\n",11); fg_fail(&mfg,"headset refused the wide-band voice link"); }
int hfp_mic_avail(void){ return hfp_slc_up(); }
int hfp_mic_selected(void){ return h_hook&&g_snd.rec_start==h_rec_start; }
void hfp_mic_select(int hs){ h_pref=(u8)(hs?1:0); if(!hs)h_hook_rec(0); else if(hfp_slc_up()){ if(h_hook&&g_snd.rec_start!=h_rec_start)h_hook=0; h_hook_rec(1); } }
void hfp_mic_keep(void){ if(h_pref&&h_hook&&hfp_slc_up()&&g_snd_state==1&&g_snd.rec_start!=h_rec_start){ h_hook=0; h_hook_rec(1); } }
int hfp_connect(btconn_t* c,int ms){
    if(!c||c->le||c->state!=BTC_UP)return -1;
    if(!hfp_enabled())return -1;
    if(!h_trusted(c)){ h_say("not connected: link is not encrypted with a stored bond"); return -1; }
    if(hfp_slc_up()&&h_c==c)return 1;
    hfp_disconnect();
    int to=ms<2000?2000:ms>10000?10000:ms;
    int ch=sdp_hf_channel(c,to/2); if(ch<=0){ h_say(ch<0?"SDP query failed":"device has no Hands-Free record"); return 0; }
    if(!fg_begin(&hfg))return -1;
    hfp_ag_reset(&H); H.agf=hfp_msbc_enabled()?HFP_AG_CODEC_NEG:0; h_ln=0; h_slc=3; h_c=c;
    if(!rfcomm_open(c,(u8)ch,h_rx,to/2)){ fg_fail(&hfg,"RFCOMM channel to the headset failed"); h_c=0; return 0; }
    if(!bt_conn_wait(c,&h_slc,1,to)){ h_say("did not finish the AT service level setup"); rfcomm_close(); h_c=0; fg_fail(&hfg,"Hands-Free service level setup did not finish"); return 0; }
    fg_ok(&hfg); h_codec_select(c); if(h_pref)h_hook_rec(1); h_say("Hands-Free link up, headset microphone available"); return 1;
}
void hfp_disconnect(void){ h_hook_rec(0); bt_sco_close(); if(h_c)rfcomm_close(); h_c=0; h_slc=0; h_inc=0; }
static int h_in_ok(btconn_t* c){
    if(!bt_srv_enabled()||!hfp_enabled()||!h_trusted(c))return 0;
    if(hfp_slc_up())return 0;
    if(!fg_begin(&hfg))return 0;
    hfp_ag_reset(&H); H.agf=hfp_msbc_enabled()?HFP_AG_CODEC_NEG:0; h_ln=0; h_slc=3; h_c=c; h_inc=1; return 1;
}
static void h_in_up(btconn_t* c){ (void)c; h_say("opened the Hands-Free channel, waiting for AT setup"); }
static void h_in_slc(void){
    h_inc=0; fg_ok(&hfg);
    if((H.agf&HFP_AG_CODEC_NEG)&&(H.hff&HFP_HF_CODEC_NEG)&&(H.bac&4)&&fg_begin(&mfg)){ H.bcs=2; rfcomm_send((const u8*)"\r\n+BCS: 2\r\n",11); }
    if(h_pref)h_hook_rec(1);
    h_say("connected to us: Hands-Free link up, headset microphone available");
}
int hfp_listen(void){ return rfcomm_listen(HFP_AG_CH,h_in_ok,h_rx,h_in_up); }
int hfp_mic_start(void){ if(!hfp_slc_up())return 0; return bt_sco_setup(h_c); }
void hfp_mic_stop(void){ bt_sco_close(); }
int hfp_text(char* p,char* e){
    char* s=p; const char* t=!hfp_enabled()?"Headset microphone: off\n":!hfp_slc_up()?"Headset microphone: no Hands-Free headset connected\n":bt_sco_audio()?(bt_sco_codec()==2?"Headset microphone: wide-band (mSBC) voice link up, audio over USB\n":"Headset microphone: voice link up, audio over USB\n"):bt_sco_up()?"Headset microphone: voice link up, no audio (adapter has no usable isochronous endpoint)\n":"Headset microphone: Hands-Free connected, voice link closed\n";
    while(*t&&p<e)*p++=*t++;
    return (int)(p-s);
}
