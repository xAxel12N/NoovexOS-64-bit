#include "include/nvx/base.h"
#include "include/nvx/core/klog.h"
#include "include/nvx/drivers/usb/mtp.h"

#define MTP_BUF 65536
#define MTP_NC 1024
#define MTP_NL 64
#define MTP_ROOT 0xFFFFFFFFu
#define MTP_OK 0x2001
typedef struct { u32 h, parent, sid, size; u8 dir, used; char name[64]; } mtp_obj_t;
mtp_dev_t g_mtp;
static const mtp_tr_t* mt;
static u8 mbuf[MTP_BUF+16];
static mtp_obj_t mc[MTP_NC]; static int mc_n;
static struct { u32 sid, parent; } ml[MTP_NL]; static int ml_n;
static u16 g16(const u8* p){ return (u16)(p[0]|(p[1]<<8)); }
static u32 g32(const u8* p){ return (u32)p[0]|((u32)p[1]<<8)|((u32)p[2]<<16)|((u32)p[3]<<24); }
static u64 g64(const u8* p){ return (u64)g32(p)|((u64)g32(p+4)<<32); }
static void p16(u8* p,u32 v){ p[0]=(u8)v; p[1]=(u8)(v>>8); }
static void p32(u8* p,u32 v){ p[0]=(u8)v; p[1]=(u8)(v>>8); p[2]=(u8)(v>>16); p[3]=(u8)(v>>24); }
static int mlen(const char* s){ int n=0; while(s[n])n++; return n; }
static int meq(const char* a,const char* b){ while(*a&&*a==*b){ a++; b++; } return *a==*b; }
static void mcpy(char* d,const char* s,int cap){ int i=0; for(;s[i]&&i<cap-1;i++)d[i]=s[i]; d[i]=0; }
static int mstr(const u8* p,int avail,char* out,int cap){
    if(avail<1){ if(cap)out[0]=0; return 0; } int nc=p[0], o=0, used=1+nc*2; if(used>avail){ nc=(avail-1)/2; used=1+nc*2; }
    for(int k=0;k<nc;k++){ u32 c=g16(p+1+2*k); if(!c)break;
        if(c<0x80){ if(o+1<cap)out[o++]=(char)c; }
        else if(c<0x800){ if(o+2<cap){ out[o++]=(char)(0xC0|(c>>6)); out[o++]=(char)(0x80|(c&63)); } }
        else if(o+3<cap){ out[o++]=(char)(0xE0|(c>>12)); out[o++]=(char)(0x80|((c>>6)&63)); out[o++]=(char)(0x80|(c&63)); } }
    if(cap)out[o<cap?o:cap-1]=0; return used; }
static int mput_str(u8* p,const char* s,int cap){
    if(!s[0]){ p[0]=0; return 1; } int n=0, i=0; u8* q=p+1;
    while(s[i]&&n<254&&1+(n+2)*2<=cap){ u32 c=(u8)s[i]; int k=1;
        if(c>=0xE0&&s[i+1]&&s[i+2]){ c=((c&15)<<12)|(((u8)s[i+1]&63)<<6)|((u8)s[i+2]&63); k=3; }
        else if(c>=0xC0&&s[i+1]){ c=((c&31)<<6)|((u8)s[i+1]&63); k=2; }
        p16(q+2*n,c); n++; i+=k; }
    p16(q+2*n,0); n++; p[0]=(u8)n; return 1+2*n; }
int mtp_android_vid(u16 v){ static const u16 t[]={0x18D1,0x04E8,0x22B8,0x0BB4,0x12D1,0x2717,0x2A70,0x1004,0x0FCE,0x054C,0x19D2,0x1EBF,0x2B4C,0x05C6,0x0E8D,0x2D95,0x22D9,0x1BBB,0x17EF,0x0489,0x2916,0x29A9,0x2AE5};
    for(unsigned i=0;i<sizeof t/sizeof t[0];i++) if(t[i]==v)return 1; return 0; }
int mtp_str_is_mtp(const u8* s,int n){ if(n<2||s[1]!=3)return 0; int l=s[0]<n?s[0]:n;
    for(int i=2;i+5<l;i+=2) if(s[i]=='M'&&s[i+1]==0&&s[i+2]=='T'&&s[i+3]==0&&s[i+4]=='P'&&s[i+5]==0)return 1; return 0; }
