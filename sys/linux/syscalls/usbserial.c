#include "drivers/usb/class/usbser_proto.h"

#define UST_MAX 8
int usbser_scan(void); int usbser_ok(int k); int usbser_find(int acm,int num); int usbser_list(int i,int* acm,int* num); u32 usbser_gen(int k);
int usbser_set_line(int k,u32 baud,int bits,int parity,int stop); int usbser_mctl(int k,int dtr,int rts); int usbser_break(int k,int on); int usbser_modem(int k);
int usbser_read(int k,u8* out,int max); int usbser_avail(int k); void usbser_drop(int k); int usbser_outq(int k); int usbser_drain(int k); int usbser_write(int k,const u8* s,int len);
int usbser_open(int k); int usbser_close(int k);
static long lx_sock_wait(struct sysregs* r,int n,int nb);
static long lx_sock_wt(struct sysregs* r,int n,int nb,long us);
struct ustio { u32 gen, iflag, oflag, cflag, lflag, ispeed, ospeed; int dtr, rts; u8 line, cc[19]; };
static struct ustio g_ust[UST_MAX];
static int ust_parse(const char* p,int* acm){
    if(!(p[0]=='/'&&p[1]=='d'&&p[2]=='e'&&p[3]=='v'&&p[4]=='/'&&p[5]=='t'&&p[6]=='t'&&p[7]=='y'))return -1;
    if(p[8]=='U'&&p[9]=='S'&&p[10]=='B')*acm=0; else if(p[8]=='A'&&p[9]=='C'&&p[10]=='M')*acm=1; else return -1;
    const char* a=p+11; int n=0, d=0;
    if(*a<'0'||*a>'9')return -1;
    while(*a>='0'&&*a<='9'&&d<2){ n=n*10+(*a-'0'); a++; d++; }
    if(*a||(d>1&&p[11]=='0'))return -1;
    return n; }
static struct ustio* ust_get(int k){
    struct ustio* t=&g_ust[k]; u32 g=usbser_gen(k);
    if(t->gen!=g){ t->gen=g; t->iflag=0x500; t->oflag=5; t->cflag=0xD|0x30|USR_CREAD|USR_HUPCL|USR_CLOCAL; t->lflag=0x8A3B; t->line=0; t->ispeed=t->ospeed=9600; t->dtr=t->rts=0;
        for(int i=0;i<19;i++)t->cc[i]=0;
        t->cc[0]=3; t->cc[1]=0x1C; t->cc[2]=0x7F; t->cc[3]=0x15; t->cc[4]=4; t->cc[6]=1; t->cc[8]=0x11; t->cc[9]=0x13; t->cc[10]=0x1A; }
    return t; }
static int ust_mctl(int k,struct ustio* t,int dtr,int rts){ if(usbser_mctl(k,dtr,rts))return -LX_EIO; t->dtr=dtr?1:0; t->rts=rts?1:0; return 0; }
static int ust_apply(int k,struct ustio* t,u32 cflag,u32 ospeed){
    struct usr_line l; u32 was=usr_cflag_baud(t->cflag,t->ospeed);
    if(!usr_cflag_line(cflag,ospeed,&l)){ t->cflag=cflag; t->ospeed=0; return ust_mctl(k,t,0,0); }
    int e=usbser_set_line(k,l.baud,l.bits,l.parity,l.stop); if(e==-2)return -LX_EINVAL; if(e)return -LX_EIO;
    t->cflag=cflag; t->ospeed=l.baud; t->ispeed=l.baud;
    if(!was)return ust_mctl(k,t,1,1);
    return 0; }
static int userial_open(const char* p,int flags){
    int acm=0, n=ust_parse(p,&acm); if(n<0)return -9999;
    int k=usbser_find(acm,n); if(k<0){ usbser_scan(); k=usbser_find(acm,n); }
    if(k<0||k>=UST_MAX)return -LX_ENOENT;
    int vi=jc_newvf(LX_USBSER,k,flags&~0x80000); if(vi<0)return -LX_EMFILE;
    int q=0; for(;p[q]&&q<127;q++)g_vf[vi].vpath[q]=p[q]; g_vf[vi].vpath[q]=0;
    int fd=lx_fd_install(vi,(flags&0x80000)!=0,0); if(fd<0){ vf_unref(vi); return fd; }
    if(usbser_open(k)==1){ struct ustio* t=ust_get(k); usbser_drop(k); int e=ust_apply(k,t,t->cflag,t->ospeed); if(!e&&usr_cflag_baud(t->cflag,t->ospeed))ust_mctl(k,t,1,1); }
    return fd; }
void userial_vclose(int k){
    if(k<0||k>=UST_MAX)return;
    if(usbser_close(k)==0&&usbser_ok(k)){ struct ustio* t=ust_get(k); if(t->cflag&USR_HUPCL)ust_mctl(k,t,0,0); } }
static int userial_stat(const char* p,u32* o,u64* rd){
    int acm=0, n=ust_parse(p,&acm); if(n<0)return 0;
    int k=usbser_find(acm,n); if(k<0){ usbser_scan(); k=usbser_find(acm,n); } if(k<0)return 0;
    for(int i=0;i<10;i++)o[i]=0;
    o[3]=1; o[6]=o[7]=o[8]=1760000000u; o[2]=20;
    o[0]=0020660u; o[5]=0x37200000u+(u32)(acm*32+n); *rd=acm?((166u<<8)|(u32)n):((188u<<8)|(u32)n);
    return 1; }
static int userial_list(int i,char* name){
    int acm=0, num=0; if(usbser_list(i,&acm,&num)<0)return 0;
    const char* s=acm?"ttyACM":"ttyUSB"; char* q=name; while(*s)*q++=*s++; if(num>=10)*q++=(char)('0'+num/10); *q++=(char)('0'+num%10); *q=0; return 1; }
