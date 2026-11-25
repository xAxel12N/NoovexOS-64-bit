#include "include/nvx/base.h"
#include "include/nvx/core/klog.h"
#include "include/nvx/core/sysfeat.h"
#include "net/bluetooth/bt_host.h"
#include "net/bluetooth/bt_sec.h"
#include "net/bluetooth/profiles/hfp.h"
#include "net/bluetooth/profiles/sdp_srv.h"
static u8 sd_rec[SDP_NREC][160]; static int sd_rn[SDP_NREC]; static int sd_built;
static struct featguard sd_fg={"Bluetooth incoming headset connections","feat-btin.cfg",1,0,3,0,0};
static u16 sd_b16(const u8* p){ return (u16)((p[0]<<8)|p[1]); }
static u32 sd_b32(const u8* p){ return ((u32)p[0]<<24)|((u32)p[1]<<16)|((u32)p[2]<<8)|p[3]; }
int sdp_de(const u8* p,int n,int* hl,int* ln){
    if(!p||n<1)return -1;
    int t=p[0]>>3, sz=p[0]&7, h=1, l;
    if(t==0){ if(sz)return -1; l=0; }
    else if(t==1||t==2){ if(sz>4)return -1; l=1<<sz; }
    else if(t==3){ if(sz!=1&&sz!=2&&sz!=4)return -1; l=1<<sz; }
    else if(t==5){ if(sz)return -1; l=1; }
    else if(t==4||t==6||t==7||t==8){
        if(sz==5){ if(n<2)return -1; l=p[1]; h=2; }
        else if(sz==6){ if(n<3)return -1; l=sd_b16(p+1); h=3; }
        else if(sz==7){ if(n<5)return -1; u32 v=sd_b32(p+1); if(v>(u32)n)return -1; l=(int)v; h=5; }
        else return -1; }
    else return -1;
    if(l>n-h)return -1;
    *hl=h; *ln=l; return t;
}
static const u8 sd_base[12]={0x00,0x00,0x10,0x00,0x80,0x00,0x00,0x80,0x5F,0x9B,0x34,0xFB};
static int sd_uuid(const u8* v,int l,u32* u){
    if(l==2){ *u=sd_b16(v); return 1; }
    if(l==4){ *u=sd_b32(v); return 1; }
    if(l==16){ for(int i=0;i<12;i++)if(v[4+i]!=sd_base[i])return 0; *u=sd_b32(v); return 1; }
    return 0;
}
static int sd_has(const u8* p,int n,u32 u,int depth){
    for(int o=0;o<n;){ int hl,ln,t=sdp_de(p+o,n-o,&hl,&ln); if(t<0)return 0;
        if(t==3){ u32 x; if(sd_uuid(p+o+hl,ln,&x)&&x==u)return 1; }
        else if((t==6||t==7)&&depth<4&&sd_has(p+o+hl,ln,u,depth+1))return 1;
        o+=hl+ln; }
    return 0;
}
static int sd_put(u8* o,int k,const u8* s,int n){ for(int i=0;i<n;i++)o[k+i]=s[i]; return k+n; }
static void sd_set(int i,const u8* a,int n){ u8* r=sd_rec[i]; r[0]=0x35; r[1]=(u8)n; sd_put(r,2,a,n); sd_rn[i]=n+2; }
void sdp_srv_build(u8 ch,u16 f){
    u8 h[]={0x09,0,0,0x0A,0,1,0,1, 0x09,0,1,0x35,6,0x19,0x11,0x1F,0x19,0x12,0x03, 0x09,0,4,0x35,12,0x35,3,0x19,1,0,0x35,5,0x19,0,3,0x08,ch,
        0x09,0,5,0x35,3,0x19,0x10,0x02, 0x09,0,9,0x35,8,0x35,6,0x19,0x11,0x1E,0x09,1,8,
        0x09,1,0,0x25,13,'V','o','i','c','e',' ','g','a','t','e','w','a','y', 0x09,3,1,0x08,1, 0x09,3,0x11,0x09,(u8)(f>>8),(u8)f};
    u8 a[]={0x09,0,0,0x0A,0,1,0,2, 0x09,0,1,0x35,3,0x19,0x11,0x0A, 0x09,0,4,0x35,16,0x35,6,0x19,1,0,0x09,0,0x19,0x35,6,0x19,0,0x19,0x09,1,3,
        0x09,0,5,0x35,3,0x19,0x10,0x02, 0x09,0,9,0x35,8,0x35,6,0x19,0x11,0x0D,0x09,1,3,
        0x09,1,0,0x25,12,'A','u','d','i','o',' ','s','o','u','r','c','e', 0x09,3,0x11,0x09,0,1};
    u8 v[]={0x09,0,0,0x0A,0,1,0,3, 0x09,0,1,0x35,3,0x19,0x11,0x0C, 0x09,0,4,0x35,16,0x35,6,0x19,1,0,0x09,0,0x17,0x35,6,0x19,0,0x17,0x09,1,4,
        0x09,0,5,0x35,3,0x19,0x10,0x02, 0x09,0,9,0x35,8,0x35,6,0x19,0x11,0x0E,0x09,1,4,
        0x09,1,0,0x25,14,'R','e','m','o','t','e',' ','c','o','n','t','r','o','l', 0x09,3,0x11,0x09,0,1};
    sd_set(0,h,(int)sizeof h); sd_set(1,a,(int)sizeof a); sd_set(2,v,(int)sizeof v); sd_built=1;
}
const u8* sdp_srv_record(int i,int* n){ if(i<0||i>=SDP_NREC||!sd_built)return 0; *n=sd_rn[i]; return sd_rec[i]; }
static int sd_err(u8* o,u16 tid,u16 code){ o[0]=0x01; o[1]=(u8)(tid>>8); o[2]=(u8)tid; o[3]=0; o[4]=2; o[5]=(u8)(code>>8); o[6]=(u8)code; return 7; }
static int sd_pattern(const u8* p,int n,u32* u,int* used){
    int hl,ln,t=sdp_de(p,n,&hl,&ln); if(t!=6)return -1;
    int c=0; for(int o=hl;o<hl+ln;){ int h2,l2,t2=sdp_de(p+o,hl+ln-o,&h2,&l2); if(t2!=3||c>=12)return -1;
        u[c]=0xFFFFFFFFu; sd_uuid(p+o+h2,l2,&u[c]); c++; o+=h2+l2; }
    if(!c)return -1;
    *used=hl+ln; return c;
}
static int sd_match(int r,const u32* u,int c){ int hl,ln; if(sdp_de(sd_rec[r],sd_rn[r],&hl,&ln)!=6)return 0; for(int i=0;i<c;i++)if(!sd_has(sd_rec[r]+hl,ln,u[i],0))return 0; return 1; }
static int sd_ids(const u8* p,int n,u32* lo,u32* hi,int* used){
    int hl,ln,t=sdp_de(p,n,&hl,&ln); if(t!=6)return -1;
    int c=0; for(int o=hl;o<hl+ln;){ int h2,l2,t2=sdp_de(p+o,hl+ln-o,&h2,&l2); if(t2!=1||c>=16)return -1;
        if(l2==2){ lo[c]=hi[c]=sd_b16(p+o+h2); } else if(l2==4){ lo[c]=sd_b16(p+o+h2); hi[c]=sd_b16(p+o+h2+2); if(lo[c]>hi[c])return -1; } else return -1;
        c++; o+=h2+l2; }
    if(!c)return -1;
    *used=hl+ln; return c;
}
static int sd_attrs(int r,const u32* lo,const u32* hi,int c,u8* o,int cap){
    int hl,ln; if(sdp_de(sd_rec[r],sd_rn[r],&hl,&ln)!=6)return -1;
    const u8* p=sd_rec[r]+hl; u8 b[160]; int k=0;
    for(int x=0;x+3<=ln;){ int h1,l1; if(sdp_de(p+x,ln-x,&h1,&l1)!=1||l1!=2)return -1; u16 id=sd_b16(p+x+1); int h2,l2; if(sdp_de(p+x+3,ln-x-3,&h2,&l2)<0)return -1;
        int w=3+h2+l2, want=0; for(int i=0;i<c;i++)if(id>=lo[i]&&id<=hi[i])want=1;
        if(want){ if(k+w>(int)sizeof b)return -1; k=sd_put(b,k,p+x,w); }
        x+=w; }
    if(k+2>cap)return -1;
    o[0]=0x35; o[1]=(u8)k; sd_put(o,2,b,k); return k+2;
}
int sdp_srv_req(const u8* q,int qn,u8* o,int cap){
    if(!q||!o||cap<32)return 0;
    if(!sd_built)sdp_srv_build(HFP_AG_CH,0);
    u16 tid=qn>=3?sd_b16(q+1):0;
    if(qn<5||sd_b16(q+3)!=qn-5)return sd_err(o,tid,0x0004);
    u8 pdu=q[0]; const u8* p=q+5; int n=qn-5, k=0;
    if(pdu!=0x02&&pdu!=0x04&&pdu!=0x06)return sd_err(o,tid,0x0003);
    u32 u[12], lo[16], hi[16]; int uc=0, ic=0, used, rec=-1; u16 maxb=0, maxc=0;
    if(pdu==0x02||pdu==0x06){ uc=sd_pattern(p,n,u,&used); if(uc<0)return sd_err(o,tid,0x0003); k=used; }
    else { if(n<4)return sd_err(o,tid,0x0003); u32 hd=sd_b32(p); for(int i=0;i<SDP_NREC;i++)if(hd==0x00010001u+(u32)i)rec=i; if(rec<0)return sd_err(o,tid,0x0002); k=4; }
    if(k+2>n)return sd_err(o,tid,0x0003);
    if(pdu==0x02)maxc=sd_b16(p+k); else maxb=sd_b16(p+k);
    k+=2;
    if(pdu!=0x02){ ic=sd_ids(p+k,n-k,lo,hi,&used); if(ic<0)return sd_err(o,tid,0x0003); k+=used; if(maxb<7)return sd_err(o,tid,0x0003); }
    if(k>=n)return sd_err(o,tid,0x0003);
    int cl=p[k]; if(k+1+cl!=n)return sd_err(o,tid,0x0003);
    if(cl!=0&&cl!=2)return sd_err(o,tid,0x0005);
    int off=cl==2?sd_b16(p+k+1):0;
    if(pdu==0x02){
        if(cl||!maxc)return sd_err(o,tid,cl?0x0005:0x0003);
        int m=0; for(int r=0;r<SDP_NREC&&m<maxc;r++)if(sd_match(r,u,uc)){ u32 hd=0x00010001u+(u32)r; o[9+m*4]=(u8)(hd>>24); o[10+m*4]=(u8)(hd>>16); o[11+m*4]=(u8)(hd>>8); o[12+m*4]=(u8)hd; m++; }
        int pl=4+m*4+1; o[0]=0x03; o[1]=(u8)(tid>>8); o[2]=(u8)tid; o[3]=(u8)(pl>>8); o[4]=(u8)pl; o[5]=0; o[6]=(u8)m; o[7]=0; o[8]=(u8)m; o[9+m*4]=0;
        return 5+pl; }
    u8 full[600]; int fn=0;
    if(pdu==0x04){ fn=sd_attrs(rec,lo,hi,ic,full,(int)sizeof full); if(fn<0)return sd_err(o,tid,0x0003); }
    else { fn=3; for(int r=0;r<SDP_NREC;r++)if(sd_match(r,u,uc)){ int w=sd_attrs(r,lo,hi,ic,full+fn,(int)sizeof full-fn); if(w<0)return sd_err(o,tid,0x0003); fn+=w; }
        full[0]=0x36; full[1]=(u8)((fn-3)>>8); full[2]=(u8)(fn-3); }
    if(off>=fn)return sd_err(o,tid,0x0005);
    int ch=cap-5-2-3; if(ch>maxb)ch=maxb; if(ch>fn-off)ch=fn-off;
    int more=off+ch<fn, pl=2+ch+1+(more?2:0);
    o[0]=pdu==0x04?0x05:0x07; o[1]=(u8)(tid>>8); o[2]=(u8)tid; o[3]=(u8)(pl>>8); o[4]=(u8)pl; o[5]=(u8)(ch>>8); o[6]=(u8)ch;
    sd_put(o,7,full+off,ch); int e=7+ch;
    if(more){ int no=off+ch; o[e++]=2; o[e++]=(u8)(no>>8); o[e++]=(u8)no; } else o[e++]=0;
    return e;
}
int sdp_srv_trusted(const btconn_t* c){ return c&&c->state==BTC_UP&&!c->le&&c->enc&&c->keysz>=BT_BREDR_KEYSZ&&bt_keys_find(c->addr,0)!=0; }
static void sd_rx(l2chan_t* ch,const u8* p,int n){
    if(!sdp_srv_trusted(ch->c))return;
    u8 o[256]; int cap=ch->omtu<(int)sizeof o?ch->omtu:(int)sizeof o;
    int k=sdp_srv_req(p,n,o,cap); if(k>0)l2cap_send(ch,o,k);
}
static int sd_accept(l2chan_t* ch){
    if(!bt_srv_enabled()||!sdp_srv_trusted(ch->c)){ klog("bluetooth: SDP request refused (not an encrypted, bonded device)"); return 0; }
    ch->rx=sd_rx; return 1;
}
int bt_srv_enabled(void){ fg_boot(&sd_fg); return sd_fg.on&&!sd_fg.dead; }
void bt_srv_set_enabled(int on){ fg_enable(&sd_fg,on); if(on&&bt_hci_up())bt_srv_init(); }
void bt_srv_init(void){
    if(!bt_srv_enabled()||!fg_begin(&sd_fg))return;
    sdp_srv_build(HFP_AG_CH,hfp_msbc_enabled()?0x0020:0);
    if(l2cap_server(SDP_PSM,sd_accept)&&hfp_listen()&&a2dp_listen()){ fg_ok(&sd_fg); klog("bluetooth: accepting headset connections (Hands-Free, A2DP, AVRCP)"); }
    else fg_fail(&sd_fg,"could not register the Bluetooth service listeners");
}
