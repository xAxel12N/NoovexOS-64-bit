#include "sys/linux/rawmidi_abi.h"
int umidi_ports(void); int umidi_port_ok(int k); int umidi_port_caps(int k); int umidi_port_name(int k,char* out,int cap);
int umidi_port_read(int k,u8* out,int max); int umidi_port_avail(int k); int umidi_port_write(int k,const u8* s,int len); void umidi_port_drop(int k); int umidi_scan(void);
static int armidi_parse(const char* p,int* oss){
    const char* a; int n=0, d=0;
    if(p[0]=='/'&&p[1]=='d'&&p[2]=='e'&&p[3]=='v'&&p[4]=='/'&&p[5]=='m'&&p[6]=='i'&&p[7]=='d'&&p[8]=='i'){ a=p+9; *oss=1; }
    else if(p[0]=='/'&&p[1]=='d'&&p[2]=='e'&&p[3]=='v'&&p[4]=='/'&&p[5]=='s'&&p[6]=='n'&&p[7]=='d'&&p[8]=='/'&&p[9]=='m'&&p[10]=='i'&&p[11]=='d'&&p[12]=='i'&&p[13]=='C'&&p[14]=='0'&&p[15]=='D'){ a=p+16; *oss=0; }
    else return -1;
    if(*a<'0'||*a>'9')return -1;
    while(*a>='0'&&*a<='9'&&d<3){ n=n*10+(*a-'0'); a++; d++; }
    if(*a||(d>1&&p[*oss?9:16]=='0'))return -1;
    return n; }
static int armidi_open(const char* p,int flags){
    int oss=0, k=armidi_parse(p,&oss); if(k<0)return -9999;
    if(!umidi_port_ok(k))umidi_scan();
    if(!umidi_port_ok(k))return -LX_ENOENT;
    int caps=umidi_port_caps(k), m=flags&3;
    if((m!=1&&!(caps&1))||(m!=0&&!(caps&2)))return -6;
    int vi=jc_newvf(LX_SNDMIDI,k,flags&~0x80000); if(vi<0)return -LX_EMFILE;
    int q=0; for(;p[q]&&q<127;q++)g_vf[vi].vpath[q]=p[q]; g_vf[vi].vpath[q]=0;
    if(m!=1)umidi_port_drop(k);
    int fd=lx_fd_install(vi,(flags&0x80000)!=0,0); if(fd<0)vf_unref(vi); return fd; }
static int armidi_stat(const char* p,u32* o,u64* rd){
    int oss=0, k=armidi_parse(p,&oss); if(k<0||!umidi_port_ok(k))return 0;
    for(int i=0;i<10;i++)o[i]=0;
    o[3]=1; o[6]=o[7]=o[8]=1760000000u; o[2]=29;
    o[0]=0020660u; o[5]=0x37100000u+(u32)k; *rd=oss?((14u<<8)|(u32)(2+16*k)):((116u<<8)|(u32)(8+k));
    return 1; }
static int armidi_list(int n,char* name,int oss){
    int c=0; for(int k=0;k<umidi_ports();k++){ if(!umidi_port_ok(k))continue; if(c==n){ char* q=name; const char* s=oss?"midi":"midiC0D"; while(*s)*q++=*s++; if(k>=10)*q++=(char)('0'+k/10); *q++=(char)('0'+k%10); *q=0; return 1; } c++; }
    return 0; }
static void armidi_info(u8* b,int k,int stream){
    u32 dev=(u32)k; lx_zero(b,ARM_INFO_SZ); *(u32*)(b+ARM_INFO_DEVICE)=dev; *(int*)(b+ARM_INFO_STREAM)=stream; *(int*)(b+ARM_INFO_CARD)=0;
    int caps=umidi_port_caps(k); *(u32*)(b+ARM_INFO_FLAGS)=((caps&2)?1u:0)|((caps&1)?2u:0)|(caps==3?4u:0);
    char nm[64]; int l=umidi_port_name(k,nm,64); for(int i=0;i<l&&i<63;i++)b[ARM_INFO_ID+i]=(u8)nm[i]; for(int i=0;i<l&&i<79;i++)b[ARM_INFO_NAME+i]=(u8)nm[i]; for(int i=0;i<l&&i<31;i++)b[ARM_INFO_SUBNAME+i]=(u8)nm[i];
    *(u32*)(b+ARM_INFO_SUBCOUNT)=1; *(u32*)(b+ARM_INFO_SUBAVAIL)=1; }
