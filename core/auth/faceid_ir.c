#include "include/nvx/base.h"
#include "include/nvx/lib/base/fmt.h"
#include "include/nvx/core/klog.h"
#include "include/nvx/core/sysfeat.h"
#include "include/nvx/drivers/usb/uvc.h"
#include "include/nvx/drivers/usb/uvc_ir.h"
#include "include/nvx/core/auth/faceid_ir.h"

int fs_find_name(const char*); char* fsd(int); int fs_save_bin(const char*,const u8*,int,int); void nvx3_sync_all(void);
static struct featguard fir_fg={"Face ID infrared camera","feat-faceir.cfg",1,0,3,0,0};
static int fir_on, fir_prev=-1, fir_saved, fir_okd; static u32 fir_f0;
static const char* FIR_CFG="faceid-ir.cfg";

static char* fir_hex(char* p,u32 v,int nd){ for(int i=nd-1;i>=0;i--){ u32 d=(v>>(i*4))&15u; *p++=(char)(d<10?'0'+d:'a'+d-10); } return p; }
static int fir_hv(char c){ return c>='0'&&c<='9'?c-'0':c>='a'&&c<='f'?c-'a'+10:c>='A'&&c<='F'?c-'A'+10:-1; }
static int fir_rd(const char** s,u32* v){ while(**s==' ')(*s)++; int n=0; u32 r=0; for(;n<8;n++){ int h=fir_hv(**s); if(h<0)break; r=(r<<4)|(u32)h; (*s)++; } *v=r; return n>0; }
int fir_cfg_parse(const char* s,u16 vid,u16 pid,int* unit,int* sel,int* len,u8* val){
    for(int line=0;line<16&&s&&*s;line++){ const char* q=s; u32 v,p,u,l,se; int ok=fir_rd(&q,&v)&&fir_rd(&q,&p)&&fir_rd(&q,&u)&&fir_rd(&q,&se)&&fir_rd(&q,&l)&&l>=1&&l<=16;
        if(ok&&v==vid&&p==pid){ int k=0; for(;k<(int)l;k++){ u32 b; if(!fir_rd(&q,&b)||b>255)break; val[k]=(u8)b; } if(k==(int)l&&u<256&&se<256){ *unit=(int)u; *sel=(int)se; *len=(int)l; return 1; } }
        while(*s&&*s!='\n')s++; if(*s=='\n')s++; }
    return 0; }
int fir_cfg_line(char* out,u16 vid,u16 pid,int unit,int sel,int len,const u8* val){
    char* p=out; p=fir_hex(p,vid,4); *p++=' '; p=fir_hex(p,pid,4); *p++=' '; p=fir_hex(p,(u32)unit,2); *p++=' '; p=fir_hex(p,(u32)sel,2); *p++=' '; p=fir_hex(p,(u32)len,2);
    for(int k=0;k<len;k++){ *p++=' '; p=fir_hex(p,val[k],2); } *p++='\n'; *p=0; return (int)(p-out); }
static int fir_load(int* unit,int* sel,int* len,u8* val){ int k=fs_find_name(FIR_CFG); if(k<0)return 0; char* d=fsd(k); if(!d)return 0; return fir_cfg_parse(d,uvc_vid,uvc_pid,unit,sel,len,val); }
static void fir_save(void){
    int unit,sel,len; u8 v[16]; if(!uvc_ir_emitter_ctl(&unit,&sel,&len,v))return;
    char b[80]; int n=fir_cfg_line(b,uvc_vid,uvc_pid,unit,sel,len,v);
    if(fs_save_bin(FIR_CFG,(const u8*)b,n,0)>=0){ nvx3_sync_all(); klog("faceid: infrared light setting saved"); } }

