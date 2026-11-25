#include <stdio.h>
#include <string.h>
#include "net/bluetooth/conn.c"
#include "net/bluetooth/l2cap.c"
#include "net/bluetooth/sbc/sbc_enc.c"
#include "net/bluetooth/profiles/a2dp.c"
#include "net/bluetooth/profiles/sdp_srv.c"
int hfp_msbc_enabled(void){ return 1; }
int hfp_listen(void){ return 1; }
static int fails, checks;
static int bonded=1, peer_mtu=0x029F;
btkey_t* bt_keys_find(const u8* a,int le){ static btkey_t k; (void)a; return bonded&&!le?&k:0; }
#define CHECK(c,m) do{ checks++; if(c)printf("  %s ok\n",m); else { printf("  FAIL %s\n",m); fails++; } }while(0)
volatile u32 g_ml_beat; static u64 now;
struct nvx_snd g_snd; int g_snd_state=1, g_snd_vol=80;

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
int bt_hci_send(u16 op,const u8* p,int n){ (void)op; (void)p; (void)n; return 0; }
int bt_hci_cmd(u16 op,const u8* p,int n,u8* rp,int rcap,int ms){ (void)p; (void)n; (void)rcap; (void)ms;
    if(op==0x1005){ u8 r[8]={0,0xFD,0x03,0,8,0,0,0}; memcpy(rp,r,8); return 8; } rp[0]=0; return 1; }
int bt_hci_wait_evt(int code,int sub,u8* o,int cap,int ms){ (void)code; (void)sub; (void)o; (void)cap; (void)ms; bt_poll(); return -1; }
static u8 pend[32][1100]; static int pendn[32], ph, pt;
static void l2rx(u16 cid,const u8* d,int n){ u8* b=pend[pt]; pendn[pt]=n+8; pt=(pt+1)&31; b[0]=0x42; b[1]=0x20; b[2]=(u8)(n+4); b[3]=(u8)((n+4)>>8); b[4]=(u8)n; b[5]=(u8)(n>>8); b[6]=(u8)cid; b[7]=(u8)(cid>>8); memcpy(b+8,d,n); }
void bt_poll(void){ while(evh!=evt){ int k=evh; evh=(evh+1)&15; bt_host_event(evq[k],evn[k]); } while(ph!=pt){ int k=ph; ph=(ph+1)&31; bt_acl_rx(pend[k],pendn[k]); } }
static int lc_matches_remote(int k,u16 d){ return lc[k].state!=L2_CLOSED&&lc[k].rcid==d; }
static u16 lc_l(int k){ return lc[k].lcid; }
static u16 pcid=0x70, sig_l, med_l, sig_r, med_r; static int media_pkts, media_bytes, reject_cmd=-1, sdp_q; static u8 last_cfg[12]; static u8 media_first[1100]; static int media_first_n;
static u16 in_cid; static int in_res=-1, in_n; static u8 in_rsp[64];
static void peer_l2(u16 cid,const u8* s,int n){
    if(cid==1&&s[0]==0x03){ in_res=s[8]|(s[9]<<8); return; }
    if(in_cid&&cid==in_cid){ in_n=n<64?n:64; memcpy(in_rsp,s,in_n); return; }
    if(cid==1){
        if(s[0]==0x02){ u16 psm=s[4]|(s[5]<<8), sc=s[6]|(s[7]<<8), my=pcid++; if(psm==0x19){ if(!sig_l){ sig_l=sc; sig_r=my; } else { med_l=sc; med_r=my; } }
            u8 r[12]={0x03,s[1],8,0,(u8)my,(u8)(my>>8),(u8)sc,(u8)(sc>>8),0,0,0,0}; l2rx(1,r,12); u8 c[12]={0x04,0x77,8,0,(u8)sc,(u8)(sc>>8),0,0,0x01,2,(u8)peer_mtu,(u8)(peer_mtu>>8)}; l2rx(1,c,12); }
        else if(s[0]==0x04){ u8 r[10]={0x05,s[1],6,0,0x70,0,0,0,0,0}; u16 d=s[4]|(s[5]<<8); u16 src=d==sig_r?sig_l:d==med_r?med_l:d==0x70?0:(u16)(d); (void)src; r[4]=0; r[5]=0;
            for(int i=0;i<12;i++){} u16 lc=0; for(int k=0;k<BT_MAX_CHAN;k++) if(lc_matches_remote(k,d))lc=lc_l(k); r[4]=(u8)lc; r[5]=(u8)(lc>>8); l2rx(1,r,10); }
        else if(s[0]==0x06){ u8 r[8]={0x07,s[1],4,0,s[4],s[5],s[6],s[7]}; l2rx(1,r,8); }
        return; }
    if(cid==sig_r&&sig_l){ u8 lbl=s[0]>>4, id=s[1]; u8 r[64]; int k=2; r[0]=(u8)((lbl<<4)|2); r[1]=id;
        if(id==reject_cmd){ r[0]=(u8)((lbl<<4)|3); r[k++]=0x29; l2rx(sig_l,r,k); return; }
        if(id==1){ r[k++]=(1<<2)|2; r[k++]=0x08; r[k++]=(2<<2); r[k++]=0x08; }
        else if(id==2){ u8 c[]={1,0,7,6,0,0,0xFF,0xFF,2,53}; memcpy(r+k,c,10); k+=10; }
        else if(id==3){ memcpy(last_cfg,s+2,n-2>12?12:n-2); }
        l2rx(sig_l,r,k); return; }
    if(cid==med_r&&med_l){ if(!media_pkts){ memcpy(media_first,s,n); media_first_n=n; } media_pkts++; media_bytes+=n; return; }
    if(cid>=0x40){ sdp_q++; u8 r[16]={0x07,s[1],s[2],0,9,0,6,0x35,4,0x35,2,0x09,0x00,0}; for(int k=0;k<BT_MAX_CHAN;k++) if(lc[k].state==L2_OPEN&&lc[k].psm==1){ l2rx(lc[k].lcid,r,13); } }
}


