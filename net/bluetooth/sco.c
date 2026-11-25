#include "include/nvx/base.h"
#include "include/nvx/core/klog.h"
#include "include/nvx/arch/x86_64/time/hrtimer.h"
#include "net/bluetooth/bt_host.h"
#include "net/bluetooth/profiles/hfp.h"
#include "net/bluetooth/sbc/sbc.h"
__attribute__((weak)) int hfp_sco_peer(const u8* addr){ (void)addr; return 0; }
__attribute__((weak)) int btusb_sco_start(int alt){ (void)alt; return 0; }
__attribute__((weak)) void btusb_sco_stop(void){}
__attribute__((weak)) int btusb_sco_mps(void){ return 0; }
__attribute__((weak)) int hfp_codec(void){ return 1; }
__attribute__((weak)) void hfp_codec_fallback(void){}
#define SCO_RING 8192
enum { SC_IDLE, SC_SETUP, SC_UP };
static struct { u8 st, addr[6], air, link, audio, codec, retry; u16 h, acl; int pkt; u64 t0; } S;
static scoreasm_t SR; static u8 S_rx[SCO_RING]; static u32 S_rr, S_rw; static int S_txo; static u8 S_txp[3+255]; static sbcenc_t S_enc; static msbcdec_t S_dec; static u8 S_mf[60], S_mtx[60]; static int S_mfn, S_mto=60, S_seq;
int bt_sco_reasm(scoreasm_t* r,const u8* p,int n,u16 h,void (*out)(const u8*,int)){
    int pk=0;
    for(int i=0;i<n;i++){ r->b[r->n++]=p[i];
        if(r->n==3&&((u16)((r->b[0]|(r->b[1]<<8))&0xFFF)!=h||r->b[2]>(r->max?r->max:BT_SCO_MTU))){ r->b[0]=r->b[1]; r->b[1]=r->b[2]; r->n=2; r->bad++; continue; }
        if(r->n>=3&&r->n==3+r->b[2]){ if(r->b[2]&&out)out(r->b+3,r->b[2]); r->n=0; pk++; } }
    return pk;
}
static void sco_put(const u8* p,int n){ for(int i=0;i<n;i++){ if(S_rw-S_rr>=SCO_RING)S_rr++; S_rx[S_rw%SCO_RING]=p[i]; S_rw++; } }
static const u8 h2s[4]={0x08,0x38,0xC8,0xF8};
int msbc_h2_rx(msbcdec_t* d,u8* f,int* fn,const u8* p,int n,void (*out)(const u8*,int)){
    int got=0;
    for(int i=0;i<n;i++){ u8 b=p[i];
        if(*fn==0&&b!=0x01)continue;
        if(*fn==1&&b!=0x08&&b!=0x38&&b!=0xC8&&b!=0xF8){ *fn=b==0x01; continue; }
        if(*fn==2&&b!=0xAD){ *fn=b==0x01; continue; }
        f[(*fn)++]=b;
        if(*fn==60){ int16_t pcm[120]; if(msbc_dec_frame(d,f+2,57,pcm)==120){ if(out)out((const u8*)pcm,240); got++; } *fn=0; } }
    return got;
}
int msbc_h2_frame(sbcenc_t* e,int seq,const int16_t* pcm,u8* o){ o[0]=0x01; o[1]=h2s[seq&3]; if(sbc_enc_frame(e,(const u8*)pcm,o+2,58)!=MSBC_FRAME)return -1; o[59]=0; return 60; }
static void sco_rx_pl(const u8* p,int n){ if(S.codec==2)msbc_h2_rx(&S_dec,S_mf,&S_mfn,p,n,sco_put); else sco_put(p,n); }
void bt_sco_rx_stream(const u8* p,int n){ if(S.st==SC_UP&&S.audio)bt_sco_reasm(&SR,p,n,S.h,sco_rx_pl); }
static u8 sco_tx_byte(void){ if(S.codec!=2)return 0; if(S_mto>=60){ static const int16_t z[120]; if(msbc_h2_frame(&S_enc,S_seq++,z,S_mtx)<0){ for(int k=0;k<60;k++)S_mtx[k]=0; } S_mto=0; } return S_mtx[S_mto++]; }
int bt_sco_tx_stream(u8* o,int n){
    int pk=S.pkt>0&&S.pkt<=255?S.pkt:BT_SCO_MTU;
    for(int i=0;i<n;i++){ if(!S_txo){ S_txp[0]=(u8)S.h; S_txp[1]=(u8)(S.h>>8); S_txp[2]=(u8)pk; for(int k=0;k<pk;k++)S_txp[3+k]=sco_tx_byte(); }
        o[i]=S_txp[S_txo++]; if(S_txo>=3+pk)S_txo=0; }
    return n;
}
int bt_iso_refill(u32 enq,u32 done,int valid,u8 cc,int target){
    int n=0; if(valid&&enq<255&&done<255){ int fl=(int)((enq+255-(done+1)%255)%255); n=target-fl; } else if(cc==14||cc==15)n=target;
    return n<0?0:n>target?target:n;
}
int bt_sco_pktlen(int mps,int mtu){ int b=mps<=0?BT_SCO_MTU:mps>=63?mps-3:3*mps-3; if(b<=0||b>255)b=BT_SCO_MTU; if(mtu>0&&mtu<b)b=mtu; return b; }
int bt_sco_codec(void){ return S.st==SC_UP?S.codec:0; }
int bt_sco_rate(void){ return S.codec==2?16000:8000; }
int bt_sco_read(u8* o,int n){ int k=0; while(k<n&&S_rr!=S_rw){ o[k++]=S_rx[S_rr%SCO_RING]; S_rr++; } return k; }
static u16 sg16(const u8* p){ return (u16)(p[0]|(p[1]<<8)); }
static u8* sp16(u8* p,u16 v){ p[0]=(u8)v; p[1]=(u8)(v>>8); return p+2; }
static u8* sp32(u8* p,u32 v){ p=sp16(p,(u16)v); return sp16(p,(u16)(v>>16)); }
static u8* sco_params(u8* p,int codec){ p=sp32(p,8000); p=sp32(p,8000);
    if(codec==2){ p=sp16(p,0x000D); p=sp16(p,0x0063); *p++=0x02; return sp16(p,0x0388); }
    p=sp16(p,0xFFFF); p=sp16(p,0x0060); *p++=0xFF; return sp16(p,0x003F); }