int mtp_match(const u8* cfg,int n,mtp_if_t* o){
    int best=0; mtp_if_t c; int cur=0;
    for(int i=0;i+1<n;){ int bl=cfg[i]; if(bl<2||i+bl>n)break;
        if(cfg[i+1]==4&&bl>=9){
            if(cur&&c.in_ep&&c.out_ep&&cur>best){ *o=c; best=cur; }
            int cl=cfg[i+5], sc=cfg[i+6], pr=cfg[i+7]; cur=0; for(unsigned k=0;k<sizeof c;k++)((u8*)&c)[k]=0;
            if(cl==6&&sc==1&&pr==1)cur=2; else if(cl==0xFF&&cfg[i+4]==3)cur=1;
            c.iface=cfg[i+2]; c.cls=cl; c.istr=cfg[i+8]; c.in_mps=512; c.out_mps=512; }
        else if(cfg[i+1]==5&&bl>=7&&cur){ int a=cfg[i+2], at=cfg[i+3]&3, mp=g16(cfg+i+4)&0x7FF;
            if(at==2){ if(a&0x80){ if(!c.in_ep){ c.in_ep=a; c.in_mps=mp; } } else if(!c.out_ep){ c.out_ep=a; c.out_mps=mp; } }
            else if(at==3&&(a&0x80)&&!c.int_ep)c.int_ep=a;
            if(cur==1&&at!=2&&at!=3)cur=0; }
        i+=bl; }
    if(cur&&c.in_ep&&c.out_ep&&cur>best){ *o=c; best=cur; }
    if(best==1&&!o->int_ep)best=0;
    return best; }
static void mfail(int r){ if(r==MTP_E_TIMEOUT)g_mtp.timeouts++; g_mtp.errs++; if(++g_mtp.fails>=3){ g_mtp.dead=1; klog("mtp: device stopped answering - unplug and reconnect it"); } if(mt&&mt->reset)mt->reset(); }
static int mrecv(int ms){
    for(int z=0;z<3;z++){ p32(mbuf,0); int r=mt->in(mbuf,512,ms); if(r<0)return r; if(r>=12&&g32(mbuf)>=12)return r; }
    return MTP_E_PROTO; }
static int mtx(u16 op,const u32* p,int np,const u8* dout,u32 nout,u8* din,u32 cap,u32* got,u32* rp,int ms){
    if(!mt||g_mtp.dead)return MTP_E_NODEV; u8 cmd[32]; u32 tid=g_mtp.tid++;
    p32(cmd,12+4*(u32)np); p16(cmd+4,1); p16(cmd+6,op); p32(cmd+8,tid); for(int i=0;i<np;i++)p32(cmd+12+4*i,p[i]);
    int r=mt->out(cmd,12+4*np,2000); if(r<0){ mfail(r); return r; }
    if(got)*got=0;
    if(dout){ u32 tot=12+nout, sent=0; int first=1;
        while(first||sent<tot){ int ch=mt->max; u32 n; if(first){ p32(mbuf,tot); p16(mbuf+4,2); p16(mbuf+6,op); p32(mbuf+8,tid); n=tot<(u32)ch?tot:(u32)ch; for(u32 k=12;k<n;k++)mbuf[k]=dout[k-12]; first=0; }
            else { n=tot-sent; if(n>(u32)ch)n=(u32)ch; for(u32 k=0;k<n;k++)mbuf[k]=dout[sent-12+k]; }
            r=mt->out(mbuf,(int)n,ms); if(r<0){ mfail(r); return r; } sent+=n; } }
    r=din?mt->in(mbuf,mt->max,ms):mrecv(ms); if(r<0){ mfail(r); return r; }
    if(din&&(r<12||g32(mbuf)<12)){ r=mrecv(ms); if(r<0){ mfail(r); return r; } }
    if(g16(mbuf+4)==2){ u32 tot=g32(mbuf), have=(u32)r; if(have>tot)have=tot; u32 dl=tot-12, w=0;
        for(u32 k=12;k<have;k++){ if(w<cap)din[w]=mbuf[k]; w++; }
        while(w<dl){ u32 want=dl-w; if(want>(u32)mt->max)want=(u32)mt->max; r=mt->in(mbuf,(int)want,ms); if(r<0){ mfail(r); return r; } if(!r)continue;
            for(int k=0;k<r&&w<dl;k++){ if(w<cap)din[w]=mbuf[k]; w++; } }
        if(got)*got=w<cap?w:cap; if(w>cap&&got)*got=cap;
        r=mrecv(ms); if(r<0){ mfail(r); return r; } }
    if(g16(mbuf+4)!=3||g32(mbuf+8)!=tid){ mfail(MTP_E_PROTO); return MTP_E_PROTO; }
    g_mtp.fails=0; u32 rl=g32(mbuf); if(rl>(u32)r)rl=(u32)r; if(rp){ for(int i=0;i<5;i++)rp[i]=12+4*(u32)i+4<=rl?g32(mbuf+12+4*i):0; }
    g_mtp.lastrc=g16(mbuf+6); return g16(mbuf+6); }
