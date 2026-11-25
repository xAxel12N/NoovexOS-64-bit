#include "include/nvx/base.h"
#include "include/nvx/core/klog.h"
#include "include/nvx/core/sysfeat.h"
#include "include/nvx/core/sound.h"
#include "include/drivers.h"
#include "include/nvx/arch/x86_64/time/hrtimer.h"
#include "net/bluetooth/bt_host.h"
#include "net/bluetooth/bt_sec.h"
#include "net/bluetooth/sbc/sbc.h"
#include "net/bluetooth/profiles/a2dp.h"
#include "net/bluetooth/profiles/sdp_srv.h"
#define A2_RING 65536
#define A2_PKT 1024
enum { AV_DISCOVER=1, AV_GETCAP=2, AV_SETCONF=3, AV_OPEN=6, AV_START=7, AV_CLOSE=8, AV_SUSPEND=9, AV_ABORT=10, AV_DELAY=13 };
typedef struct { btconn_t* c; l2chan_t *sig, *med, *avc; u8 lbl, wsig, done, acp, cfg[4], vol, paused, inc; a2acp_t as; u8 rsp[160]; int rsn, rate, up;
    sbcenc_t enc; u8 ring[A2_RING]; u32 rr, rw; u16 seq; u32 ts; u64 t0, sent; const char* err; } a2_t;
