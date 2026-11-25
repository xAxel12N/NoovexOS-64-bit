#include "sys/linux/evdev_abi.h"
#include "drivers/input/gamepad/gpad_proto.h"
#include "drivers/input/gamepad/gamepad.h"
int gpusb_scan(void); void gpusb_poll(void);
static struct { u8 used; u16 s, w, len; } aev_ff[GP_MAX][16];
static u8 aev_jsinit[VMAXOPEN];
static u8 aev_scanned;
static int aev_parse(const char* p,int* js){
    const char* a; if(p[0]!='/'||p[1]!='d'||p[2]!='e'||p[3]!='v'||p[4]!='/'||p[5]!='i'||p[6]!='n'||p[7]!='p'||p[8]!='u'||p[9]!='t'||p[10]!='/')return -1;
    a=p+11; if(a[0]=='e'&&a[1]=='v'&&a[2]=='e'&&a[3]=='n'&&a[4]=='t'){ a+=5; *js=0; } else if(a[0]=='j'&&a[1]=='s'){ a+=2; *js=1; } else return -1;
    if(a[0]<'0'||a[0]>'9'||a[1])return -1;
    if(!*js&&a[0]=='0')return -1;
    return a[0]-'0'-(*js?0:1); }
static void aev_scan(void){ if(!aev_scanned){ aev_scanned=1; gpusb_scan(); } }
static int aev_isdir(const char* p){ return lx_streq(p,"/dev/input"); }
static int aev_open(const char* p,int flags){
    if(aev_isdir(p)){ if((flags&3)!=0)return -LX_EISDIR; aev_scan(); int vi=jc_newvf(LX_SRDIR,0,flags); if(vi<0)return -LX_EMFILE; const char* z="/dev/input"; int q=0; for(;z[q];q++)g_vf[vi].vpath[q]=z[q]; g_vf[vi].vpath[q]=0; int fd=lx_fd_install(vi,(flags&0x80000)!=0,0); if(fd<0)vf_unref(vi); return fd; }
    int js=0, k=aev_parse(p,&js); if(k<0)return -9999;
    if(!gpad_valid(k))aev_scan();
    if(!gpad_valid(k))return -LX_ENOENT;
    int vi=jc_newvf(js?LX_JSDEV:LX_EVDEV,k,flags&~0x80000); if(vi<0)return -LX_EMFILE;
    int q=0; for(;p[q]&&q<127;q++)g_vf[vi].vpath[q]=p[q]; g_vf[vi].vpath[q]=0;
    g_vf[vi].off=(int)gpad_qpos(k); g_vf[vi].gen=gpad_at(k)->gen; aev_jsinit[vi]=(u8)js;
    int fd=lx_fd_install(vi,(flags&0x80000)!=0,0); if(fd<0)vf_unref(vi); return fd; }
static int aev_stat(const char* p,u32* o,u64* rd){
    if(aev_isdir(p)){ for(int i=0;i<10;i++)o[i]=0; o[0]=0040755u; o[3]=2; o[5]=0x37200000u; o[6]=o[7]=o[8]=1760000000u; *rd=0; return 1; }
    int js=0, k=aev_parse(p,&js); if(k<0||!gpad_valid(k))return 0;
    for(int i=0;i<10;i++)o[i]=0;
    o[3]=1; o[6]=o[7]=o[8]=1760000000u; o[0]=js?0020664u:0020660u; o[5]=0x37210000u+(u32)(js*16+k); *rd=(13u<<8)|(u32)(js?k:65+k);
    return 1; }
static int aev_list(int n,char* name,int* type){
    aev_scan(); int c=0;
    for(int js=0;js<2;js++) for(int k=0;k<GP_MAX;k++){ if(!gpad_valid(k))continue; if(c==n){ const char* s=js?"js":"event"; int q=0; for(;*s;s++)name[q++]=*s; name[q++]=(char)('0'+k+(js?0:1)); name[q]=0; *type=2; return 1; } c++; }
    return 0; }
static int aev_live(struct vfile* v){ struct gpad* p=gpad_at(v->fs_idx); return p&&p->gen==v->gen; }
static int aev_bits(u8* b,int cap,int ev){
    for(int i=0;i<cap;i++)b[i]=0;
    int top=0;
#define AEV_SET(x) do{ int _x=(x); if((_x>>3)<cap){ b[_x>>3]|=(u8)(1u<<(_x&7)); if((_x>>3)+1>top)top=(_x>>3)+1; } }while(0)
    if(ev==0){ AEV_SET(0); AEV_SET(1); AEV_SET(3); AEV_SET(AEV_EV_FF); }
    else if(ev==1){ for(int i=0;i<GP_NKEY;i++)AEV_SET(gp_keylist[i]); }
    else if(ev==3){ for(int i=0;i<GP_NABS;i++)AEV_SET(gp_abslist[i]); }
    else if(ev==AEV_EV_FF){ AEV_SET(AEV_FF_RUMBLE); AEV_SET(AEV_FF_GAIN); }
#undef AEV_SET
    return cap; }