int fir_available(void){ fg_boot(&fir_fg); return fir_fg.on&&!fir_fg.dead&&uvc_found()&&uvc_ir_stream()>=0; }
int fir_active(void){ return fir_on; }
int fir_emitter_on(void){ int s=uvc_ir_emitter(); return fir_on&&(s==UIR_ON||s==UIR_SAVED); }
int fir_start(void){
    if(fir_on||uvc_state==2||!fir_available())return 0;
    int ir=uvc_ir_stream(); if(ir<0||!fg_begin(&fir_fg))return 0;
    fir_prev=uvc_stream_cur(); if(ir!=fir_prev)uvc_select_stream(ir);
    if(uvc_start(0)<0){ fg_fail(&fir_fg,uvc_msg[0]?uvc_msg:"the infrared camera did not start"); if(fir_prev!=ir)uvc_select_stream(fir_prev); fir_prev=-1; return 0; }
    fir_on=1; fir_saved=0; fir_okd=0; fir_f0=uvc_frames;
    int unit,sel,len; u8 v[16];
    if(fir_load(&unit,&sel,&len,v)&&uvc_ir_emitter_apply(unit,sel,len,v)){ fir_saved=1; klog("faceid: infrared camera, saved light setting applied"); }
    else { uvc_ir_reset(); uvc_ir_search_begin(); }
    klog("faceid: using the infrared camera"); return 1; }
void fir_tick(void){
    if(!fir_on)return; uvc_ir_poll();
    if(!fir_okd&&uvc_frames-fir_f0>=10){ fir_okd=1; fg_ok(&fir_fg); }
    if(!fir_saved&&uvc_ir_emitter()==UIR_ON){ fir_saved=1; fir_save(); } }
void fir_stop(void){
    if(!fir_on)return; fir_on=0; uvc_ir_reset();
    if(fir_prev>=0&&fir_prev!=uvc_stream_cur())uvc_select_stream(fir_prev); else if(uvc_state==2)uvc_stop();
    fir_prev=-1; }
const char* fir_camera_text(void){
    if(!uvc_found())return "No camera found";
    fg_boot(&fir_fg); int ir=uvc_state==2&&!fir_on?-1:uvc_ir_stream();
    if(ir<0)return "Uses the colour camera";
    if(fir_fg.dead||!fir_fg.on)return "Infrared camera found but turned off after errors - uses the colour camera";
    if(fir_on){ int s=uvc_ir_emitter(); return s==UIR_ON||s==UIR_SAVED?"Uses the infrared camera, light on":s==UIR_BASE||s==UIR_TRY?"Uses the infrared camera, finding its light":"Uses the infrared camera, its light was not found"; }
    return "Uses the infrared camera (Windows Hello type)"; }
int fir_live(const u32* rgb,int w,int h,int x,int y,int fw,int fh){
    if(!rgb||w<8||h<8||fw<4||fh<4)return 0; if(x<0)x=0; if(y<0)y=0; if(x+fw>w)fw=w-x; if(y+fh>h)fh=h-y; if(fw<4||fh<4)return 0;
    u64 fs=0, fn=0, as=0, an=0, gs=0; u32 lo=255, hi=0;
    for(int yy=0;yy<h;yy+=2) for(int xx=0;xx<w;xx+=2){ u32 v=rgb[(long)yy*w+xx]&0xFF; int in=xx>=x&&xx<x+fw&&yy>=y&&yy<y+fh;
        if(in){ fs+=v; fn++; if(v<lo)lo=v; if(v>hi)hi=v; if(xx+2<x+fw){ int d=(int)v-(int)(rgb[(long)yy*w+xx+2]&0xFF); gs+=(u64)(d<0?-d:d); } } else { as+=v; an++; } }
    if(!fn)return 0; u32 fm=(u32)(fs/fn), am=an?(u32)(as/an):0, tex=(u32)(gs/fn);
    if(fm<24||hi-lo<24||tex<2)return 0;
    int sc=40+(int)(fm>am?(fm-am):0)+(int)(tex*4); if(fm+8<am)sc-=30; if(sc<0)sc=0; if(sc>100)sc=100; return sc; }