int mtp_has_op(u16 op){ for(int i=0;i<g_mtp.nops;i++) if(g_mtp.ops[i]==op)return 1; return 0; }
static void mc_clear(void){ mc_n=0; ml_n=0; }
static u8 mdata[4096];
static int mstores(void){
    u32 got=0; int r=mtx(0x1004,0,0,0,0,mdata,sizeof mdata,&got,0,5000);
    if(r<0)return r; g_mtp.nst=0;
    if(r==MTP_OK&&got>=4){ u32 n=g32(mdata); if(n>(got-4)/4)n=(got-4)/4;
        for(u32 i=0;i<n&&g_mtp.nst<MTP_MAXST;i++){ u32 id=g32(mdata+4+4*i); if(!(id&0xFFFF))continue; mtp_store_t* s=&g_mtp.st[g_mtp.nst]; s->id=id; s->cap=0; s->freeb=0; s->ro=0; mcpy(s->desc,g_mtp.kind==MTP_K_CAMERA?"Memory card":"Storage",40);
            u32 a=id; u8 si[512]; u32 sg=0; if(mtx(0x1005,&a,1,0,0,si,sizeof si,&sg,0,5000)==MTP_OK&&sg>=27){ s->ro=g16(si+4)!=0; s->cap=g64(si+6); s->freeb=g64(si+14); char d[40]; mstr(si+26,(int)sg-26,d,40); if(d[0])mcpy(s->desc,d,40); }
            g_mtp.nst++; } }
    int was=g_mtp.locked; g_mtp.locked=g_mtp.nst==0; if(g_mtp.locked&&!was)klog("mtp: phone has no storage visible - unlock it and choose File transfer");
    g_mtp.gen++; return 0; }
int mtp_open(const mtp_tr_t* tr,u16 vid,u16 pid){
    for(unsigned k=0;k<sizeof g_mtp;k++)((u8*)&g_mtp)[k]=0; mc_clear(); mt=tr; if(!tr||tr->max<512||tr->max>MTP_BUF)return MTP_E_ARG;
    g_mtp.vid=vid; g_mtp.pid=pid; u32 got=0; int r=mtx(0x1001,0,0,0,0,mdata,sizeof mdata,&got,0,5000);
    if(r!=MTP_OK){ klog("mtp: device did not send its device info"); mt=0; return r<0?r:MTP_E_PROTO; }
    { u32 o=8; u32 vext=got>=6?g32(mdata+2):0; char vd[64]; vd[0]=0; o+=(u32)mstr(mdata+8,(int)got-8,vd,64); o+=2;
      int mtpx=vext==6; for(int i=0;vd[i];i++){ char a=vd[i]|32; if(a=='m'&&(vd[i+1]|32)=='i'&&(vd[i+2]|32)=='c')mtpx=1; if(a=='m'&&(vd[i+1]|32)=='t'&&(vd[i+2]|32)=='p')mtpx=1; }
      if(o+4<=got){ u32 n=g32(mdata+o); o+=4; for(u32 i=0;i<n&&o+2<=got;i++,o+=2) if(g_mtp.nops<MTP_MAXOPS)g_mtp.ops[g_mtp.nops++]=g16(mdata+o); }
      for(int a=0;a<4&&o+4<=got;a++){ u32 n=g32(mdata+o); o+=4+2*n; }
      if(o<got)o+=(u32)mstr(mdata+o,(int)(got-o),g_mtp.maker,32); if(o<got)mstr(mdata+o,(int)(got-o),g_mtp.model,40);
      g_mtp.kind=(mtpx||mtp_android_vid(vid))?MTP_K_PHONE:MTP_K_CAMERA; }
    u32 sid=1; g_mtp.tid=0; r=mtx(0x1002,&sid,1,0,0,0,0,0,0,5000);
    if(r==0x201E){ g_mtp.tid=1; mtx(0x1003,0,0,0,0,0,0,0,0,3000); g_mtp.tid=0; r=mtx(0x1002,&sid,1,0,0,0,0,0,0,5000); }
    if(r!=MTP_OK){ klog("mtp: OpenSession refused"); mt=0; return r<0?r:MTP_E_PROTO; }
    g_mtp.tid=1; r=mstores(); if(r<0){ mt=0; return r; }
    g_mtp.up=1; klog(g_mtp.kind==MTP_K_PHONE?"mtp: phone connected (MTP)":"mtp: camera connected (PTP)"); return 0; }