static long aev_str(u64 arg,u32 len,const char* s){ u8 t[256]; u32 n=0; while(s[n]&&n<len&&n<255){ t[n]=(u8)s[n]; n++; } if(n<len&&n<256)t[n++]=0; if(n&&lx_put(arg,t,n))return -LX_EFAULT; return (long)n; }
static long aev_ioctl(struct vfile* v,u32 req,u64 arg){
    int k=v->fs_idx; if(!aev_live(v))return -LX_ENODEV; struct gpad* p=gpad_at(k);
    u32 dir=req>>30, sz=(req>>16)&0x3FFF, ty=(req>>8)&255, nr=req&255;
    if(v->dev==LX_JSDEV){
        if(req==AEV_JSGVERSION){ u32 x=AEV_JSVERSION; return lx_put(arg,&x,4); }
        if(req==AEV_JSGAXES){ u8 x=GP_NABS; return lx_put(arg,&x,1); }
        if(req==AEV_JSGBUTTONS){ u8 x=GP_NKEY; return lx_put(arg,&x,1); }
        if(req==AEV_JSGAXMAP){ u8 m[64]; for(int i=0;i<64;i++)m[i]=0; for(int i=0;i<GP_NABS;i++)m[i]=gp_abslist[i]; return lx_put(arg,m,64); }
        if(req==AEV_JSGBTNMAP){ u8 m[1024]; for(int i=0;i<1024;i++)m[i]=0; for(int i=0;i<GP_NKEY;i++){ m[2*i]=(u8)gp_keylist[i]; m[2*i+1]=(u8)(gp_keylist[i]>>8); } return lx_put(arg,m,1024); }
        if(dir==2&&ty=='j'&&nr==0x13)return aev_str(arg,sz,p->name);
        return -LX_ENOTTY; }
    if(req==AEV_GVERSION){ int x=AEV_VERSION; return lx_put(arg,&x,4); }
    if(req==AEV_GID){ u16 id[4]={(u16)(p->bus==5?5:3),p->vid,p->pid,0x0111}; return lx_put(arg,id,8); }
    if(req==AEV_GEFFECTS){ int x=(p->caps&GPC_RUMBLE)?16:0; return lx_put(arg,&x,4); }
    if(req==AEV_GRAB||req==AEV_REVOKE||req==AEV_SCLOCKID)return 0;
    if(req==AEV_SFF){ u8 e[AEV_FFSZ]; if(!(p->caps&GPC_RUMBLE))return -LX_EINVAL; if(lx_get(e,arg,AEV_FFSZ))return -LX_EFAULT;
        u16 t=(u16)(e[0]|(e[1]<<8)); short id=(short)(e[2]|(e[3]<<8)); if(t!=AEV_FF_RUMBLE)return -LX_EINVAL;
        if(id<0){ id=-1; for(int i=0;i<16;i++) if(!aev_ff[k][i].used){ id=(short)i; break; } if(id<0)return -LX_ENOSPC; } else if(id>=16||!aev_ff[k][id].used)return -LX_EINVAL;
        aev_ff[k][id].used=1; aev_ff[k][id].len=(u16)(e[10]|(e[11]<<8)); aev_ff[k][id].s=(u16)(e[16]|(e[17]<<8)); aev_ff[k][id].w=(u16)(e[18]|(e[19]<<8));
        e[2]=(u8)id; e[3]=(u8)((u16)id>>8); return lx_put(arg+2,e+2,2); }
    if(req==AEV_RMFF){ long id=(long)arg; if(id<0||id>=16||!aev_ff[k][id].used)return -LX_EINVAL; aev_ff[k][id].used=0; gpad_rumble(k,0,0,0); return 0; }
    if(dir!=2||ty!='E')return -LX_ENOTTY;
    if(nr==0x06)return aev_str(arg,sz,p->name);
    if(nr==0x07)return aev_str(arg,sz,p->bus==5?"bluetooth":"usb-gamepad");
    if(nr==0x08)return aev_str(arg,sz,"");
    if(nr==0x09||nr==0x19||nr==0x1A||nr==0x1B){ u8 z[64]; u32 n=sz>64?64:sz; for(u32 i=0;i<n;i++)z[i]=0; if(n&&lx_put(arg,z,n))return -LX_EFAULT; return (long)n; }
    if(nr==0x18){ u8 b[96]; u32 n=sz>96?96:sz; for(u32 i=0;i<n;i++)b[i]=0; for(int i=0;i<GP_NKEY;i++){ int c=gp_keylist[i]; if((u32)(c>>3)<n&&gpad_keyval(k,c))b[c>>3]|=(u8)(1u<<(c&7)); } if(n&&lx_put(arg,b,n))return -LX_EFAULT; return (long)n; }
    if(nr>=0x20&&nr<0x40){ u8 b[96]; u32 n=sz>96?96:sz; aev_bits(b,(int)n,(int)(nr-0x20)); if(n&&lx_put(arg,b,n))return -LX_EFAULT; return (long)n; }
    if(nr>=0x40&&nr<0x80&&sz>=AEV_ABSSZ){ int lo,hi,fl; if(!gpad_absinfo((int)(nr-0x40),&lo,&hi,&fl))return -LX_EINVAL; int a[6]={gpad_absval(k,(int)(nr-0x40)),lo,hi,0,fl,0}; return lx_put(arg,a,AEV_ABSSZ); }
    return -LX_ENOTTY; }