static long armidi_ctl_ioctl(u32 req,u64 arg){
    if(req==ARM_CTL_NEXT_DEVICE){ int d; if(lx_get(&d,arg,4))return -LX_EFAULT; if(d<-1)d=-1; if(d==-1)umidi_scan(); int n=-1; for(int k=d+1;k<umidi_ports();k++) if(umidi_port_ok(k)){ n=k; break; } return lx_put(arg,&n,4); }
    if(req==ARM_CTL_INFO){ u8 b[ARM_INFO_SZ]; if(lx_get(b,arg,ARM_INFO_SZ))return -LX_EFAULT; u32 dev=*(u32*)(b+ARM_INFO_DEVICE), sub=*(u32*)(b+ARM_INFO_SUBDEVICE); int st=*(int*)(b+ARM_INFO_STREAM);
        if(dev>=16||!umidi_port_ok((int)dev))return -6;
        if(st<0||st>1)return -LX_EINVAL;
        int caps=umidi_port_caps((int)dev);
        if(!(caps&(st?1:2))||(sub!=0&&sub!=0xFFFFFFFFu))return -LX_ENOENT;
        armidi_info(b,(int)dev,st); return lx_put(arg,b,ARM_INFO_SZ); }
    if(req==ARM_CTL_PREFER_SUBDEVICE)return 0;
    return -9999; }
static long armidi_ioctl(struct vfile* v,u32 req,u64 arg){
    int k=v->fs_idx, m=v->flags&3, smask=(m!=1?2:0)|(m!=0?1:0);
    if(req==ARM_IOC_PVERSION){ int x=ARM_VERSION; return lx_put(arg,&x,4); }
    if(req==ARM_IOC_USER_PVERSION)return 0;
    if(!umidi_port_ok(k))return -LX_ENODEV;
    if(req==ARM_IOC_INFO){ u8 b[ARM_INFO_SZ]; armidi_info(b,k,m==0?1:0); return lx_put(arg,b,ARM_INFO_SZ); }
    if(req==ARM_IOC_PARAMS){ u8 b[ARM_PARAMS_SZ]; if(lx_get(b,arg,ARM_PARAMS_SZ))return -LX_EFAULT; return arm_params_ok(b,smask); }
    if(req==ARM_IOC_STATUS){ u8 b[ARM_STATUS_SZ]; if(lx_get(b,arg,ARM_STATUS_SZ))return -LX_EFAULT; int st=*(int*)(b+ARM_STATUS_STREAM); if(st<0||st>1||!((smask>>st)&1))return -LX_EINVAL;
        for(int i=8;i<ARM_STATUS_SZ;i++)b[i]=0;
        int av=st?umidi_port_avail(k):ARM_BUFSZ; if(av<0)av=0; *(u64*)(b+ARM_STATUS_AVAIL)=(u64)av; return lx_put(arg,b,ARM_STATUS_SZ); }
    if(req==ARM_IOC_DROP||req==ARM_IOC_DRAIN){ int st; if(lx_get(&st,arg,4))return -LX_EFAULT; if(st<0||st>1||!((smask>>st)&1))return -LX_EINVAL; if(st==1&&req==ARM_IOC_DROP)umidi_port_drop(k); return 0; }
    if(req==0x541Bu){ int n=(m!=1)?umidi_port_avail(k):0; if(n<0)n=0; return lx_put(arg,&n,4); }
    return -LX_ENOTTY; }
static int armidi_read(struct vfile* v,u64 buf,u64 len){
    int k=v->fs_idx; u8 t[256]; int max=len>sizeof t?(int)sizeof t:(int)len;
    int n=umidi_port_read(k,t,max); if(n<0)return -LX_ENODEV; if(n==0)return JS_BLOCK;
    if(lx_put(buf,t,n))return -LX_EFAULT;
    return n; }
static long armidi_write(struct vfile* v,u64 buf,u64 len){
    int k=v->fs_idx; if(!umidi_port_ok(k))return -LX_ENODEV;
    u8 t[256]; u64 done=0;
    while(done<len){ int c=len-done>sizeof t?(int)sizeof t:(int)(len-done); if(lx_get(t,buf+done,c))return done?(long)done:-LX_EFAULT;
        int w=umidi_port_write(k,t,c); if(w<=0)return done?(long)done:-LX_EIO; done+=(u64)w; if(w<c)break; }
    return (long)done; }
static int armidi_poll(struct vfile* v){
    int k=v->fs_idx, m=v->flags&3; if(!umidi_port_ok(k))return 8|16;
    int re=0; if(m!=1&&umidi_port_avail(k)>0)re|=1; if(m!=0)re|=4; return re; }