void mtp_close(void){ if(mt&&g_mtp.up&&!g_mtp.dead)mtx(0x1003,0,0,0,0,0,0,0,0,1000); g_mtp.up=0; g_mtp.nst=0; g_mtp.gen++; mt=0; mc_clear(); }
int mtp_refresh(void){ if(!g_mtp.up)return MTP_E_NODEV; mc_clear(); return mstores(); }
static int mobj(u32 h,mtp_obj_t* e){
    u32 got=0; int r=mtx(0x1008,&h,1,0,0,mdata,sizeof mdata,&got,0,5000); if(r<0)return r; if(r!=MTP_OK||got<53)return MTP_E_PROTO;
    e->h=h; e->sid=g32(mdata); e->dir=g16(mdata+4)==0x3001; e->size=g32(mdata+8); e->parent=g32(mdata+38); mstr(mdata+52,(int)got-52,e->name,64); e->used=1; return 0; }
static int mlist(int s,u32 dir){
    u32 sid=g_mtp.st[s].id; for(int i=0;i<ml_n;i++) if(ml[i].sid==sid&&ml[i].parent==dir)return 0;
    static u8 hb[16384]; u32 got=0, p[3]={sid,0,dir}; int r=mtx(0x1007,p,3,0,0,hb,sizeof hb,&got,0,10000); if(r<0)return r; if(r!=MTP_OK||got<4)return MTP_E_IO;
    u32 n=g32(hb); if(n>(got-4)/4)n=(got-4)/4;
    for(u32 i=0;i<n;i++){ mtp_obj_t e; if(mobj(g32(hb+4+4*i),&e)<0)continue; if(dir==MTP_ROOT){ if(e.parent!=0&&e.parent!=MTP_ROOT)continue; e.parent=MTP_ROOT; } else if(e.parent!=dir)continue;
        if(!e.name[0])continue; e.sid=sid; int k=mc_n<MTP_NC?mc_n++:-1; if(k<0)break; mc[k]=e; }
    if(ml_n>=MTP_NL){ mc_clear(); return mlist(s,dir); } ml[ml_n].sid=sid; ml[ml_n].parent=dir; ml_n++; return 0; }
static int mchild(int s,u32 dir,const char* nm,int nl,mtp_obj_t** out){
    int r=mlist(s,dir); if(r<0)return r; u32 sid=g_mtp.st[s].id;
    for(int i=0;i<mc_n;i++){ mtp_obj_t* e=&mc[i]; if(!e->used||e->sid!=sid||e->parent!=dir)continue; int k=0; while(k<nl&&e->name[k]==nm[k])k++; if(k==nl&&!e->name[k]){ *out=e; return 0; } }
    return MTP_E_NOENT; }