static int aev_js_val(int code,int v){ if(code==2||code==5)return v*257-32767; if(code==0x10||code==0x11)return v*32767; return v<-32767?-32767:v; }
static int aev_read(struct vfile* v,u64 buf,u64 len){
    int k=v->fs_idx; if(!aev_live(v))return -LX_ENODEV;
    gpusb_poll();
    int js=v->dev==LX_JSDEV, esz=js?AEV_JSSZ:AEV_EVSZ; if(len<(u64)esz)return -LX_EINVAL;
    u8 t[24*32]; int max=(int)(len/(u64)esz); if(max>32)max=32; int n=0;
    if(js&&aev_jsinit[v-g_vf]){ aev_jsinit[v-g_vf]=0; u32 ms=(u32)(hr_ns()/1000000ULL);
        for(int i=0;i<GP_NKEY&&n<max;i++){ u8* e=t+n*8; *(u32*)e=ms; *(short*)(e+4)=(short)gpad_keyval(k,gp_keylist[i]); e[6]=0x81; e[7]=(u8)i; n++; }
        for(int i=0;i<GP_NABS&&n<max;i++){ u8* e=t+n*8; *(u32*)e=ms; *(short*)(e+4)=(short)aev_js_val(gp_abslist[i],gpad_absval(k,gp_abslist[i])); e[6]=0x82; e[7]=(u8)i; n++; } }
    u32 pos=(u32)v->off; struct gpev ev[32]; int drop=0;
    while(n<max){ int got=gpad_read_ev(k,&pos,ev,1,&drop); if(got<=0)break;
        if(drop&&!js){ u8* e=t+n*24; lx_zero(e,24); *(u16*)(e+16)=0; *(u16*)(e+18)=3; n++; drop=0; if(n>=max){ pos--; break; } }
        if(js){ int ty=0, num=-1; if(ev[0].type==1){ ty=1; for(int i=0;i<GP_NKEY;i++) if(gp_keylist[i]==ev[0].code)num=i; } else if(ev[0].type==3){ ty=2; for(int i=0;i<GP_NABS;i++) if(gp_abslist[i]==ev[0].code)num=i; }
            if(num<0)continue; u8* e=t+n*8; *(u32*)e=(u32)(ev[0].ns/1000000ULL); *(short*)(e+4)=(short)(ty==2?aev_js_val(ev[0].code,ev[0].val):ev[0].val); e[6]=(u8)ty; e[7]=(u8)num; n++; }
        else { u8* e=t+n*24; *(u64*)e=ev[0].ns/1000000000ULL; *(u64*)(e+8)=(ev[0].ns/1000ULL)%1000000ULL; *(u16*)(e+16)=ev[0].type; *(u16*)(e+18)=ev[0].code; *(int*)(e+20)=ev[0].val; n++; } }
    v->off=(int)pos;
    if(!n)return JS_BLOCK;
    if(lx_put(buf,t,(u64)n*(u64)esz))return -LX_EFAULT;
    return n*esz; }
static long aev_write(struct vfile* v,u64 buf,u64 len){
    int k=v->fs_idx; if(!aev_live(v))return -LX_ENODEV; if(v->dev==LX_JSDEV)return -LX_EINVAL; if(len<AEV_EVSZ)return -LX_EINVAL;
    u64 done=0;
    while(done+AEV_EVSZ<=len){ u8 e[AEV_EVSZ]; if(lx_get(e,buf+done,AEV_EVSZ))return done?(long)done:-LX_EFAULT; done+=AEV_EVSZ;
        u16 ty=*(u16*)(e+16), co=*(u16*)(e+18); int val=*(int*)(e+20);
        if(ty!=AEV_EV_FF||co>=16||!aev_ff[k][co].used)continue;
        if(val>0)gpad_rumble(k,aev_ff[k][co].s>>8,aev_ff[k][co].w>>8,aev_ff[k][co].len?aev_ff[k][co].len:5000); else gpad_rumble(k,0,0,0); }
    return (long)done; }
static int aev_poll(struct vfile* v){
    if(!aev_live(v))return 8|16;
    gpusb_poll();
    int re=v->dev==LX_EVDEV?4:0; if((u32)v->off!=gpad_qpos(v->fs_idx)||(v->dev==LX_JSDEV&&aev_jsinit[v-g_vf]))re|=1; return re; }