static int ust_put(u64 arg,struct ustio* t,int two){
    u8 b[44]; lx_zero(b,44); *(u32*)(b+0)=t->iflag; *(u32*)(b+4)=t->oflag; *(u32*)(b+8)=t->cflag; *(u32*)(b+12)=t->lflag; b[16]=t->line; for(int i=0;i<19;i++)b[17+i]=t->cc[i];
    *(u32*)(b+36)=t->ispeed; *(u32*)(b+40)=t->ospeed; return lx_put(arg,b,two?44:36); }
static long userial_ioctl(struct vfile* v,u32 req,u64 arg){
    int k=v->fs_idx; if(k<0||k>=UST_MAX)return -LX_EBADF;
    if(!usbser_ok(k))return -LX_EIO;
    struct ustio* t=ust_get(k);
    switch(req){
    case 0x5401: return ust_put(arg,t,0);
    case 0x802C542Au: return ust_put(arg,t,1);
    case 0x5402: case 0x5403: case 0x5404: case 0x402C542Bu: case 0x402C542Cu: case 0x402C542Du: {
        int two=(req>>16)!=0; u8 b[44]; if(lx_get(b,arg,two?44:36))return -LX_EFAULT;
        int op=two?(int)(req&0xFF)-0x2B:(int)req-0x5402;
        if(op>=1)usbser_drain(k);
        if(op==2)usbser_drop(k);
        u32 cf=*(u32*)(b+8), os=two?*(u32*)(b+40):0;
        if(!two&&(cf&USR_CBAUD)==USR_BOTHER){ cf=(cf&~USR_CBAUD)|(t->cflag&USR_CBAUD); os=t->ospeed; }
        int e=ust_apply(k,t,cf,os); if(e)return e;
        t->iflag=*(u32*)(b+0); t->oflag=*(u32*)(b+4); t->lflag=*(u32*)(b+12); t->line=b[16]; for(int i=0;i<19;i++)t->cc[i]=b[17+i];
        return 0; }
    case 0x5409: case 0x5425: { usbser_drain(k); if(req==0x5409&&arg)return 0; u32 us=req==0x5425&&arg?(u32)(arg>30?30:arg)*100000u:250000u; if(usbser_break(k,1))return -LX_EIO; hr_delay_us(us); usbser_break(k,0); return 0; }
    case 0x5427: return usbser_break(k,1)?-LX_EIO:0;
    case 0x5428: return usbser_break(k,0)?-LX_EIO:0;
    case 0x540A: case 0x540C: case 0x540D: return 0;
    case 0x540B: { if(arg>2)return -LX_EINVAL; if(arg==0||arg==2)usbser_drop(k); return 0; }
    case 0x5415: { int m=usbser_modem(k); if(m<0)return -LX_EIO; m=(m&~(USR_TIOCM_DTR|USR_TIOCM_RTS))|(t->dtr?USR_TIOCM_DTR:0)|(t->rts?USR_TIOCM_RTS:0); return lx_put(arg,&m,4); }
    case 0x5416: case 0x5417: case 0x5418: { int m; if(lx_get(&m,arg,4))return -LX_EFAULT;
        int dtr=t->dtr, rts=t->rts;
        if(req==0x5418){ dtr=(m&USR_TIOCM_DTR)!=0; rts=(m&USR_TIOCM_RTS)!=0; }
        else { int on=req==0x5416; if(m&USR_TIOCM_DTR)dtr=on; if(m&USR_TIOCM_RTS)rts=on; }
        return ust_mctl(k,t,dtr,rts); }
    case 0x541B: { int n=usbser_avail(k); if(n<0)n=0; return lx_put(arg,&n,4); }
    case 0x5411: { int n=usbser_outq(k); return lx_put(arg,&n,4); }
    case 0x5422: { int d=0; return lx_put(arg,&d,4); }
    }
    return -LX_ENOTTY; }
static long userial_read(struct sysregs* r,struct vfile* v,u64 buf,u64 len){
    int k=v->fs_idx; if(k<0||k>=UST_MAX)return -LX_EBADF;
    u8 tb[512]; int max=len>sizeof tb?(int)sizeof tb:(int)len;
    int n=usbser_read(k,tb,max); if(n<0)return 0;
    if(n>0){ if(lx_put(buf,tb,n))return -LX_EFAULT; return n; }
    struct ustio* t=ust_get(k); int nb=lx_nonblock(v);
    if(!nb&&t->cc[6]==0){ if(t->cc[5]==0)return 0; long w=lx_sock_wt(r,JS_BLOCK,0,(long)t->cc[5]*100000L); return w==-LX_EAGAIN?0:w; }
    return lx_sock_wait(r,JS_BLOCK,nb); }
static long userial_write(struct vfile* v,u64 buf,u64 len){
    int k=v->fs_idx; if(k<0||k>=UST_MAX||!usbser_ok(k))return -LX_EIO;
    u8 tb[512]; u64 done=0;
    while(done<len){ int c=len-done>sizeof tb?(int)sizeof tb:(int)(len-done); if(lx_get(tb,buf+done,c))return done?(long)done:-LX_EFAULT;
        int w=usbser_write(k,tb,c); if(w<=0)return done?(long)done:-LX_EIO; done+=(u64)w; if(w<c)break; }
    return (long)done; }
static int userial_poll(struct vfile* v){
    int k=v->fs_idx; if(k<0||k>=UST_MAX||!usbser_ok(k))return 1|8|16;
    int re=4; if(usbser_avail(k)>0)re|=1; return re; }