int btusb_bulk_out(const u8* p,int n,int ms){ (void)ms; u8 e[7]={0x13,5,1,p[0],(u8)(p[1]&15),1,0}; ev(e,7); if(n>=8){ u16 cid=p[6]|(p[7]<<8); peer_l2(cid,p+8,n-8); } return n; }
int main(void){
    setvbuf(stdout,0,_IONBF,0); bt_conn_init(); l2cap_init();
    u8 cc[13]={0x03,11,0,0x42,0x00,1,2,3,4,5,6,0x01,0}; bt_host_event(cc,13);
    btconn_t* c=bt_conn_by_addr((u8[]){1,2,3,4,5,6},0); CHECK(c&&c->state==BTC_UP,"ACL link to headphones up"); c->enc=1; c->keysz=16;
    { const u8 caps[]={1,0,7,6,0,0,0x3F,0xFF,2,53}; const u8* s; int rate; u8 cfg[4]; CHECK(a2dp_find_sbc(caps,10,&s)&&a2dp_pick_config(s,&rate,cfg)==51&&rate==48000&&cfg[0]==0x11&&cfg[1]==0x15,"capabilities: SBC found, 48 kHz joint stereo 16 blocks 8 subbands loudness bitpool 51 chosen"); }
    { const u8 caps[]={0x2E,0xFF,2,35}; int rate; u8 cfg[4]; CHECK(a2dp_pick_config(caps,&rate,cfg)==35&&rate==44100&&cfg[0]==0x22&&cfg[3]==35,"44.1 kHz-only stereo sink: 44.1 kHz stereo, bitpool clamped to max 35"); }
    { const u8 caps[]={0x8F,0xFF,2,53}; int rate; u8 cfg[4]; CHECK(!a2dp_pick_config(caps,&rate,cfg),"16 kHz-only sink refused"); }
    { const u8 sep[]={(1<<2)|2,0x08,(3<<2),0x00,(2<<2),0x08}; u8 a=0; CHECK(a2dp_parse_sep(sep,6,&a)&&a==2,"discover: skips in-use and source endpoints, picks free audio sink SEID 2"); }
    CHECK(a2dp_frames_fit(119,672)==5&&a2dp_frames_fit(119,895)==7&&a2dp_frames_fit(119,100)==0,"frames per media packet from MTU");
    { static sbcenc_t e; sbc_enc_init(&e,48000,2,SBC_MODE_JOINT,8,16,0,51); static u8 pcm[5*512], o[1024]; memset(pcm,0,sizeof pcm); int n=a2dp_media_packet(&e,pcm,5,0x1234,0xA0B0C0D0,80,o,sizeof o);
      CHECK(n==13+5*e.framelen&&o[0]==0x80&&o[1]==0x60&&o[2]==0x12&&o[3]==0x34&&o[4]==0xA0&&o[7]==0xD0&&o[12]==5&&o[13]==0x9C,"media packet: RTP v2 PT 96, seq, timestamp, SBC header with frame count, frames follow"); }
    int r=a2dp_connect(c,8000);
    CHECK(r==1&&a2dp_active()&&sdp_q==1,"SDP query, AVDTP discover/get caps/set config/open/start: streaming");
    CHECK(last_cfg[0]==(2<<2)&&last_cfg[2]==1&&last_cfg[4]==7&&last_cfg[6]==0&&last_cfg[7]==0&&last_cfg[8]==0x11&&last_cfg[11]==51,"set configuration names SEID 2, media transport + SBC 48k joint bitpool 51");
    CHECK(g_snd.write==a2_write&&g_snd.rate==48000&&g_snd_state==1,"system sound output routed to the A2DP sink");
    static u8 pcm[48000*4]; for(int i=0;i<48000*2;i++){ short v=(short)((i/2%96<48)?6000:-6000); pcm[2*i]=(u8)v; pcm[2*i+1]=(u8)(v>>8); }
    int fed=0; u64 t_end=now+1000000000ULL; while(now<t_end){ if(fed<(int)sizeof pcm){ int k=g_snd.write(pcm+fed,(int)sizeof pcm-fed); fed+=k; } g_snd.poll(); bt_poll(); hr_delay_us(2000); }
    int frames=media_bytes>0?(media_bytes-13*media_pkts)/A.enc.framelen:0;
    CHECK(media_pkts>20&&frames>=360&&frames<=390,"1 s of PCM paced into ~375 SBC frames on the media channel");
    CHECK(media_first_n>13&&media_first[0]==0x80&&media_first[13]==0x9C&&media_first_n<=A.med->omtu,"first media packet is RTP+SBC and fits the peer MTU");
    { u8 s[8]={(u8)((5<<4)|0),AV_SUSPEND}; l2rx(sig_l==0?0:sig_r,s,2); u16 lcid=A.sig->lcid; pendn[(pt-1)&31]=0; ph=pt; l2rx(lcid,s,2); bt_poll(); int before=media_pkts; for(int i=0;i<50;i++){ g_snd.poll(); bt_poll(); hr_delay_us(2000); }
      CHECK(A.paused&&media_pkts==before,"peer SUSPEND accepted, no media while suspended"); }
    { u8 s[2]={(u8)((6<<4)|0),AV_START}; l2rx(A.sig->lcid,s,2); bt_poll(); CHECK(!A.paused,"peer START resumes"); }
    { u8 p[8]={0x10,0x11,0x0E,0x00,0x48,0x7C,0x41,0x00}; int v0=g_snd_vol; l2chan_t ch; memset(&ch,0,sizeof ch); avc_rx(&ch,p,8); CHECK(g_snd_vol==v0,"AVRCP frame on a channel without the headphones' link ignored");
      static btconn_t other; other=*c; other.handle=0x99; ch.c=&other; avc_rx(&ch,p,8); CHECK(g_snd_vol==v0&&!avc_accept(&ch),"AVRCP from another (spoofed) connection ignored and refused");
      ch.c=c; c->enc=0; avc_rx(&ch,p,8); CHECK(g_snd_vol==v0&&!avc_accept(&ch),"AVRCP on the unencrypted link ignored and refused"); c->enc=1;
      bonded=0; avc_rx(&ch,p,8); CHECK(g_snd_vol==v0&&!avc_accept(&ch),"AVRCP from an unbonded address ignored and refused"); bonded=1;
      A.avc=0; CHECK(avc_accept(&ch)&&A.avc==&ch,"AVRCP channel on the encrypted, bonded A2DP link accepted"); ch.state=L2_OPEN; A.avc=&ch;
      avc_rx(&ch,p,8); CHECK(g_snd_vol==v0+5,"AVRCP pass-through VOLUME UP raises system volume"); p[6]=0x46; avc_rx(&ch,p,8); CHECK(A.paused,"AVRCP PAUSE pauses the stream"); p[6]=0x44; avc_rx(&ch,p,8); CHECK(!A.paused,"AVRCP PLAY resumes"); }
    { void* w0=(void*)g_snd.write; int r0=a2dp_route(0); CHECK(!r0&&a2dp_active()&&!a2dp_routed()&&g_snd.write!=a2_write,"route to speakers while connected: previous output back, stream stays up"); (void)w0; int r1=a2dp_route(1); CHECK(r1&&a2dp_routed()&&g_snd.write==a2_write,"route back to the headphones"); }
    bt_disconnect(c,0x13,100); u8 dc[6]={0x05,4,0,0x42,0x00,0x16}; bt_host_event(dc,6);
    CHECK(!a2dp_active()&&g_snd.write!=a2_write,"link loss restores the previous sound output");
    reject_cmd=3; bt_host_event(cc,13); c=bt_conn_by_addr((u8[]){1,2,3,4,5,6},0); sig_l=med_l=0; int f0=fg_fails; r=a2dp_connect(c,8000);
    CHECK(r==0&&!a2dp_active()&&fg_fails==f0+1&&g_snd.write!=a2_write,"rejected SET CONFIGURATION: setup fails, featguard counts a failure, output unchanged");
    { reject_cmd=-1; peer_mtu=1691; bt_disconnect(c,0x13,100); bt_host_event(dc,6); bt_host_event(cc,13); c=bt_conn_by_addr((u8[]){1,2,3,4,5,6},0); c->enc=1; c->keysz=16; sig_l=med_l=0; media_pkts=media_bytes=0; media_first_n=0;
      r=a2dp_connect(c,8000); CHECK(r==1&&A.med&&A.med->omtu==1691,"sink with L2CAP MTU 1691 streams");
      int fed2=0; u64 t2=now+300000000ULL; while(now<t2){ if(fed2<(int)sizeof pcm){ int k=g_snd.write(pcm+fed2,(int)sizeof pcm-fed2); fed2+=k; } g_snd.poll(); bt_poll(); hr_delay_us(2000); }
      CHECK(media_pkts>5&&media_first_n>13&&media_first_n<=1024&&media_first[12]==(1024-13)/A.enc.framelen,"MTU 1691: media packets sent, sized to the 1024-byte packet buffer (not silence)"); }
    { a2acp_t s; memset(&s,0,sizeof s); u8 r[16]; int rn, k;
      k=a2dp_acp_cmd(&s,AV_DISCOVER,0,0,r,&rn); CHECK(k==2&&rn==2&&r[0]==0x04&&r[1]==0x00,"acceptor DISCOVER: SEID 1, free, audio source");
      u8 g=0x04; k=a2dp_acp_cmd(&s,AV_GETCAP,&g,1,r,&rn); CHECK(k==2&&rn==10&&r[2]==7&&r[6]==0x33&&r[7]==0x17&&r[9]==53,"acceptor GET_CAPABILITIES: SBC 44.1/48 kHz, stereo/joint, 16 blocks, 8 subbands, bitpool 2-53");
      u8 g2=0x08; k=a2dp_acp_cmd(&s,AV_GETCAP,&g2,1,r,&rn); CHECK(k==3&&r[0]==0x12,"GET_CAPABILITIES for SEID 2: BAD_ACP_SEID");
      k=a2dp_acp_cmd(&s,AV_GETCAP,0,0,r,&rn); CHECK(k==3&&r[0]==0x11,"GET_CAPABILITIES without a SEID: BAD_LENGTH");
      u8 o1=0x04; k=a2dp_acp_cmd(&s,AV_OPEN,&o1,1,r,&rn); CHECK(k==3&&r[0]==0x31,"OPEN before SET_CONFIGURATION: BAD_STATE");
      u8 bf[]={0x04,0x08,1,0,7,6,0,0,0x81,0x15,2,40}; k=a2dp_acp_cmd(&s,AV_SETCONF,bf,12,r,&rn); CHECK(k==3&&r[0]==7&&r[1]==0x29&&!s.st,"SET_CONFIGURATION with 16 kHz: UNSUPPORTED_CONFIGURATION");
      u8 cp[]={0x04,0x08,1,0,4,2,2,0,7,6,0,0,0x21,0x15,2,40}; k=a2dp_acp_cmd(&s,AV_SETCONF,cp,16,r,&rn); CHECK(k==3&&r[0]==4&&r[1]==0x17,"SET_CONFIGURATION asking for content protection: BAD_SERV_CATEGORY");
      u8 tr[]={0x04,0x08,1,0,7,9,0,0,0x21,0x15}; k=a2dp_acp_cmd(&s,AV_SETCONF,tr,10,r,&rn); CHECK(k==3&&r[1]==0x11,"SET_CONFIGURATION with a category longer than the packet: BAD_LENGTH");
      u8 hi[]={0x04,0x08,1,0,7,6,0,0,0x21,0x15,2,250}; k=a2dp_acp_cmd(&s,AV_SETCONF,hi,12,r,&rn); CHECK(k==2&&s.st==1&&s.cfg[3]==53&&s.rate==44100&&s.intseid==2,"SET_CONFIGURATION 44.1 kHz joint, max bitpool 250: accepted, encoder bitpool 53");
      k=a2dp_acp_cmd(&s,AV_SETCONF,hi,12,r,&rn); CHECK(k==3&&r[1]==0x13,"second SET_CONFIGURATION: SEP_IN_USE");
      u8 st1=0x04; k=a2dp_acp_cmd(&s,AV_START,&st1,1,r,&rn); CHECK(k==3&&r[1]==0x31,"START before OPEN: BAD_STATE");
      k=a2dp_acp_cmd(&s,AV_OPEN,&o1,1,r,&rn); CHECK(k==2&&s.st==2,"OPEN accepted");
      k=a2dp_acp_cmd(&s,AV_START,&st1,1,r,&rn); CHECK(k==2&&s.st==3,"START accepted");
      k=a2dp_acp_cmd(&s,AV_SUSPEND,&st1,1,r,&rn); CHECK(k==2&&s.st==2,"SUSPEND accepted");
      k=a2dp_acp_cmd(&s,0x0B,&st1,1,r,&rn); CHECK(k==1,"unknown command: general reject");
      k=a2dp_acp_cmd(&s,AV_CLOSE,&st1,1,r,&rn); CHECK(k==2&&!s.st,"CLOSE returns the endpoint to idle"); }
    { a2dp_disconnect(); bt_host_event(cc,13); c=bt_conn_by_addr((u8[]){1,2,3,4,5,6},0); c->enc=0; c->keysz=16; bonded=1; CHECK(a2dp_listen(),"AVDTP and AVCTP listeners registered");
      u8 q[8]={0x02,0x31,4,0,0x19,0,0x60,0}; l2rx(1,q,8); bt_poll(); CHECK(in_res==3&&!A.sig,"incoming AVDTP from an unencrypted link refused");
      c->enc=1; in_res=-1; in_cid=0x60; q[1]=0x32; l2rx(1,q,8); bt_poll(); u16 lcid=0; for(int i=0;i<BT_MAX_CHAN;i++)if(lc[i].state!=L2_CLOSED&&lc[i].rcid==0x60)lcid=lc[i].lcid;
      u8 cf[8]={0x04,0x33,4,0,(u8)lcid,(u8)(lcid>>8),0,0}; l2rx(1,cf,8); bt_poll(); CHECK(in_res==0&&A.inc&&A.sig&&A.sig->state==L2_OPEN,"incoming AVDTP signalling from the bonded, encrypted headphones accepted");
      u8 d1[2]={0x10,0x01}; l2rx(lcid,d1,2); bt_poll(); CHECK(in_n==4&&in_rsp[0]==0x12&&in_rsp[1]==1&&in_rsp[2]==0x04,"incoming DISCOVER answered over L2CAP");
      u8 sc[14]={0x20,0x03,0x04,0x08,1,0,7,6,0,0,0x11,0x15,2,40}; l2rx(lcid,sc,14); bt_poll(); CHECK(in_n==2&&in_rsp[0]==0x22&&A.as.st==1&&A.acp==2,"incoming SET_CONFIGURATION (48 kHz joint, bitpool 40) accepted");
      u8 op[3]={0x30,0x06,0x04}; l2rx(lcid,op,3); bt_poll(); CHECK(in_rsp[0]==0x32&&A.as.st==2,"incoming OPEN accepted");
      u8 m[8]={0x02,0x34,4,0,0x19,0,0x61,0}; l2rx(1,m,8); bt_poll(); u16 ml=0; for(int i=0;i<BT_MAX_CHAN;i++)if(lc[i].state!=L2_CLOSED&&lc[i].rcid==0x61)ml=lc[i].lcid;
      u8 mc[8]={0x04,0x35,4,0,(u8)ml,(u8)(ml>>8),0,0}; l2rx(1,mc,8); bt_poll(); CHECK(A.med&&A.med->rcid==0x61&&A.med->state==L2_OPEN,"incoming media channel bound to the stream");
      u8 sa[3]={0x40,0x07,0x04}; l2rx(lcid,sa,3); bt_poll(); CHECK(in_rsp[0]==0x42&&A.up&&a2dp_routed()&&A.rate==48000&&A.cfg[3]==40,"incoming START: streaming SBC 48 kHz, sound output switched");
      u8 q2[8]={0x02,0x36,4,0,0x19,0,0x62,0}; in_res=-1; l2rx(1,q2,8); bt_poll(); CHECK(in_res==3,"third AVDTP channel while streaming refused");
      u8 cl[3]={0x50,0x08,0x04}; l2rx(lcid,cl,3); bt_poll(); CHECK(in_rsp[0]==0x52&&!A.up&&!a2dp_routed(),"incoming CLOSE stops the stream and restores the output");
      in_cid=0; }
    printf(fails?"A2DP HOST TEST: %d FAIL of %d\n":"A2DP HOST TEST: ALL PASS (%d checks)\n",fails?fails:checks,checks);
    return fails!=0;
}