int bt_sco_conn_req(const u8* p,int pl){
    if(pl<10||(p[9]!=0&&p[9]!=2))return 0;
    u8 d[21]; for(int i=0;i<6;i++)d[i]=p[i];
    if(S.st==SC_UP||!hfp_sco_peer(p)){ d[6]=0x0F; bt_hci_send(0x042A,d,7); klog("bluetooth: voice link request refused (not the connected, bonded headset)"); return 1; }
    S.codec=(u8)(hfp_codec()==2?2:1); S.retry=0; sco_params(d+6,S.codec); S.st=SC_SETUP; { btconn_t* ac=bt_conn_by_addr(p,0); S.acl=ac?ac->handle:0; } S.t0=hr_ns(); for(int i=0;i<6;i++)S.addr[i]=p[i];
    bt_hci_send(0x0429,d,21); return 1;
}
int bt_sco_setup(btconn_t* c){
    if(!c||c->le||c->state!=BTC_UP||!hfp_sco_peer(c->addr))return 0;
    if(S.st==SC_UP)return 1;
    if(S.st==SC_SETUP&&hr_ns()-S.t0<10000000000ULL)return 1;
    u8 d[17]; S.codec=(u8)(hfp_codec()==2?2:1); S.retry=0; sp16(d,c->handle); sco_params(d+2,S.codec);
    S.st=SC_SETUP; S.t0=hr_ns(); S.acl=c->handle; for(int i=0;i<6;i++)S.addr[i]=c->addr[i];
    if(bt_hci_send(0x0428,d,17)<0){ S.st=SC_IDLE; return 0; }
    return 1;
}
void bt_sco_event(const u8* e,int n){
    if(n<2){ return; } int code=e[0], pl=e[1]; if(pl+2>n)return; const u8* p=e+2;
    if(code==0x2C&&pl>=17){
        int same=1; for(int i=0;i<6;i++)if(p[3+i]!=S.addr[i])same=0;
        if(p[0]){ if(same&&S.st==SC_SETUP){
                if(S.codec==2&&!S.retry){ hfp_codec_fallback(); u8 d[17]; S.codec=1; S.retry=1; sp16(d,S.acl); sco_params(d+2,1); klog("bluetooth: wide-band voice link refused, retrying with CVSD"); if(S.acl&&bt_hci_send(0x0428,d,17)>=0){ S.t0=hr_ns(); return; } }
                S.st=SC_IDLE; klog("bluetooth: voice link setup failed"); } return; }
        u16 h=(u16)(sg16(p+1)&0xFFF);
        if(!same||S.st!=SC_SETUP||!hfp_sco_peer(p+3)){ u8 d[3]; sp16(d,h); d[2]=0x13; bt_hci_send(0x0406,d,3); return; }
        S.h=h; S.link=p[9]; S.air=p[16]; S.st=SC_UP; SR.n=0; S_rr=S_rw=0; S_txo=0; S_mfn=0; S_mto=60; S_seq=0;
        if(S.air==3&&S.codec!=2)S.codec=2; else if(S.air!=3)S.codec=1;
        if(S.codec==2){ sbc_enc_init_msbc(&S_enc); msbc_dec_init(&S_dec); }
        S.audio=(u8)(btusb_sco_start(S.codec==2?6:2)>0); S.pkt=bt_sco_pktlen(S.audio?btusb_sco_mps():0,bt_hci_sco_mtu()); SR.max=S.pkt>BT_SCO_MTU?S.pkt:BT_SCO_MTU;
        klog(S.codec==2?(S.audio?"bluetooth: voice link up (mSBC wide-band 16 kHz over USB isochronous)":"bluetooth: voice link up (mSBC); no audio: the adapter has no usable isochronous endpoint"):S.audio?"bluetooth: voice link up (CVSD 8 kHz over USB isochronous)":"bluetooth: voice link up (CVSD); no audio: the adapter has no usable isochronous endpoint"); }
    else if(code==0x05&&pl>=4&&!p[0]&&S.st!=SC_IDLE){ u16 h=(u16)(sg16(p+1)&0xFFF); if((S.st==SC_UP&&h==S.h)||h==S.acl){ if(S.audio)btusb_sco_stop(); S.audio=0; S.st=SC_IDLE; klog("bluetooth: voice link closed"); } }
}
void bt_sco_close(void){ if(S.audio)btusb_sco_stop(); S.audio=0; if(S.st==SC_UP){ u8 d[3]; sp16(d,S.h); d[2]=0x13; bt_hci_send(0x0406,d,3); } S.st=SC_IDLE; }
int bt_sco_up(void){ return S.st==SC_UP; }
int bt_sco_audio(void){ return S.st==SC_UP&&S.audio; }
const char* bt_sco_text(void){ return S.st==SC_UP?(S.audio?(S.codec==2?"wide-band voice link up, audio over USB":"voice link up, audio over USB"):"voice link up, no audio path"):S.st==SC_SETUP?"voice link being set up":"no voice link"; }