static int mwalk(int s,const char* path,u32* h,mtp_obj_t** obj){
    if(!g_mtp.up||g_mtp.dead)return MTP_E_NODEV; if(s<0||s>=g_mtp.nst)return g_mtp.locked?MTP_E_LOCKED:MTP_E_ARG; if(!path||path[0]!='/')return MTP_E_ARG;
    u32 cur=MTP_ROOT; mtp_obj_t* e=0; int i=0;
    for(;;){ while(path[i]=='/')i++; if(!path[i])break; int k=i; while(path[i]&&path[i]!='/')i++;
        if(e&&!e->dir)return MTP_E_NOENT; int r=mchild(s,cur,path+k,i-k,&e); if(r<0)return r; cur=e->h; }
    *h=cur; if(obj)*obj=e; return 0; }
static int msplit(const char* path,char* dir,char* nm){ int n=mlen(path); if(n<2||n>=512||path[n-1]=='/')return MTP_E_ARG; int sl=n; while(sl>0&&path[sl-1]!='/')sl--; if(!sl||n-sl>=64)return MTP_E_ARG;
    for(int i=0;i<sl;i++)dir[i]=path[i]; dir[sl]=0; mcpy(nm,path+sl,64); return 0; }
int mtp_ls(int s,const char* path,mtp_lscb cb,void* ctx){
    u32 h; mtp_obj_t* e=0; int r=mwalk(s,path,&h,&e); if(r<0)return r; if(e&&!e->dir)return MTP_E_NOENT; r=mlist(s,h); if(r<0)return r;
    u32 sid=g_mtp.st[s].id; for(int i=0;i<mc_n;i++) if(mc[i].used&&mc[i].sid==sid&&mc[i].parent==h)cb(mc[i].name,mc[i].dir,mc[i].size,ctx); return 0; }
long mtp_size(int s,const char* path){ u32 h; mtp_obj_t* e=0; int r=mwalk(s,path,&h,&e); if(r<0)return r; if(!e||e->dir)return MTP_E_NOENT; if(e->size==0xFFFFFFFFu)return MTP_E_BIG; return (long)e->size; }
long mtp_read(int s,const char* path,char* buf,long cap){
    u32 h; mtp_obj_t* e=0; int r=mwalk(s,path,&h,&e); if(r<0)return r; if(!e||e->dir||cap<0)return MTP_E_ARG; if(e->size==0xFFFFFFFFu)return MTP_E_BIG;
    long want=(long)e->size<cap?(long)e->size:cap; u32 got=0;
    if(want<(long)e->size&&mtp_has_op(0x101B)){ u32 p[3]={h,0,(u32)want}; r=mtx(0x101B,p,3,0,0,(u8*)buf,(u32)want,&got,0,20000); }
    else r=mtx(0x1009,&h,1,0,0,(u8*)buf,(u32)want,&got,0,20000);
    if(r<0)return r; if(r!=MTP_OK)return MTP_E_IO; return (long)got; }
static int mdel(u32 h){ u32 p[2]={h,0}; int r=mtx(0x100B,p,2,0,0,0,0,0,0,10000); if(r<0)return r; return r==MTP_OK?0:r==0x200F||r==0x200D?MTP_E_RO:MTP_E_IO; }
static long mcreate(int s,const char* path,const char* buf,long len,int folder){
    if(g_mtp.st[s<0||s>=g_mtp.nst?0:s].ro&&s>=0&&s<g_mtp.nst)return MTP_E_RO;
    char dir[512], nm[64]; int r=msplit(path,dir,nm); if(r<0)return r; u32 ph; mtp_obj_t* pe=0; r=mwalk(s,dir,&ph,&pe); if(r<0)return r; if(pe&&!pe->dir)return MTP_E_NOENT;
    mtp_obj_t* ex=0; r=mchild(s,ph,nm,mlen(nm),&ex); if(r==0)return MTP_E_EXIST; if(r!=MTP_E_NOENT)return r;
    if(len<0||len>0x7FFFFFFFL)return MTP_E_BIG;
    u8 oi[256]; for(int i=0;i<52;i++)oi[i]=0; u32 sid=g_mtp.st[s].id; p32(oi,sid); p16(oi+4,folder?0x3001:0x3000); p32(oi+8,(u32)len); p32(oi+38,ph); p16(oi+42,folder?1:0);
    int n=52+mput_str(oi+52,nm,256-52-3); oi[n++]=0; oi[n++]=0; oi[n++]=0;
    u32 p[2]={sid,ph}, rp[5]; r=mtx(0x100C,p,2,oi,(u32)n,0,0,0,rp,10000); if(r<0)return r;
    if(r!=MTP_OK)return r==0x200C||r==0x200D||r==0x200F?MTP_E_RO:MTP_E_IO;
    u32 nh=rp[2];
    if(!folder){ r=mtx(0x100D,0,0,len?(const u8*)buf:(const u8*)"",(u32)len,0,0,0,0,30000); if(r<0)return r; if(r!=MTP_OK){ if(nh)mdel(nh); mc_clear(); return MTP_E_IO; } }
    mc_clear(); return folder?0:len; }
