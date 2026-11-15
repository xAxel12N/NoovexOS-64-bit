/*-
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Copyright (c) 2013 Adrian Chadd <adrian@freebsd.org>
 * Copyright (c) 2019 Vladimir Kondratyev <wulf@FreeBSD.org>
 * Copyright (c) 2023 Future Crew LLC.
 * Copyright (c) 2026 NoovexOS project (port to NoovexOS 64)
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */
#ifndef INTELBT_HOST
#include "include/nvx/base.h"
#include "include/nvx/core/klog.h"
#include "include/nvx/mem/heap.h"
#include "include/nvx/arch/x86_64/time/hrtimer.h"
#include "include/nvx/fs/vfs/vfs.h"
#include "include/nvx/drivers/bluetooth/bt.h"
#endif
#include "drivers/bluetooth/intelbt_fw.h"
static const struct { u16 pid; u8 ty; } intelbt_ids[]={
    { 0x07dc, INTELBT_7260 }, { 0x0a2a, INTELBT_7260 }, { 0x0aa7, INTELBT_7260 },
    { 0x0a2b, INTELBT_8260 }, { 0x0aaa, INTELBT_8260 }, { 0x0025, INTELBT_8260 }, { 0x0026, INTELBT_8260 }, { 0x0029, INTELBT_8260 },
    { 0x0032, INTELBT_9260 }, { 0x0033, INTELBT_9260 }, { 0x0035, INTELBT_9260 }, { 0x0036, INTELBT_9260 },
};
static u16 ib_g16(const u8* p){ return (u16)(p[0]|(p[1]<<8)); }
static u32 ib_g32(const u8* p){ return (u32)p[0]|((u32)p[1]<<8)|((u32)p[2]<<16)|((u32)p[3]<<24); }
static char* ib_s(char* p,char* e,const char* s){ while(*s&&p<e)*p++=*s++; return p; }
static char* ib_x(char* p,char* e,u32 v,int w){ static const char h[]="0123456789abcdef"; int d=1; while(d<8&&(v>>(4*d)))d++; if(w>d)d=w; for(int k=d-1;k>=0&&p<e;k--)*p++=h[(v>>(4*k))&15]; return p; }
static char* ib_u(char* p,char* e,u32 v){ char t[12]; int n=0; do{ t[n++]=(char)('0'+v%10); v/=10; }while(v); while(n&&p<e)*p++=t[--n]; return p; }
int intelbt_match(u16 vid,u16 pid){
    if(vid!=0x8087)return 0;
    for(unsigned i=0;i<sizeof intelbt_ids/sizeof intelbt_ids[0];i++) if(intelbt_ids[i].pid==pid)return intelbt_ids[i].ty;
    return 0;
}
int intelbt_parse_tlv(const u8* d,int n,intelbt_tlv_t* v){
    u8* z=(u8*)v; for(int i=0;i<(int)sizeof *v;i++)z[i]=0;
    if(n<1||d[0])return -1;
    d++; n--;
    while(n>=2){
        int t=d[0], l=d[1]; d+=2; n-=2;
        if(n<l)return -1;
        int ok=1;
        switch(t){
        case 0x10: if((ok=l==4))v->cnvi_top=ib_g32(d); break;
        case 0x11: if((ok=l==4))v->cnvr_top=ib_g32(d); break;
        case 0x12: if((ok=l==4))v->cnvi_bt=ib_g32(d); break;
        case 0x13: if((ok=l==4))v->cnvr_bt=ib_g32(d); break;
        case 0x16: if((ok=l==2))v->dev_rev_id=ib_g16(d); break;
        case 0x1C: if((ok=l==1))v->img_type=d[0]; break;
        case 0x1D: if((ok=l==2)){ v->min_cw=d[0]; v->min_yy=d[1]; v->timestamp=ib_g16(d); } break;
        case 0x1E: if((ok=l==1))v->build_type=d[0]; break;
        case 0x1F: if((ok=l==4)){ v->min_nn=d[0]; v->build_num=ib_g32(d); } break;
        case 0x28: if((ok=l==1))v->secure_boot=d[0]; break;
        case 0x2A: if((ok=l==1))v->otp_lock=d[0]; break;
        case 0x2B: if((ok=l==1))v->api_lock=d[0]; break;
        case 0x2C: if((ok=l==1))v->debug_lock=d[0]; break;
        case 0x2D: if((ok=l==3)){ v->min_nn=d[0]; v->min_cw=d[1]; v->min_yy=d[2]; } break;
        case 0x2E: if((ok=l==1))v->limited_cce=d[0]; break;
        case 0x2F: if((ok=l==1))v->sbe_type=d[0]; break;
        case 0x30: if((ok=l>=6)) for(int i=0;i<6;i++)v->bdaddr[i]=d[i]; break;
        }
        if(!ok)return -1;
        d+=l; n-=l;
    }
    return 0;
}
int intelbt_identify(const u8* d,int n,intelbt_tlv_t* v){
    if(n==10&&d[1]==0x37){
        switch(d[2]){ case 0x07: case 0x08: return INTELBT_7260; case 0x0b: case 0x0c: case 0x11: case 0x12: case 0x13: case 0x14: return INTELBT_8260; }
        return 0;
    }
    if(intelbt_parse_tlv(d,n,v)<0)return 0;
    if(((v->cnvi_bt>>8)&0xff)!=0x37)return 0;
    return ((v->cnvi_bt>>16)&0x3f)<0x17?INTELBT_8260:INTELBT_9260;
}
static u16 ib_pack(u32 t){ return (u16)(((t&0x0f000000)>>16)|((t&0x0000000f)<<12)|((t&0x00000ff0)>>4)); }
int intelbt_name_tlv(const intelbt_tlv_t* v,const char* sfx,char* out,int cap){
    char* p=out; char* e=out+cap-1;
    p=ib_s(p,e,"ibt-"); p=ib_x(p,e,ib_pack(v->cnvi_top),4); p=ib_s(p,e,"-"); p=ib_x(p,e,ib_pack(v->cnvr_top),4); p=ib_s(p,e,"."); p=ib_s(p,e,sfx);
    *p=0; return (int)(p-out);
}
int intelbt_name_legacy(const u8* ver,u16 devrev,const char* sfx,int fallback,char* out,int cap){
    char* p=out; char* e=out+cap-1;
    switch(ver[2]){
    case 0x07: case 0x08:
        p=ib_s(p,e,"ibt-hw-"); p=ib_x(p,e,ver[1],0); p=ib_s(p,e,"."); p=ib_x(p,e,ver[2],0);
        if(!fallback){ p=ib_s(p,e,"."); p=ib_x(p,e,ver[3],0); p=ib_s(p,e,"-fw-"); for(int i=4;i<9;i++){ p=ib_x(p,e,ver[i],0); if(i<8)p=ib_s(p,e,"."); } }
        break;
    case 0x0b: case 0x0c:
        p=ib_s(p,e,"ibt-"); p=ib_u(p,e,ver[2]); p=ib_s(p,e,"-"); p=ib_u(p,e,devrev); break;
    case 0x11: case 0x12: case 0x13: case 0x14:
        p=ib_s(p,e,"ibt-"); p=ib_u(p,e,ver[2]); p=ib_s(p,e,"-"); p=ib_u(p,e,ver[3]); p=ib_s(p,e,"-"); p=ib_u(p,e,ver[5]); break;
    default: *out=0; return 0;
    }
    p=ib_s(p,e,"."); p=ib_s(p,e,sfx); *p=0; return (int)(p-out);
}
int intelbt_css_check(const u8* fw,int len,u8 hwv,u8 sbe){
    if(sbe>1)return -1;
    if(hwv<=0x14){ if(len<644||ib_g32(fw+8)!=0x00010000u||sbe)return -1; return 644; }
    if(hwv>=0x17){ if(len<644+320||fw[644]!=0x06||ib_g32(fw+644+8)!=0x00020000u)return -1; return 644+320; }
    return -1;
}
int intelbt_send_fw(const u8* fw,int len,u8 hwv,u8 sbe,u32* boot,intelbt_frag_fn f,void* ctx){
    static const u8 rsa[5][2]={{0x00,0x80},{0x03,0x80},{0x03,0x80},{0x02,0x80},{0x02,0x80}}, ec[3][2]={{0x00,0x80},{0x03,0x60},{0x02,0x60}};
    int hl=intelbt_css_check(fw,len,hwv,sbe); if(hl<0)return -1;
    int sent=sbe?644:0, nf=0;
    for(int i=0;i<(sbe?3:5);i++){ const u8* r=sbe?ec[i]:rsa[i]; if(!sbe&&i==3)sent+=4; int n=r[1]; if(n>len-sent)n=len-sent; if(n<=0||f(r[0],fw+sent,n,ctx)<0)return -1; sent+=r[1]; nf++; }
    sent=hl; int ready=0;
    while(len-sent-ready>=3){
        const u8* c=fw+sent+ready;
        if(ib_g16(c)==0xfc0e&&sent+ready+7<=len)*boot=ib_g32(c+3);
        ready+=3+c[2];
        while(ready>=0xFC){ int n=len-sent<0xFC?len-sent:0xFC; if(n<=0||f(0x01,fw+sent,n,ctx)<0)return -1; sent+=0xFC; ready-=0xFC; nf++; }
        if(ready>0&&ready%4==0){ int n=len-sent<ready?len-sent:ready; if(n<=0||f(0x01,fw+sent,n,ctx)<0)return -1; sent+=ready; ready=0; nf++; }
    }
    return nf;
}
#ifndef INTELBT_HOST
extern volatile u32 g_ml_beat;
void ui_pump(void);
static u8 ib_ev[264];
static u64 ib_pump_t;
static int ib_done6;
static void ib_idle(void){ u64 t=hr_ns(); if(t-ib_pump_t>20000000ULL){ ib_pump_t=t; g_ml_beat++; ui_pump(); } }
static int ib_next(u8* e,int cap,u64 t0,int ms,int bulk){
    u64 lim=(u64)ms*1000000ULL;
    while(hr_ns()-t0<lim){
        int n=btusb_evt(e,cap); if(n>0)return n;
        if(bulk){ n=btusb_bulk_in(e,cap,0); if(n>0)return n; if(n<0)return -1; }
        if(!btusb_ok())return -1;
        ib_idle(); hr_delay_us(200);
    }
    return 0;
}
static int ib_send(u16 op,const u8* p,int n){
    u8 pk[260]; if(n<0||n>255)return -1;
    pk[0]=(u8)op; pk[1]=(u8)(op>>8); pk[2]=(u8)n; for(int i=0;i<n;i++)pk[3+i]=p[i];
    return btusb_cmd(pk,n+3);
}
static int ib_cmd(u16 op,const u8* p,int n,int ms){
    if(ib_send(op,p,n)<0)return -1;
    u64 t0=hr_ns();
    for(;;){ int m=ib_next(ib_ev,(int)sizeof ib_ev,t0,ms,0); if(m<=0)return -1;
        if(ib_ev[0]==0x0E&&m>=5&&ib_g16(ib_ev+3)==op)return m;
        if(ib_ev[0]==0xFF)return m; }
}
static int ib_rp(int m,const u8** rp){ *rp=ib_ev+5; int l=ib_ev[1]-3; if(l>m-5)l=m-5; return l<0?0:l; }
static int ib_vendor(int code,int ms,int bulk){
    if(code==0x06&&ib_done6)return 1;
    u64 t0=hr_ns();
    for(;;){ int m=ib_next(ib_ev,(int)sizeof ib_ev,t0,ms,bulk); if(m<=0)return 0; if(ib_ev[0]==0xFF&&m>=3&&ib_ev[2]==code)return 1; }
}
static int ib_frag(u8 type,const u8* d,int n,void* ctx){
    (void)ctx; u8 pk[260];
    pk[0]=0x09; pk[1]=0xFC; pk[2]=(u8)(n+1); pk[3]=type; for(int i=0;i<n;i++)pk[4+i]=d[i];
    if(btusb_bulk_out(pk,n+4,2000)<0)return -1;
    u64 t0=hr_ns();
    for(;;){ int m=ib_next(ib_ev,(int)sizeof ib_ev,t0,2000,1); if(m<=0)return -1;
        if(ib_ev[0]==0xFF&&m>=3&&ib_ev[2]==0x06){ ib_done6=1; return 0; }
        if(ib_ev[0]==0x0E&&m>=6&&ib_g16(ib_ev+3)==0xFC09)return ib_ev[5]?-1:0;
        if(ib_ev[0]==0x0F&&m>=6&&ib_g16(ib_ev+4)==0xFC09)return ib_ev[2]?-1:0; }
}
static void ib_msg(char* m,int cap,const char* a,const char* b,const char* c){
    char* p=m; char* e=m+cap-1; p=ib_s(p,e,"Intel: "); p=ib_s(p,e,a); if(b)p=ib_s(p,e,b); if(c)p=ib_s(p,e,c); *p=0;
    char k[160]; p=k; e=k+sizeof k-1; p=ib_s(p,e,"bluetooth: "); p=ib_s(p,e,m); *p=0; klog(k);
}
static u8* ib_load(const char* name,int* len,char* path,int pcap){
    char* p=path; char* e=path+pcap-1; p=ib_s(p,e,"/lib/firmware/intel/"); p=ib_s(p,e,name); *p=0;
    *len=0; int fd=vfs_open(path,O_RDONLY); if(fd<0)return 0;
    int cap=1<<16, n=0; u8* b=(u8*)kmalloc((unsigned long)cap);
    while(b){ if(n==cap){ if(cap>=(4<<20)){ kfree(b); b=0; break; } u8* nb=(u8*)krealloc(b,(unsigned long)cap*2); if(!nb){ kfree(b); b=0; break; } b=nb; cap*=2; }
        int r=vfs_read(fd,(char*)b+n,cap-n); if(r<=0)break; n+=r; }
    vfs_close(fd);
    if(b&&!n){ kfree(b); b=0; }
    *len=n; return b;
}
static int ib_ddc(const char* name){
    char path[96]; int len; u8* d=ib_load(name,&len,path,(int)sizeof path); if(!d)return 0;
    int o=0, ok=1;
    while(o<len){ int pl=d[o]+1; if(pl>len-o)pl=len-o; if(ib_cmd(0xFC8B,d+o,pl,2000)<0){ ok=0; break; } o+=d[o]+1; }
    kfree(d); return ok?1:-1;
}
static void ib_evmask(void){ static const u8 m[8]={0x87,0x0c,0,0,0,0,0,0}; ib_cmd(0xFC52,m,8,2000); }
static int ib_reset(u32 boot){
    u8 p[8]={0,0,0,1,0,0,0,0}; p[4]=(u8)boot; p[5]=(u8)(boot>>8); p[6]=(u8)(boot>>16); p[7]=(u8)(boot>>24);
    if(ib_send(0xFC01,p,8)<0)return 0;
    return ib_vendor(0x02,5000,0);
}
static int ib_boot(const char* name,u8 hwv,u8 sbe,char* msg,int cap){
    char path[96]; int len; u8* fw=ib_load(name,&len,path,(int)sizeof path);
    if(!fw){ ib_msg(msg,cap,"firmware file missing: ",path,0); return -1; }
    ib_msg(msg,cap,"loading ",path,0);
    u32 boot=0; ib_done6=0;
    int nf=intelbt_send_fw(fw,len,hwv,sbe,&boot,ib_frag,0);
    kfree(fw);
    if(nf<0){ ib_msg(msg,cap,"firmware download failed: ",name,0); return -1; }
    if(!ib_vendor(0x06,5000,1)){ ib_msg(msg,cap,"no download-complete event for ",name,0); return -1; }
    if(!ib_reset(boot)){ ib_msg(msg,cap,"controller did not boot the firmware ",name,0); return -1; }
    return 0;
}
static int ib_patch(const u8* f,int len){
    int act=0;
    while(len>0){
        if(len<4||f[0]!=0x01)return -1;
        f++; len--;
        u16 op=ib_g16(f); int cl=f[2]; if(op==0xfc8e)act=1;
        if(len-3<cl)return -1;
        if(btusb_cmd(f,3+cl)<0)return -1;
        f+=3+cl; len-=3+cl;
        while(len>0&&f[0]==0x02){
            if(len<3)return -1;
            int el=f[2]; f+=3; len-=3; if(len<el)return -1;
            int m=ib_next(ib_ev,(int)sizeof ib_ev,hr_ns(),2000,0); if(m<=0||m!=el+2)return -1;
            for(int i=0;i<el;i++) if(ib_ev[2+i]!=f[i])return -1;
            f+=el; len-=el;
        }
    }
    return act;
}
static int ib_mfg(int on,int mode){ u8 p[2]={(u8)on,(u8)mode}; return ib_cmd(0xFC11,p,2,2000)>0; }
static int ib_7260(char* msg,int cap){
    const u8* v; int m=ib_cmd(0xFC05,0,0,2000); if(m<=0||ib_ev[0]!=0x0E||ib_rp(m,&v)!=10){ ib_msg(msg,cap,"Read Version failed",0,0); return -1; }
    u8 ver[10]; for(int i=0;i<10;i++)ver[i]=v[i];
    if(ver[9]>0){ ib_msg(msg,cap,"firmware already running",0,0); return 2; }
    char name[64], path[96]; int len; intelbt_name_legacy(ver,0,"bseq",0,name,(int)sizeof name);
    u8* fw=ib_load(name,&len,path,(int)sizeof path);
    if(!fw){ char n2[64], p2[96]; intelbt_name_legacy(ver,0,"bseq",1,n2,(int)sizeof n2); fw=ib_load(n2,&len,p2,(int)sizeof p2); }
    if(!fw){ ib_msg(msg,cap,"firmware file missing: ",path,0); return -1; }
    if(!ib_mfg(1,0)){ kfree(fw); ib_msg(msg,cap,"cannot enter manufacturer mode",0,0); return -1; }
    int r=ib_patch(fw,len); kfree(fw);
    if(r<0){ ib_mfg(0,1); ib_msg(msg,cap,"patch download failed",0,0); return -1; }
    if(!ib_mfg(0,r?2:0)){ ib_msg(msg,cap,"cannot leave manufacturer mode",0,0); return -1; }
    if(ib_mfg(1,0)){ ib_evmask(); ib_mfg(0,0); }
    ib_msg(msg,cap,"patch loaded",0,0); return 1;
}
static int ib_8260(char* msg,int cap){
    const u8* v; int m=ib_cmd(0xFC05,0,0,2000); if(m<=0||ib_ev[0]!=0x0E||ib_rp(m,&v)!=10){ ib_msg(msg,cap,"Read Version failed",0,0); return -1; }
    u8 ver[10]; for(int i=0;i<10;i++)ver[i]=v[i];
    if(ver[4]==0x23){ ib_msg(msg,cap,"firmware already running",0,0); return 2; }
    if(ver[4]!=0x06){ ib_msg(msg,cap,"unknown firmware variant",0,0); return -1; }
    m=ib_cmd(0xFC0D,0,0,2000); if(m<=0||ib_ev[0]!=0x0E||ib_rp(m,&v)<23||v[0]){ ib_msg(msg,cap,"Read Boot Params failed",0,0); return -1; }
    u16 rev=ib_g16(v+4); if(v[21]){ ib_msg(msg,cap,"unsupported loading method (limited CCE)",0,0); return -1; }
    char name[64]; if(!intelbt_name_legacy(ver,rev,"sfi",0,name,(int)sizeof name)){ ib_msg(msg,cap,"unsupported hardware variant",0,0); return -1; }
    if(ib_boot(name,0,0,msg,cap)<0)return -1;
    intelbt_name_legacy(ver,rev,"ddc",0,name,(int)sizeof name); int d=ib_ddc(name);
    ib_evmask();
    intelbt_name_legacy(ver,rev,"sfi",0,name,(int)sizeof name);
    ib_msg(msg,cap,"firmware loaded: ",name,d>0?" (with DDC)":d<0?" (DDC failed)":""); return 1;
}
static int ib_9260(intelbt_tlv_t* t,char* msg,int cap){
    if(t->img_type==0x03){ ib_msg(msg,cap,"firmware already running",0,0); return 2; }
    if(t->img_type!=0x01){ ib_msg(msg,cap,"unknown image type",0,0); return -1; }
    if(t->limited_cce){ ib_msg(msg,cap,"unsupported loading method (limited CCE)",0,0); return -1; }
    if(t->sbe_type>1){ ib_msg(msg,cap,"unsupported secure boot engine",0,0); return -1; }
    char name[64]; intelbt_name_tlv(t,"sfi",name,(int)sizeof name);
    if(ib_boot(name,(u8)((t->cnvi_bt>>16)&0x3f),t->sbe_type,msg,cap)<0)return -1;
    static const u8 ff=0xFF; const u8* v; int m=ib_cmd(0xFC05,&ff,1,2000); intelbt_tlv_t t2;
    if(m>0&&ib_ev[0]==0x0E){ int l=ib_rp(m,&v); if(intelbt_parse_tlv(v,l,&t2)==0)t=&t2; }
    intelbt_name_tlv(t,"ddc",name,(int)sizeof name); int d=ib_ddc(name);
    ib_evmask();
    intelbt_name_tlv(t,"sfi",name,(int)sizeof name);
    ib_msg(msg,cap,"firmware loaded: ",name,d>0?" (with DDC)":d<0?" (DDC failed)":""); return 1;
}
int intelbt_setup(char* msg,int cap){
    u16 vid=0,pid=0; btusb_ids(&vid,&pid); int ty=intelbt_match(vid,pid); if(!ty)return 0;
    for(int i=0;i<64&&btusb_evt(ib_ev,(int)sizeof ib_ev)>0;i++){}
    if(ty==INTELBT_7260)ib_cmd(0x0C03,0,0,2000);
    static const u8 ff=0xFF; const u8* v; int m=ib_cmd(0xFC05,&ff,1,2000);
    if(m<=0||ib_ev[0]!=0x0E){ ib_msg(msg,cap,"controller does not answer Read Version",0,0); return -1; }
    int l=ib_rp(m,&v); u8 d[256]; for(int i=0;i<l;i++)d[i]=v[i];
    intelbt_tlv_t t; int k=intelbt_identify(d,l,&t);
    if(k==INTELBT_7260)return ib_7260(msg,cap);
    if(k==INTELBT_8260)return ib_8260(msg,cap);
    if(k==INTELBT_9260)return ib_9260(&t,msg,cap);
    ib_msg(msg,cap,"unsupported controller",0,0); return -1;
}
#endif