static a2_t A;
static struct featguard a2g={"Bluetooth audio (A2DP)","feat-btaudio.cfg",1,0,3,0,0};
static struct nvx_snd a2snd, a2prev; static int a2prev_state, a2att;
static u16 b16(const u8* p){ return (u16)((p[0]<<8)|p[1]); }
static void a2_say(const char* m){ klog2("bluetooth: audio ",m); }
static u32 a2_avail(void){ return A.rw-A.rr; }
static int a2_free(void){ return A.up?(int)(A2_RING-a2_avail())&~3:0; }
static int a2_write(const u8* p,int n){ int f=a2_free(); if(n>f)n=f; n&=~3; for(int i=0;i<n;i++)A.ring[(A.rw+(u32)i)%A2_RING]=p[i]; A.rw+=(u32)n; return n; }
static int a2_start(void){ return A.up; }
static void a2_stop(void){ A.rr=A.rw; }
static void a2_volume(int v){ A.vol=(u8)(v<0?0:v>100?100:v); }
int a2dp_media_packet(sbcenc_t* e,const u8* pcm,int frames,u16 seq,u32 ts,int vol,u8* out,int cap){
    if(frames<1||frames>15||cap<13)return -1;
    out[0]=0x80; out[1]=0x60; out[2]=(u8)(seq>>8); out[3]=(u8)seq; out[4]=(u8)(ts>>24); out[5]=(u8)(ts>>16); out[6]=(u8)(ts>>8); out[7]=(u8)ts; out[8]=0; out[9]=0; out[10]=0; out[11]=1; out[12]=(u8)frames;
    int n=13; u8 t[512];
    for(int f=0;f<frames;f++){ int cs=e->codesize; if(cs>(int)sizeof t)return -1;
        for(int i=0;i+1<cs;i+=2){ int s=(int16_t)(pcm[i]|(pcm[i+1]<<8)); s=s*vol/100; t[i]=(u8)s; t[i+1]=(u8)(s>>8); }
        int k=sbc_enc_frame(e,t,out+n,cap-n); if(k<0)return -1; n+=k; pcm+=cs; }
    return n;
}
int a2dp_frames_fit(int framelen,int omtu){ int k=(omtu-13)/(framelen>0?framelen:1); return k<1?0:k>15?15:k; }
static void a2_poll(void){
    if(!A.up||A.paused||!A.med||A.med->state!=L2_OPEN)return;
    u64 now=hr_ns(); int cs=A.enc.codesize, spf=A.enc.subbands*A.enc.blocks;
    u64 due=(now-A.t0)*(u64)A.rate/1000000000ULL+(u64)A.rate/50;
    if(A.sent+(u64)A.rate/5<due)A.sent=due-(u64)A.rate/50;
    int mtu=A.med->omtu<A2_PKT?A.med->omtu:A2_PKT, fit=a2dp_frames_fit(A.enc.framelen,mtu);
    for(int g=0;g<8&&A.sent<due&&fit;g++){
        if(bt_acl_pending()>3)return;
        int fr=(int)(a2_avail()/(u32)cs); if(fr>fit)fr=fit; if(fr<1)return;
        static u8 pc[15*512], pk[A2_PKT]; for(int i=0;i<fr*cs;i++)pc[i]=A.ring[(A.rr+(u32)i)%A2_RING];
        int n=a2dp_media_packet(&A.enc,pc,fr,A.seq,A.ts,A.vol,pk,mtu);
        A.rr+=(u32)(fr*cs); if(n<0)return;
        if(l2cap_send(A.med,pk,n)<0)return;
        A.seq++; A.ts+=(u32)(fr*spf); A.sent+=(u64)(fr*spf);
    }
}
static void a2_attach(int on){
    if(on&&!a2att){ if(!g_snd_state)snd_init(); a2prev=g_snd; a2prev_state=g_snd_state;
        u8* z=(u8*)&a2snd; for(unsigned i=0;i<sizeof a2snd;i++)z[i]=0;
        a2snd.name="Bluetooth headphones (A2DP)"; a2snd.rate=A.rate; a2snd.start=a2_start; a2snd.free_bytes=a2_free; a2snd.write=a2_write; a2snd.poll=a2_poll; a2snd.stop=a2_stop; a2snd.volume=a2_volume;
        a2snd.rec_start=a2prev.rec_start; a2snd.rec_read=a2prev.rec_read; a2snd.rec_stop=a2prev.rec_stop; a2snd.rec_gain=a2prev.rec_gain; a2snd.rec_name=a2prev.rec_name;
        if(a2prev_state==1&&a2prev.stop)a2prev.stop();
        g_snd=a2snd; g_snd_state=1; a2att=1; a2_volume(g_snd_vol); a2_say("is now the sound output"); }
    else if(!on&&a2att){ a2att=0; if(g_snd.write==a2_write){ struct nvx_snd cur=g_snd; g_snd=a2prev; g_snd_state=a2prev_state; g_snd.rec_start=cur.rec_start; g_snd.rec_read=cur.rec_read; g_snd.rec_stop=cur.rec_stop; g_snd.rec_gain=cur.rec_gain; g_snd.rec_name=cur.rec_name; if(g_snd_state==1&&g_snd.volume)g_snd.volume(g_snd_vol); } a2_say("output back to the built-in sound card"); }
}
static void sig_send(u8 lbl,u8 mt,u8 id,const u8* d,int n){ u8 b[80]; if(n>78)n=78; b[0]=(u8)((lbl<<4)|mt); b[1]=id; for(int i=0;i<n;i++)b[2+i]=d[i]; l2cap_send(A.sig,b,2+n); }
int a2dp_acp_cmd(a2acp_t* s,u8 id,const u8* d,int n,u8* r,int* rn){
    *rn=0; int seid=n>=1?d[0]>>2:0;
    if(id==AV_DISCOVER){ r[0]=(u8)((1<<2)|(s->st?2:0)); r[1]=0x00; *rn=2; return 2; }
    if(id==AV_GETCAP||id==0x0C){ if(n<1){ r[0]=0x11; *rn=1; return 3; } if(seid!=1){ r[0]=0x12; *rn=1; return 3; }
        u8 c[10]={0x01,0x00,0x07,0x06,0x00,0x00,0x33,0x17,2,53}; for(int i=0;i<10;i++)r[i]=c[i]; *rn=10; return 2; }
    if(id==AV_SETCONF){
        r[0]=0; *rn=2;
        if(n<2){ r[1]=0x11; return 3; } if(seid!=1){ r[1]=0x12; return 3; } if(s->st){ r[1]=0x13; return 3; }
        const u8* cf=0; for(int o=2;o<n;){ if(o+2>n){ r[1]=0x11; return 3; } int cat=d[o], l=d[o+1]; if(o+2+l>n){ r[1]=0x11; return 3; }
            if(cat==7){ if(l!=6||(d[o+2]>>4)!=0||d[o+3]!=0){ r[0]=7; r[1]=0x29; return 3; } cf=d+o+4; }
            else if(cat!=1||l!=0){ r[0]=(u8)cat; r[1]=0x17; return 3; }
            o+=2+l; }
        if(!cf){ r[0]=7; r[1]=0x29; return 3; }
        u8 f=cf[0]&0xF0, m=cf[0]&0x0F, b=cf[1]&0xF0, sb=cf[1]&0x0C, am=cf[1]&3; int mn=cf[2], mx=cf[3];
        if((f!=0x20&&f!=0x10)||(m!=0x02&&m!=0x01)||b!=0x10||sb!=0x04||(am!=1&&am!=2)||mn<2||mx<mn||mx>250){ r[0]=7; r[1]=0x29; return 3; }
        int bp=mx>53?53:mx; if(bp<mn)bp=mn;
        s->cfg[0]=cf[0]; s->cfg[1]=cf[1]; s->cfg[2]=(u8)mn; s->cfg[3]=(u8)bp; s->rate=f==0x10?48000:44100; s->intseid=(u8)(d[1]>>2); s->st=1; *rn=0; return 2; }
    if(id==4){ if(seid!=1||!s->st){ r[0]=0x12; *rn=1; return 3; } u8 c[10]={0x01,0x00,0x07,0x06,0x00,0x00,s->cfg[0],s->cfg[1],s->cfg[2],s->cfg[3]}; for(int i=0;i<10;i++)r[i]=c[i]; *rn=10; return 2; }
    if(id==AV_OPEN){ if(seid!=1){ r[0]=0x12; *rn=1; return 3; } if(s->st!=1){ r[0]=0x31; *rn=1; return 3; } s->st=2; return 2; }
    if(id==AV_START){ if(seid!=1||s->st<2){ r[0]=d&&n>=1?d[0]:0; r[1]=seid!=1?0x12:0x31; *rn=2; return 3; } s->st=3; return 2; }
    if(id==AV_SUSPEND){ if(seid!=1||s->st!=3){ r[0]=n>=1?d[0]:0; r[1]=seid!=1?0x12:0x31; *rn=2; return 3; } s->st=2; return 2; }
    if(id==AV_CLOSE){ if(seid!=1||s->st<2){ r[0]=seid!=1?0x12:0x31; *rn=1; return 3; } s->st=0; return 2; }
    if(id==AV_ABORT){ s->st=0; return 2; }
    if(id==AV_DELAY)return 2;
    return 1;
}
static void a2_down(const char* why){ if(A.up)a2_say(why); A.up=0; A.paused=0; a2_attach(0); }
static void sig_rx(l2chan_t* ch,const u8* p,int n){
    (void)ch; if(n<2||((p[0]>>2)&3))return;
    u8 lbl=p[0]>>4, mt=p[0]&3, id=p[1]&0x3F;
    if(mt==0&&A.inc){
        u8 r[16]; int rn, was=A.as.st, k=a2dp_acp_cmd(&A.as,id,p+2,n-2,r,&rn);
        sig_send(lbl,(u8)k,id,r,rn);
        if(k!=2)return;
        if(id==AV_SETCONF){ A.acp=A.as.intseid; for(int i=0;i<4;i++)A.cfg[i]=A.as.cfg[i]; A.rate=A.as.rate; }
        else if(id==AV_START&&was==2){
            if(!A.up){ if(sbc_enc_init(&A.enc,A.rate,2,(A.cfg[0]&1)?SBC_MODE_JOINT:SBC_MODE_STEREO,8,16,(A.cfg[1]&1)?SBC_AM_LOUDNESS:SBC_AM_SNR,A.cfg[3])<0){ fg_fail(&a2g,"bad SBC configuration from the headphones"); return; }
                A.t0=hr_ns(); A.sent=0; A.up=1; A.paused=0; fg_ok(&a2g); a2_attach(1); a2_say(A.rate==48000?"headphones connected to us, streaming SBC 48 kHz":"headphones connected to us, streaming SBC 44.1 kHz"); }
            else { A.paused=0; A.t0=hr_ns(); A.sent=0; } }
        else if(id==AV_SUSPEND)A.paused=1;
        else if(id==AV_CLOSE||id==AV_ABORT)a2_down("stream closed by the headphones");
        return; }
    if(mt==0){
        if(id==AV_SUSPEND){ A.paused=1; sig_send(lbl,2,id,0,0); }
        else if(id==AV_START){ A.paused=0; A.t0=hr_ns(); A.sent=0; sig_send(lbl,2,id,0,0); }
        else if(id==AV_CLOSE||id==AV_ABORT){ sig_send(lbl,2,id,0,0); a2_down("stream closed by the headphones"); }
        else if(id==AV_DELAY){ sig_send(lbl,2,id,0,0); }
        else { u8 e=0x0A; if(id==AV_DISCOVER||id==AV_GETCAP||id==AV_SETCONF)sig_send(lbl,3,id,&e,1); else sig_send(lbl,1,id,0,0); }
        return; }
    if(lbl!=A.lbl||id!=A.wsig)return;
    int k=n-2; if(k>(int)sizeof A.rsp)k=(int)sizeof A.rsp; for(int i=0;i<k;i++)A.rsp[i]=p[2+i]; A.rsn=k; A.done=mt==2?1:0;
}
static int sig_cmd(u8 id,const u8* d,int n,int ms){
    if(!A.sig||A.sig->state!=L2_OPEN)return 0;
    A.lbl=(u8)((A.lbl+1)&15); A.wsig=id; A.done=3; sig_send(A.lbl,0,id,d,n);
    if(!bt_conn_wait(A.c,&A.done,1,ms)){ if(A.done!=1)A.err=A.done==0?"the headphones rejected a stream command":"the headphones did not answer"; return 0; }
    return 1;
}
int a2dp_pick_config(const u8* sbc,int* rate,u8* cfg){
    u8 fr=sbc[0]>>4, cm=sbc[0]&15, bl=sbc[1]>>4, sb=(sbc[1]>>2)&3, am=sbc[1]&3; int mn=sbc[2], mx=sbc[3];
    if(!(fr&3)||!(cm&3)||!(bl&1)||!(sb&1)||!am||mx<mn||mx<2)return 0;
    *rate=fr&1?48000:44100;
    cfg[0]=(u8)((fr&1?0x10:0x20)|(cm&1?1:2)); cfg[1]=(u8)(0x10|0x04|(am&1?1:2));
    int bp=*rate==48000?51:53; if(bp>mx)bp=mx; if(bp<mn)bp=mn; if(bp<2)bp=2; cfg[2]=(u8)mn; cfg[3]=(u8)bp; return bp;
}
int a2dp_parse_sep(const u8* d,int n,u8* acp){ for(int i=0;i+1<n;i+=2){ if((d[i]>>2)&&!(d[i]&2)&&(d[i+1]>>4)==0&&(d[i+1]&8)){ *acp=d[i]>>2; return 1; } } return 0; }
int a2dp_find_sbc(const u8* d,int n,const u8** sbc){
    for(int o=0;o+2<=n;){ int cat=d[o], l=d[o+1]; if(o+2+l>n)break; if(cat==7&&l>=6&&(d[o+2]>>4)==0&&d[o+3]==0){ *sbc=d+o+4; return 1; } o+=2+l; } return 0;
}
static volatile u8 sdpg; static u16 sdpc;
static void sdp_rx(l2chan_t* ch,const u8* p,int n){ (void)ch; if(n>=7&&p[0]==0x07){ sdpc=b16(p+5); sdpg=1; } else if(n>=1&&p[0]==0x01)sdpg=0; }
static int sdp_has_sink(btconn_t* c){
    l2chan_t* s=l2cap_connect(c,0x0001,sdp_rx,5000); if(!s)return -1;
    u8 q[]={0x06,0,1,0,15, 0x35,3,0x19,0x11,0x0B, 0,64, 0x35,5,0x0A,0x00,0x00,0xFF,0xFF, 0};
    sdpg=3; sdpc=0; l2cap_send(s,q,(int)sizeof q);
    int ok=bt_conn_wait(c,&sdpg,1,3000); l2cap_close(s,1000);
    return ok?sdpc>2:-1;
}
int a2dp_avc_trusted(const btconn_t* c){ return A.up&&c&&c==A.c&&c->state==BTC_UP&&!c->le&&c->enc&&c->keysz>=BT_BREDR_KEYSZ&&bt_keys_find(c->addr,0)!=0; }
static void avc_rx(l2chan_t* ch,const u8* p,int n){
    if(!a2dp_avc_trusted(ch->c))return;
    if(n<6||(p[0]&2)||b16(p+1)!=0x110E)return;
    u8 r[16]; int k=n<16?n:16; for(int i=0;i<k;i++)r[i]=p[i]; r[0]=(u8)(p[0]|2);
    if(p[3]==0x00&&p[5]==0x7C&&n>=8){ r[3]=0x09; u8 op=p[6]&0x7F, rel=p[6]&0x80;
        if(!rel){ if(op==0x44||op==0x46||op==0x4C){ A.paused=op==0x46?1:op==0x44?0:!A.paused; if(!A.paused){ A.t0=hr_ns(); A.sent=0; } a2_say(A.paused?"paused (headphone button)":"playing (headphone button)"); }
            else if(op==0x41||op==0x42){ int v=g_snd_vol+(op==0x41?5:-5); v=v<0?0:v>100?100:v; g_snd_vol=v; a2_volume(v); } } }
    else r[3]=0x08;
    l2cap_send(ch,r,k);
}
static int avc_accept(l2chan_t* ch){ if((!a2dp_avc_trusted(ch->c)&&!(A.inc&&A.sig&&A.c==ch->c&&sdp_srv_trusted(ch->c)))||(A.avc&&A.avc->state!=L2_CLOSED&&A.avc!=ch)){ a2_say("remote-control channel refused (not the encrypted, bonded headphones)"); return 0; } ch->rx=avc_rx; A.avc=ch; return 1; }
static void med_closed(l2chan_t* ch){ (void)ch; A.med=0; a2_down("media channel closed"); }
static void sig_closed(l2chan_t* ch){ (void)ch; A.sig=0; a2_down("headphones disconnected"); }
int a2dp_enabled(void){ fg_boot(&a2g); return a2g.on&&!a2g.dead; }
int a2dp_connect(btconn_t* c,int ms){
    if(!c||c->le||c->state!=BTC_UP)return -1;
    if(!a2dp_enabled()){ a2_say("is turned off in Settings"); return -1; }
    if(A.up&&A.c==c)return 1;
    if(A.inc&&A.sig&&A.sig->state!=L2_CLOSED&&A.c==c)return 1;
    if(A.up||(A.inc&&A.sig))a2dp_disconnect();
    l2cap_server(0x0017,avc_accept);
    int to=ms<2000?2000:ms>15000?15000:ms;
    int has=sdp_has_sink(c); if(has==0){ a2_say("device has no A2DP sink record"); return 0; }
    u8* z=(u8*)&A; for(unsigned i=0;i<sizeof A;i++)z[i]=0; A.c=c; A.vol=(u8)g_snd_vol;
    A.sig=l2cap_connect(c,0x0019,sig_rx,to/2); if(!A.sig){ a2_say("AVDTP channel refused"); return 0; }
    A.sig->closed=sig_closed;
    if(!fg_begin(&a2g)){ l2cap_close(A.sig,500); A.sig=0; return -1; }
    const char* why="stream setup failed"; A.err=0;
    if(!sig_cmd(AV_DISCOVER,0,0,3000)||!a2dp_parse_sep(A.rsp,A.rsn,&A.acp)){ why="no free audio sink endpoint"; goto bad; }
    { u8 d=(u8)(A.acp<<2); if(!sig_cmd(AV_GETCAP,&d,1,3000))goto bad; }
    { const u8* s; if(!a2dp_find_sbc(A.rsp,A.rsn,&s)||!a2dp_pick_config(s,&A.rate,A.cfg)){ why="headphones offer no usable SBC setting"; goto bad; } }
    { u8 d[12]={(u8)(A.acp<<2),1<<2,1,0,7,6,0,0,A.cfg[0],A.cfg[1],A.cfg[2],A.cfg[3]}; if(!sig_cmd(AV_SETCONF,d,12,3000))goto bad; }
    if(sbc_enc_init(&A.enc,A.rate,2,(A.cfg[0]&1)?SBC_MODE_JOINT:SBC_MODE_STEREO,8,16,(A.cfg[1]&1)?SBC_AM_LOUDNESS:SBC_AM_SNR,A.cfg[3])<0)goto bad;
    { u8 d=(u8)(A.acp<<2); if(!sig_cmd(AV_OPEN,&d,1,3000))goto bad; }
    A.med=l2cap_connect(c,0x0019,0,to/2); if(!A.med){ why="media channel refused"; goto bad; }
    A.med->closed=med_closed;
    { u8 d=(u8)(A.acp<<2); if(!sig_cmd(AV_START,&d,1,3000))goto bad; }
    A.t0=hr_ns(); A.up=1; fg_ok(&a2g); a2_attach(1);
    if(!A.avc){ l2chan_t* v=l2cap_connect(c,0x0017,avc_rx,2000); if(v)A.avc=v; }
    a2_say(A.rate==48000?"streaming SBC 48 kHz":"streaming SBC 44.1 kHz");
    return 1;
bad:
    if(A.err)why=A.err;
    a2_say(why); fg_fail(&a2g,why);
    if(A.med){ l2cap_close(A.med,500); A.med=0; }
    if(A.sig){ A.sig->closed=0; l2cap_close(A.sig,500); A.sig=0; }
    return 0;
}
void a2dp_disconnect(void){
    if(A.sig&&A.sig->state==L2_OPEN&&A.acp){ u8 d=(u8)(A.acp<<2); sig_cmd(AV_CLOSE,&d,1,1000); }
    a2_down("stopped");
    if(A.med){ A.med->closed=0; l2cap_close(A.med,500); A.med=0; }
    if(A.avc){ l2cap_close(A.avc,300); A.avc=0; }
    if(A.sig){ A.sig->closed=0; l2cap_close(A.sig,500); A.sig=0; }
}
int a2dp_active(void){ return A.up; }
int a2dp_text(char* p,char* e){
    char* s=p; const char* t=!a2dp_enabled()?"Bluetooth audio: off\n":A.up?(A.paused?"Bluetooth audio: paused\n":A.rate==48000?"Bluetooth audio: streaming (SBC 48 kHz)\n":"Bluetooth audio: streaming (SBC 44.1 kHz)\n"):"Bluetooth audio: no headphones connected\n";
    while(*t&&p<e)*p++=*t++;
    return (int)(p-s);
}
void a2dp_set_enabled(int on){ fg_enable(&a2g,on); if(!on)a2dp_disconnect(); }
int a2dp_routed(void){ return A.up&&a2att; }
int a2dp_route(int bt){ if(!A.up)return 0; a2_attach(bt?1:0); return a2dp_routed(); }
const char* a2dp_reason(void){ return a2g.dead?"turned off after repeated failures":""; }
static int a2_accept(l2chan_t* ch){
    if(!bt_srv_enabled()||!a2dp_enabled()||!sdp_srv_trusted(ch->c)){ a2_say("incoming stream channel refused (not encrypted, bonded headphones)"); return 0; }
    if(A.inc&&A.sig&&A.sig->state!=L2_CLOSED&&A.c==ch->c&&!A.med&&A.as.st>=2){ A.med=ch; ch->rx=0; ch->closed=med_closed; return 1; }
    if(A.up||(A.sig&&A.sig->state!=L2_CLOSED))return 0;
    if(!fg_begin(&a2g))return 0;
    u8* z=(u8*)&A; for(unsigned i=0;i<sizeof A;i++)z[i]=0;
    A.c=ch->c; A.vol=(u8)g_snd_vol; A.inc=1; A.sig=ch; ch->rx=sig_rx; ch->closed=sig_closed; return 1;
}
int a2dp_listen(void){ return l2cap_server(0x0019,a2_accept)&&l2cap_server(0x0017,avc_accept); }