long mtp_write(int s,const char* path,const char* buf,long len){ if(!buf&&len)return MTP_E_ARG; return mcreate(s,path,buf,len,0); }
int mtp_mkdir(int s,const char* path){ return (int)mcreate(s,path,0,0,1); }
static int mempty_cb_n; static void mempty_cb(const char* a,int b,unsigned long long c,void* d){ (void)a; (void)b; (void)c; (void)d; mempty_cb_n++; }
int mtp_delete(int s,const char* path){
    u32 h; mtp_obj_t* e=0; int r=mwalk(s,path,&h,&e); if(r<0)return r; if(!e)return MTP_E_ARG; if(g_mtp.st[s].ro)return MTP_E_RO;
    if(e->dir){ mempty_cb_n=0; r=mtp_ls(s,path,mempty_cb,0); if(r<0)return r; if(mempty_cb_n)return MTP_E_NOTEMPTY; }
    r=mdel(h); mc_clear(); return r; }
int mtp_rename(int s,const char* from,const char* to){
    char d1[512], n1[64], d2[512], n2[64]; if(msplit(from,d1,n1)<0||msplit(to,d2,n2)<0||!meq(d1,d2))return MTP_E_ARG; if(!mtp_has_op(0x9804))return MTP_E_IO;
    u32 h; mtp_obj_t* e=0; int r=mwalk(s,from,&h,&e); if(r<0)return r; if(g_mtp.st[s].ro)return MTP_E_RO;
    u8 sb[160]; int n=mput_str(sb,n2,160); u32 p[2]={h,0xDC07}; r=mtx(0x9804,p,2,sb,(u32)n,0,0,0,0,10000); mc_clear(); if(r<0)return r; return r==MTP_OK?0:MTP_E_IO; }
const char* mtp_kind_name(void){ return g_mtp.kind==MTP_K_CAMERA?"Camera":"Phone"; }
const char* mtp_why(int e){
    switch(e){ case MTP_E_LOCKED: return "Unlock the phone and choose File transfer in its USB notification";
    case MTP_E_TIMEOUT: return "The phone or camera did not answer in time"; case MTP_E_NODEV: return "The phone or camera is not connected - unplug and reconnect it";
    case MTP_E_RO: return "The device does not allow changes here"; case MTP_E_NOTEMPTY: return "Delete the files in the folder first";
    case MTP_E_EXIST: return "A file with that name already exists"; case MTP_E_BIG: return "Files over 2 GB cannot be copied this way yet"; case MTP_E_NOENT: return "Not found on the device"; }
    return "The device reported an error"; }
int mtp_info(char* out,int cap){
    char b[640]; int o=0; const char* l[]={g_mtp.up?(g_mtp.kind==MTP_K_PHONE?"Phone (MTP): ":"Camera (PTP): "):"No phone or camera connected", g_mtp.maker, " ", g_mtp.model, g_mtp.up&&g_mtp.locked?"\nLocked or charging only - unlock and choose File transfer":"", g_mtp.dead?"\nNot answering - reconnect":""};
    for(unsigned i=0;i<sizeof l/sizeof l[0];i++){ if(!g_mtp.up&&i)break; for(int k=0;l[i][k]&&o<(int)sizeof b-1;k++)b[o++]=l[i][k]; }
    for(int s=0;g_mtp.up&&s<g_mtp.nst;s++){ const char* d=g_mtp.st[s].desc; if(o<(int)sizeof b-2){ b[o++]='\n'; b[o++]='-'; b[o++]=' '; } for(int k=0;d[k]&&o<(int)sizeof b-1;k++)b[o++]=d[k]; }
    b[o]=0; mcpy(out,b,cap); return o; }
