static void ivl_muldivk(const struct ivl* a,const struct ivl* b,u32 k,struct ivl* c){
    lx_zero((u8*)c,sizeof *c); if(a->empty||b->empty){ c->empty=1; return; }
    u64 m=(u64)a->min*b->min; c->min=sat32(m/k); c->omin=(m%k)!=0||a->omin||b->omin;
    m=(u64)a->max*b->max; c->max=sat32(m/k); if(m%k){ c->max++; c->omax=1; } else c->omax=a->omax||b->omax; }
static void ivl_mulkdiv(const struct ivl* a,u32 k,const struct ivl* b,struct ivl* c){
    lx_zero((u8*)c,sizeof *c); if(a->empty||b->empty){ c->empty=1; return; }
    if(b->max==0){ c->empty=1; return; }
    u64 m=(u64)a->min*k; c->min=sat32(m/b->max); c->omin=(m%b->max)!=0||a->omin||b->omax;
    if(b->min>0){ m=(u64)a->max*k; c->max=sat32(m/b->min); if(m%b->min){ c->max++; c->omax=1; } else c->omax=a->omax||b->omin; } else { c->max=0xFFFFFFFFu; c->omax=0; } }
enum { HP_SB=0, HP_FB, HP_CH, HP_RATE, HP_PT, HP_PS, HP_PB, HP_PER, HP_BT, HP_BS, HP_BB, HP_TT };
static void hp_get(const u8* hp,struct ivl* iv){ for(int i=0;i<12;i++){ const u8* q=hp+260+i*12; iv[i].min=*(const u32*)q; iv[i].max=*(const u32*)(q+4); u32 f=*(const u32*)(q+8); iv[i].omin=f&1; iv[i].omax=(f>>1)&1; iv[i].integer=(f>>2)&1; iv[i].empty=(f>>3)&1; } }
static void hp_put(u8* hp,const struct ivl* iv){ for(int i=0;i<12;i++){ u8* q=hp+260+i*12; *(u32*)q=iv[i].min; *(u32*)(q+4)=iv[i].max; *(u32*)(q+8)=(u32)((iv[i].omin?1:0)|(iv[i].omax?2:0)|(iv[i].integer?4:0)|(iv[i].empty?8:0)); } }
static u32 asnd_rate(void){ return snd_init()&&g_snd.rate>0?(u32)g_snd.rate:48000u; }
static int asnd_refine(u8* hp,int choose){
    u32* mk=(u32*)(hp+4);
    mk[0]&=(1u<<3); for(int i=1;i<8;i++)mk[i]=0;
    mk[8]&=(1u<<2); for(int i=9;i<16;i++)mk[i]=0;
    mk[16]&=1u; for(int i=17;i<24;i++)mk[i]=0;
    if(!mk[0]||!mk[8]||!mk[16])return -LX_EINVAL;
    struct ivl iv[12]; hp_get(hp,iv);
    u32 R=asnd_rate();
    struct ivl hw[12]; lx_zero((u8*)hw,sizeof hw);
    for(int i=0;i<12;i++){ hw[i].min=0; hw[i].max=0xFFFFFFFFu; }
    hw[HP_SB].min=hw[HP_SB].max=16; hw[HP_FB].min=hw[HP_FB].max=32; hw[HP_CH].min=hw[HP_CH].max=2; hw[HP_RATE].min=hw[HP_RATE].max=R;
    hw[HP_PB].min=256; hw[HP_PB].max=65536; hw[HP_PER].min=2; hw[HP_PER].max=64; hw[HP_PER].integer=1; hw[HP_BB].min=1024; hw[HP_BB].max=524288;
    hw[HP_PS].integer=1; hw[HP_BS].integer=1; hw[HP_SB].integer=1; hw[HP_FB].integer=1; hw[HP_CH].integer=1;
    for(int i=0;i<12;i++) if(i!=HP_TT&&ivl_refine(&iv[i],&hw[i])<0)return -LX_EINVAL;
    for(int pass=0;pass<64;pass++){
        int any=0; struct ivl t;
        #define RULE(dst,expr) do{ expr; int r_=ivl_refine(&iv[dst],&t); if(r_<0)return -LX_EINVAL; if(r_)any=1; }while(0)
        RULE(HP_SB,ivl_div(&iv[HP_FB],&iv[HP_CH],&t));
        RULE(HP_FB,ivl_mul(&iv[HP_SB],&iv[HP_CH],&t));
        RULE(HP_FB,ivl_mulkdiv(&iv[HP_PB],8,&iv[HP_PS],&t));
        RULE(HP_FB,ivl_mulkdiv(&iv[HP_BB],8,&iv[HP_BS],&t));
        RULE(HP_CH,ivl_div(&iv[HP_FB],&iv[HP_SB],&t));
        RULE(HP_RATE,ivl_mulkdiv(&iv[HP_PS],1000000,&iv[HP_PT],&t));
        RULE(HP_RATE,ivl_mulkdiv(&iv[HP_BS],1000000,&iv[HP_BT],&t));
        RULE(HP_PER,ivl_div(&iv[HP_BS],&iv[HP_PS],&t));
        RULE(HP_PS,ivl_div(&iv[HP_BS],&iv[HP_PER],&t));
        RULE(HP_PS,ivl_mulkdiv(&iv[HP_PB],8,&iv[HP_FB],&t));
        RULE(HP_PS,ivl_muldivk(&iv[HP_PT],&iv[HP_RATE],1000000,&t));
        RULE(HP_BS,ivl_mul(&iv[HP_PS],&iv[HP_PER],&t));
        RULE(HP_BS,ivl_mulkdiv(&iv[HP_BB],8,&iv[HP_FB],&t));
        RULE(HP_BS,ivl_muldivk(&iv[HP_BT],&iv[HP_RATE],1000000,&t));
        RULE(HP_PB,ivl_muldivk(&iv[HP_PS],&iv[HP_FB],8,&t));
        RULE(HP_BB,ivl_muldivk(&iv[HP_BS],&iv[HP_FB],8,&t));
        RULE(HP_PT,ivl_mulkdiv(&iv[HP_PS],1000000,&iv[HP_RATE],&t));
        RULE(HP_BT,ivl_mulkdiv(&iv[HP_BS],1000000,&iv[HP_RATE],&t));
        #undef RULE
        if(!any){
            if(!choose)break;
            static const int order[]={HP_PS,HP_PER,HP_BS,-1}; int done=1;
            for(int k=0;order[k]>=0;k++){ struct ivl* x=&iv[order[k]]; if(x->min!=x->max){ struct ivl one; lx_zero((u8*)&one,sizeof one); one.integer=1; if(order[k]==HP_BS){ one.min=one.max=x->max-(x->omax?1:0); } else { one.min=one.max=x->min+(x->omin?1:0); } if(ivl_refine(x,&one)<0)return -LX_EINVAL; done=0; break; } }
            if(done)break; }
    }
    hp_put(hp,iv);
    *(u32*)(hp+516)=0xFFFFFFFFu;
    *(u32*)(hp+520)=0x100u|0x10000u|0x80000u|0x10u;
    *(u32*)(hp+524)=16; *(u32*)(hp+528)=R; *(u32*)(hp+532)=1; *(u64*)(hp+536)=0;
    return 0; }
struct lx_apcm { int used, stream, state, nb; u32 rate; u64 psize, bsize, hw, appl, boundary, avail_min, start_th, stop_th; u8* ring; u64 tlast; u64 trig; };
static struct lx_apcm g_apcm[2];
static u64 g_apcm_devcap;
static u64 apcm_devq(void){ if(!snd_init())return 0; int fb=g_snd.free_bytes(); if(fb<0)fb=0; if((u64)fb>g_apcm_devcap)g_apcm_devcap=(u64)fb; return (g_apcm_devcap-(u64)fb)/4; }
static u64 apcm_queued(struct lx_apcm* a){ return (a->appl+a->boundary-a->hw)%a->boundary; }
static u64 apcm_avail(struct lx_apcm* a){ if(a->stream==0)return a->bsize-apcm_queued(a); u64 av=(u64)snd_cap_avail()/4; return av>a->bsize?a->bsize:av; }
static volatile int g_apcm_busy;
static void apcm_pump2(struct lx_apcm* a);
static void apcm_pump(struct lx_apcm* a){ if(__sync_lock_test_and_set(&g_apcm_busy,1))return; apcm_pump2(a); __sync_lock_release(&g_apcm_busy); }
static void apcm_pump2(struct lx_apcm* a){
    if(!a->used||!a->bsize||!a->boundary)return;
    if(a->stream==1){ if(a->state==3){ u64 av=(u64)snd_cap_avail()/4; if(av>a->bsize){ u64 drop=av-a->bsize; u8 junk[512]; while(drop){ u64 k=drop>128?128:drop; snd_cap_read(junk,(int)k*4); drop-=k; } } a->hw=(a->appl+(av>a->bsize?a->bsize:av))%a->boundary; } return; }
    if(a->state!=3&&a->state!=5)return;
    u64 q=apcm_queued(a);
    u64 now=hr_ns();
    if(!q){ if(a->state==5&&!apcm_devq())a->state=1; a->tlast=now; return; }
    u64 can;
    if(snd_init()){ g_snd.start(); int fb=g_snd.free_bytes(); if(fb<0)fb=0; if((u64)fb>g_apcm_devcap)g_apcm_devcap=(u64)fb; u64 fill=(g_apcm_devcap-(u64)fb)/4; u64 lim=(u64)a->rate*80/1000; can=fill<lim?lim-fill:0; if(can>(u64)fb/4)can=(u64)fb/4; }
    else { u64 dt=now-a->tlast; can=dt*(u64)a->rate/1000000000ULL; if(can)a->tlast=now; else return; }
    if(can>q)can=q;
    u64 done=0;
    while(done<can){ u64 pos=(a->hw+done)%a->bsize; u64 run=a->bsize-pos; if(run>can-done)run=can-done;
        if(snd_init()){ int w=g_snd.write(a->ring+pos*4,(int)(run*4)); if(w<=0)break; done+=(u64)w/4; if((u64)w<run*4)break; } else done+=run; }
    a->hw=(a->hw+done)%a->boundary; a->tlast=now;
    if(apcm_queued(a)==0&&a->state==5&&!apcm_devq())a->state=1; }
void apcm_tick(void){ for(int i=0;i<2;i++) if(g_apcm[i].used)apcm_pump(&g_apcm[i]); }
static int asnd_open(const char* p,int flags){
    int k=-1; if(lx_streq(p,"/dev/snd/controlC0"))k=2; else if(lx_streq(p,"/dev/snd/pcmC0D0p"))k=0; else if(lx_streq(p,"/dev/snd/pcmC0D0c"))k=snd_rec_avail()?1:-2; else return -9999;
    if(k==-2)return -LX_ENOENT;
    if(k<2){ if(g_apcm[k].used)return -LX_EBUSY; lx_zero((u8*)&g_apcm[k],sizeof g_apcm[k]); g_apcm[k].used=1; g_apcm[k].stream=k; g_apcm[k].state=0; g_apcm[k].rate=asnd_rate(); }
    int vi=jc_newvf(k==2?LX_SNDCTL:LX_SNDPCM,k,flags&~0x80000); if(vi<0){ if(k<2)g_apcm[k].used=0; return -LX_EMFILE; }
    int q=0; for(;p[q]&&q<127;q++)g_vf[vi].vpath[q]=p[q]; g_vf[vi].vpath[q]=0;
    int fd=lx_fd_install(vi,(flags&0x80000)!=0,0); if(fd<0)vf_unref(vi); return fd; }
void asnd_close(int dev,int k){ if(dev!=LX_SNDPCM||k<0||k>1)return; struct lx_apcm* a=&g_apcm[k]; if(a->stream==1)snd_rec_stop(); if(a->ring)kfree(a->ring); lx_zero((u8*)a,sizeof *a); }
static int asnd_stat(const char* p,u32* o,u64* rd){
    int t=-1; if(lx_streq(p,"/dev/snd"))t=0; else if(lx_streq(p,"/dev/snd/controlC0"))t=1; else if(lx_streq(p,"/dev/snd/pcmC0D0p"))t=2; else if(lx_streq(p,"/dev/snd/pcmC0D0c")&&snd_rec_avail())t=3; else return 0;
    for(int i=0;i<10;i++)o[i]=0; o[3]=1; o[6]=o[7]=o[8]=1760000000u; o[2]=29;
    if(t==0){ o[0]=0040755u; o[3]=2; o[5]=0x37000000u; *rd=0; } else { o[0]=0020660u; o[5]=0x37000000u+(u32)t; *rd=(116u<<8)|(t==1?0u:t==2?16u:24u); }
    return 1; }
static int asnd_list(int n,char* name,int* type){ static const char* const N[]={"controlC0","pcmC0D0p","pcmC0D0c",0}; int c=0; for(int i=0;N[i];i++){ if(i==2&&!snd_rec_avail())continue; if(c==n){ int q=0; for(;N[i][q];q++)name[q]=N[i][q]; name[q]=0; *type=2; return 1; } c++; } return 0; }
static void asnd_info(u8* b,int stream){ lx_zero(b,288); *(u32*)(b+8)=(u32)stream; const char* id="NoovexOS"; for(int i=0;id[i];i++)b[16+i]=(u8)id[i]; const char* nm="NoovexOS PCM"; for(int i=0;nm[i];i++)b[80+i]=(u8)nm[i]; const char* sn="subdevice #0"; for(int i=0;sn[i];i++)b[160+i]=(u8)sn[i]; *(u32*)(b+200)=1; *(u32*)(b+204)=1; }
static int asnd_poll(struct vfile* v){
    if(v->dev==LX_SNDCTL)return 4;
    struct lx_apcm* a=&g_apcm[v->fs_idx]; apcm_pump(a);
    if(a->state==4)return 8|1|4;
    if(a->state<2)return 0;
    u64 av=apcm_avail(a); u64 am=a->avail_min?a->avail_min:1;
    if(a->stream==0)return av>=am?4:0;
    return av>=am?1:0; }
static long asnd_ctl_ioctl(u32 req,u64 arg){
    switch(req){
    case 0x80045500u: { int v=0x20009; return lx_put(arg,&v,4); }
    case 0x81785501u: { u8 b[376]; lx_zero(b,376); const char* id="NoovexOS"; for(int i=0;id[i];i++){ b[8+i]=(u8)id[i]; b[24+i]=(u8)id[i]; } const char* nm="NoovexOS Audio"; for(int i=0;nm[i];i++){ b[40+i]=(u8)nm[i]; b[72+i]=(u8)nm[i]; } const char* hn=snd_init()&&g_snd.name?g_snd.name:"no sound card"; int o=72+14; b[o++]=' '; b[o++]='('; for(int i=0;hn[i]&&o<150;i++)b[o++]=(u8)hn[i]; b[o++]=')'; const char* mx="NoovexOS Mixer"; for(int i=0;mx[i];i++)b[168+i]=(u8)mx[i]; return lx_put(arg,b,376); }
    case 0xC0505510u: { u8 b[80]; if(lx_get(b,arg,80))return -LX_EFAULT; *(u32*)(b+8)=0; *(u32*)(b+12)=0; return lx_put(arg,b,80); }
    case 0x80045530u: { int d; if(lx_get(&d,arg,4))return -LX_EFAULT; d=d<0?0:-1; return lx_put(arg,&d,4); }
    case 0xC1205531u: { u8 b[288]; if(lx_get(b,arg,288))return -LX_EFAULT; u32 dev=*(u32*)b, sub=*(u32*)(b+4); int st=*(int*)(b+8); if(dev!=0||sub!=0||st<0||st>1||(st==1&&!snd_rec_avail()))return -LX_ENOENT; asnd_info(b,st); return lx_put(arg,b,288); }
    case 0x40045532u: return 0;
    case 0xC0045516u: { int z=0; return lx_put(arg,&z,4); }
    case 0xC0045540u: case 0xC0045520u: { int d=-1; return lx_put(arg,&d,4); }
    case 0x800455d1u: { int z=0; return lx_put(arg,&z,4); }
    }
    return -LX_ENOTTY; }
static long asnd_pcm_ioctl(struct sysregs* r,struct vfile* v,u32 req,u64 arg){
    struct lx_apcm* a=&g_apcm[v->fs_idx]; apcm_pump(a);
    switch(req){
    case 0x80044100u: { int ver=0x20011; return lx_put(arg,&ver,4); }
    case 0x81204101u: { u8 b[288]; asnd_info(b,a->stream); return lx_put(arg,b,288); }
    case 0x40044102u: case 0x40044103u: case 0x40044104u: return 0;
    case 0xC2604110u: case 0xC2604111u: { u8 hp[608]; if(lx_get(hp,arg,608))return -LX_EFAULT; if(req==0xC2604111u&&a->state>=3&&a->state!=4)return -LX_EBADFD;
        int e=asnd_refine(hp,req==0xC2604111u); if(e)return e;
        if(req==0xC2604111u){ struct ivl iv[12]; hp_get(hp,iv); u64 ps=iv[HP_PS].min, bs=iv[HP_BS].min; if(!ps||!bs)return -LX_EINVAL;
            u8* ring=0; if(a->stream==0){ ring=(u8*)kmalloc((unsigned long)(bs*4)); if(!ring)return -LX_ENOMEM; }
            if(a->ring)kfree(a->ring); a->ring=ring; a->psize=ps; a->bsize=bs; a->rate=iv[HP_RATE].min; a->hw=0; a->appl=0;
            u64 b=bs; while(b*2<=0x7FFFFFFFFFFFFFFFULL-bs)b*=2; a->boundary=b; a->avail_min=ps; a->start_th=1; a->stop_th=bs; a->state=1; }
        return lx_put(arg,hp,608); }
    case 0x4112u: if(a->state>=3&&a->state!=4)return -LX_EBADFD; if(a->ring)kfree(a->ring); a->ring=0; a->bsize=0; a->state=0; return 0;
    case 0xC0884113u: { u8 sp[136]; if(lx_get(sp,arg,136))return -LX_EFAULT; if(a->state<1)return -LX_EBADFD;
        a->avail_min=*(u64*)(sp+16); if(!a->avail_min)a->avail_min=1; a->start_th=*(u64*)(sp+32); a->stop_th=*(u64*)(sp+40); *(u64*)(sp+64)=a->boundary; return lx_put(arg,sp,136); }
    case 0x80984120u: case 0xC0984124u: { u8 st[152]; lx_zero(st,152); u64 ns=hr_ns(); *(u32*)st=(u32)a->state; *(u64*)(st+24)=ns/1000000000ULL; *(u64*)(st+32)=ns%1000000000ULL; *(u64*)(st+8)=a->trig/1000000000ULL; *(u64*)(st+16)=a->trig%1000000000ULL;
        *(u64*)(st+40)=a->appl; *(u64*)(st+48)=a->hw; *(u64*)(st+56)=a->stream==0?apcm_queued(a)+(a->state==3||a->state==5?apcm_devq():0):apcm_avail(a); *(u64*)(st+64)=a->bsize?apcm_avail(a):0; *(u64*)(st+72)=*(u64*)(st+64); return lx_put(arg,st,152); }
    case 0x80084121u: { if(a->state==4)return -LX_EPIPE; long d=(long)(a->stream==0?apcm_queued(a)+(a->state==3||a->state==5?apcm_devq():0):apcm_avail(a)); return lx_put(arg,&d,8); }
    case 0x4122u: return a->state==4?-LX_EPIPE:0;
    case 0xC0884123u: { u8 sp[136]; if(lx_get(sp,arg,136))return -LX_EFAULT; u32 fl=*(u32*)sp;
        if(!(fl&2)&&a->boundary){ u64 ap=*(u64*)(sp+72); if(ap<a->boundary){ a->appl=ap; if(a->stream==0&&a->state==2&&apcm_queued(a)>=a->start_th&&a->start_th<a->boundary){ a->state=3; a->trig=hr_ns(); a->tlast=a->trig; } } }
        if(!(fl&4)){ u64 am=*(u64*)(sp+80); if(am)a->avail_min=am; }
        apcm_pump(a);
        u64 ns=hr_ns(); lx_zero(sp+8,64); *(u32*)(sp+8)=(u32)a->state; *(u64*)(sp+16)=a->hw; *(u64*)(sp+24)=ns/1000000000ULL; *(u64*)(sp+32)=ns%1000000000ULL;
        *(u64*)(sp+72)=a->appl; *(u64*)(sp+80)=a->avail_min; return lx_put(arg,sp,136); }
    case 0x80184132u: { u8 ci[24]; if(lx_get(ci,arg,24))return -LX_EFAULT; u32 ch=*(u32*)ci; if(ch>1)return -LX_EINVAL; *(u64*)(ci+8)=0; *(u32*)(ci+16)=ch*16; *(u32*)(ci+20)=32; return lx_put(arg,ci,24); }
    case 0x4140u: if(a->state<1)return -LX_EBADFD; a->state=2; a->appl=a->hw; if(a->stream==1){ snd_rec_start(); } return 0;
    case 0x4141u: a->appl=a->hw; return 0;
    case 0x4142u: if(a->state!=2)return -LX_EBADFD; a->state=3; a->trig=hr_ns(); a->tlast=a->trig; if(a->stream==1)snd_rec_start(); return 0;
    case 0x4143u: if(a->state<1)return -LX_EBADFD; if(a->stream==1)snd_rec_stop(); a->state=1; a->appl=a->hw; return 0;
    case 0x4144u: { if(a->state<2)return -LX_EBADFD; if(a->stream==1){ snd_rec_stop(); a->state=1; return 0; }
        if(a->state==2){ if(!apcm_queued(a)){ a->state=1; return 0; } a->state=3; a->trig=hr_ns(); a->tlast=a->trig; }
        if(a->state==3||a->state==6)a->state=5;
        apcm_pump(a); if(a->state==5){ if(lx_nonblock(v))return -LX_EAGAIN; return lx_sock_wait(r,JS_BLOCK,0); } return 0; }
    case 0x40044145u: { if(arg){ if(a->state!=3)return -LX_EBADFD; a->state=6; } else { if(a->state!=6)return -LX_EBADFD; a->state=3; a->tlast=hr_ns(); } return 0; }
    case 0x40084146u: case 0x40084149u: { u64 fr; if(lx_get(&fr,arg,8))return -LX_EFAULT; u64 n=0;
        if(req==0x40084146u){ u64 q=a->stream==0?apcm_queued(a):0; n=fr<q?fr:q; a->appl=(a->appl+a->boundary-n)%a->boundary; }
        else { u64 av=apcm_avail(a); n=fr<av?fr:av; a->appl=(a->appl+n)%a->boundary; }
        return lx_put(arg,&n,8); }
    case 0x4147u: return -LX_ENOSYS;
    case 0x4148u: if(a->state==3)a->state=4; return 0;
    case 0x40184150u: case 0x80184151u: { u8 x[24]; if(lx_get(x,arg,24))return -LX_EFAULT; u64 ub=*(u64*)(x+8); u64 fr=*(u64*)(x+16);
        if((req==0x40184150u)!=(a->stream==0))return -LX_EINVAL;
        if(a->state==4)return -LX_EPIPE; if(a->state<2||a->state==5)return -LX_EBADFD; if(!fr){ *(i64*)x=0; return lx_put(arg,x,24); }
        if(a->stream==0){ if(!lx_ok(ub,fr*4))return -LX_EFAULT; u64 sp=a->bsize-apcm_queued(a);
            if(!sp){ apcm_pump(a); sp=a->bsize-apcm_queued(a); }
            if(!sp){ if(lx_nonblock(v))return -LX_EAGAIN; if(a->state==2){ a->state=3; a->trig=hr_ns(); a->tlast=a->trig; } return lx_sock_wait(r,JS_BLOCK,0); }
            u64 n=fr<sp?fr:sp; u64 done=0;
            while(done<n){ u64 pos=(a->appl+done)%a->bsize; u64 run=a->bsize-pos; if(run>n-done)run=n-done; const u8* s=(const u8*)(ub+done*4); u8* d=a->ring+pos*4; for(u64 i=0;i<run*4;i++)d[i]=s[i]; done+=run; }
            a->appl=(a->appl+n)%a->boundary;
            if(a->state==2&&apcm_queued(a)>=a->start_th){ a->state=3; a->trig=hr_ns(); a->tlast=a->trig; }
            apcm_pump(a); *(i64*)x=(i64)n; return lx_put(arg,x,24); }
        if(!lx_okw(ub,fr*4))return -LX_EFAULT;
        if(a->state==2){ a->state=3; a->trig=hr_ns(); snd_rec_start(); }
        u64 av=(u64)snd_cap_avail()/4; if(!av){ if(lx_nonblock(v))return -LX_EAGAIN; return lx_sock_wait(r,JS_BLOCK,0); }
        u64 n=fr<av?fr:av; int got=snd_cap_read((u8*)ub,(int)(n*4)); n=got>0?(u64)got/4:0; a->appl=(a->appl+n)%a->boundary; a->hw=a->appl; *(i64*)x=(i64)n; return lx_put(arg,x,24); }
    case 0x40044160u: case 0x4161u: return -LX_EINVAL;
    }
    return -LX_ENOTTY; }
#define UN_MAX 512
#define UN_CAP 262144
#define UN_FQ 64
struct fbatch { u64 at; int nf,vf[64]; };
struct unsock { int used,state,peer,klen,rxn,rd_shut,wr_shut,backlog,qn,fqh,fqt,seq,rlh,rlt,cpid,passcred; u8 key[108]; u8* rx; u64 rcons; int q[16]; struct fbatch* fq; int rl[64]; };
static struct unsock g_un[UN_MAX];
static struct unsock* un_get(int i){ return (i>=0&&i<UN_MAX&&g_un[i].used)?&g_un[i]:0; }
static char* un_desc(char* q,int i){ struct unsock* s=un_get(i); if(!s)return scopy(q,"un gone"); q=scopy(q,"un st "); q=udec(q,(u32)s->state); q=scopy(q," rx "); q=udec(q,(u32)s->rxn); q=scopy(q," rl "); q=udec(q,(u32)(s->rlt-s->rlh)); q=scopy(q," fq "); q=udec(q,(u32)(s->fqt-s->fqh)); q=scopy(q," shut "); q=udec(q,(u32)(s->rd_shut*2+s->wr_shut)); struct unsock* pr=un_get(s->peer); if(pr&&s->state==4){ q=scopy(q," peer "); q=udec(q,(u32)s->peer); q=scopy(q," prx "); q=udec(q,(u32)pr->rxn); q=scopy(q," prl "); q=udec(q,(u32)(pr->rlt-pr->rlh)); } return q; }
static int un_keyeq(struct unsock* s,const u8* k,int kl){ if(s->klen!=kl||kl<=0)return 0; for(int i=0;i<kl;i++) if(s->key[i]!=k[i])return 0; return 1; }
static int un_new(void){ for(int i=0;i<UN_MAX;i++) if(!g_un[i].used){ struct unsock* s=&g_un[i]; u8* z=(u8*)s; for(int k=0;k<(int)sizeof *s;k++)z[k]=0; s->rx=(u8*)kmalloc(UN_CAP); if(!s->rx)return -12; s->fq=(struct fbatch*)kmalloc(sizeof(struct fbatch)*UN_FQ); if(!s->fq){ kfree(s->rx); s->rx=0; return -12; } s->used=1; s->state=1; s->peer=-1; s->cpid=g_jcur>=0?jc_lead(&g_jp[g_jcur])->pid:0; return i; } return -24; }
static void un_drop_fds(struct unsock* s){ while(s->fqh!=s->fqt){ struct fbatch* b=&s->fq[s->fqh%UN_FQ]; for(int k=0;k<b->nf;k++)vf_unref(b->vf[k]); s->fqh++; } }
void un_close(int i){ struct unsock* s=un_get(i); if(!s)return;
    { extern int g_lxtrace; if(g_lxtrace==-4&&s->state==4&&s->peer>=0&&un_get(s->peer)&&g_jcur>=0){ extern void lx_sc_dump(u32,int); char b[160],*q=b; q=scopy(q,"lx un close: pid "); q=udec(q,(u32)g_jp[g_jcur].pid); q=scopy(q," tg "); q=udec(q,(u32)jc_lead(&g_jp[g_jcur])->pid); q=scopy(q," sock "); q=udec(q,(u32)i); q=scopy(q," "); q=un_desc(q,i); *q=0; klog(b); lx_sc_dump((u32)g_jp[g_jcur].pid,24); } }
    un_drop_fds(s);
    if(s->peer>=0){ struct unsock* pr=un_get(s->peer); if(pr&&pr->peer==i){ pr->peer=-1; pr->rd_shut=0; } }
    for(int k=0;k<s->qn;k++){ struct unsock* c=un_get(s->q[k]); if(c){ c->peer=-1; c->state=5; } }
    if(s->rx)kfree(s->rx); s->rx=0; if(s->fq)kfree(s->fq); s->fq=0; s->used=0; }
static int un_bind(int i,const u8* key,int kl){ struct unsock* s=un_get(i); if(!s)return -9; if(s->state!=1)return -22; if(kl<=0||kl>108)return -22;
    for(int j=0;j<UN_MAX;j++) if(g_un[j].used&&(g_un[j].state==2||g_un[j].state==3)&&un_keyeq(&g_un[j],key,kl))return -98;
    for(int k=0;k<kl;k++)s->key[k]=key[k]; s->klen=kl; s->state=2; return 0; }
static int un_listen(int i,int backlog){ struct unsock* s=un_get(i); if(!s)return -9; if(s->state==3){ s->backlog=backlog<1?1:backlog>16?16:backlog; return 0; } if(s->state!=2)return -22; s->state=3; s->qn=0; s->backlog=backlog<1?1:backlog>16?16:backlog; return 0; }
static int un_connect(int i,const u8* key,int kl){ struct unsock* s=un_get(i); if(!s)return -9; if(s->state==4)return -106; if(s->state!=1&&s->state!=2)return -22;
    int L=-1; for(int j=0;j<UN_MAX;j++) if(g_un[j].used&&g_un[j].state==3&&un_keyeq(&g_un[j],key,kl)){ L=j; break; }
    if(L<0)return -111; struct unsock* lsn=&g_un[L]; if(lsn->qn>=lsn->backlog)return JS_BLOCK;
    int sv=un_new(); if(sv<0)return sv; struct unsock* gv=&g_un[sv]; gv->state=4; gv->peer=i; gv->klen=lsn->klen; for(int k=0;k<lsn->klen;k++)gv->key[k]=lsn->key[k];
    gv->seq=lsn->seq; gv->passcred=lsn->passcred; gv->cpid=lsn->cpid; s->seq=lsn->seq; s->peer=sv; s->state=4; lsn->q[lsn->qn++]=sv; return 0; }
static int un_accept(int i){ struct unsock* s=un_get(i); if(!s)return -9; if(s->state!=3)return -22; if(!s->qn)return JS_BLOCK;
    int c=s->q[0]; for(int k=1;k<s->qn;k++)s->q[k-1]=s->q[k]; s->qn--; return c; }
static int un_send(int i,const u8* b,int n,const int* vf,int nfds){ struct unsock* s=un_get(i); if(!s)return -9; if(s->state!=4||s->wr_shut)return -32;
    struct unsock* pr=un_get(s->peer); if(!pr||pr->rd_shut)return -32;
    int room=UN_CAP-pr->rxn;
    if(nfds>0&&pr->fqt+1-pr->fqh>UN_FQ)return JS_BLOCK;
    if(s->seq){ if(n>UN_CAP)return -90; if(room<n||pr->rlt-pr->rlh>=64)return JS_BLOCK; pr->rl[pr->rlt%64]=n; pr->rlt++; }
    else if(room<=0)return n?JS_BLOCK:0; int c=n<room?n:room;
    if(nfds>0&&pr->fqt+1-pr->fqh<=UN_FQ){ struct fbatch* fb=&pr->fq[pr->fqt%UN_FQ]; fb->at=pr->rcons+(u64)pr->rxn; fb->nf=nfds>64?64:nfds; for(int k=0;k<fb->nf;k++){ fb->vf[k]=vf[k]; vf_ref(vf[k]); } pr->fqt++; }
    for(int k=0;k<c;k++)pr->rx[pr->rxn+k]=b[k]; pr->rxn+=c; return c; }
static void un_shift(struct unsock* s,int c){ int left=s->rxn-c; u8* d=s->rx; const u8* q=s->rx+c; int k=0;
    if(c>=8){ for(;k+8<=left;k+=8)*(u64*)(d+k)=*(const u64*)(q+k); }
    for(;k<left;k++)d[k]=q[k]; s->rxn=left; s->rcons+=(u64)c; }
static int un_recv(int i,u8* b,int n,int peek,int* ovf,int maxf,int* onf){ struct unsock* s=un_get(i); if(!s)return -9; if(onf)*onf=0;
    if(s->seq?s->rlh==s->rlt:s->rxn==0){ struct unsock* pr=un_get(s->peer); if(s->rd_shut||s->peer<0||!pr)return 0; return (n||s->seq)?JS_BLOCK:0; }
    int rec=s->seq?s->rl[s->rlh%64]:s->rxn; int c=rec<n?rec:n;
    for(int k=0;k<c;k++)b[k]=s->rx[k];
    if(s->seq&&!peek){ if(ovf&&onf){ while(s->fqh!=s->fqt){ struct fbatch* fb=&s->fq[s->fqh%UN_FQ]; if(fb->at>=s->rcons+(u64)(rec?rec:1))break; if(*onf+fb->nf>maxf)break; for(int k=0;k<fb->nf;k++)ovf[(*onf)++]=fb->vf[k]; s->fqh++; } }
        un_shift(s,rec); s->rlh++; return c; }
    if(!peek){ if(ovf&&onf){ while(s->fqh!=s->fqt){ struct fbatch* fb=&s->fq[s->fqh%UN_FQ]; if(fb->at>=s->rcons+(u64)c)break; if(*onf+fb->nf>maxf)break; for(int k=0;k<fb->nf;k++)ovf[(*onf)++]=fb->vf[k]; s->fqh++; } }
        un_shift(s,c); }
    return c; }
static int un_poll(int i){ struct unsock* s=un_get(i); if(!s)return 0x20; int re=0;
    if(s->state==3)return s->qn?1:0;
    struct unsock* pr=un_get(s->peer);
    if(s->rxn>0||(s->seq&&s->rlh!=s->rlt))re|=1; if(s->rd_shut||s->peer<0||!pr)re|=1|0x10;
    if(pr&&!pr->rd_shut&&UN_CAP-pr->rxn>0)re|=4; else if(!pr)re|=0x10;
    return re; }
static int mfd_new(void){ for(int i=0;i<MFD_MAX;i++) if(!g_mfd[i].used){ u8* z=(u8*)&g_mfd[i]; for(int k=0;k<(int)sizeof(struct memfd);k++)z[k]=0; g_mfd[i].used=1; g_mfd[i].ref=1; return i; } { static int nf; if(nf<4){ nf++; klog("lx: memfd table full"); } } return -24; }
static long mfd_truncate(int i,u64 nsz){ if(i<0||i>=MFD_MAX||!g_mfd[i].used)return -9; struct memfd* m=&g_mfd[i]; if(nsz>0x40000000ULL)return -LX_EINVAL;
    u64 np=(nsz+4095)>>12;
    if((int)np>m->ncap){ u64* nf=(u64*)krealloc(m->frames,np*8); if(!nf)return -12; m->frames=nf; m->ncap=(int)np; }
    while((u64)m->nframes<np){ u64 f=pframe_alloc(); if(!f)return -12; m->frames[m->nframes++]=f; }
    while((u64)m->nframes>np)pframe_free(m->frames[--m->nframes]);
    m->size=nsz; return 0; }
void mfd_close(int i){ if(i<0||i>=MFD_MAX||!g_mfd[i].used)return; if(--g_mfd[i].ref>0)return; struct memfd* m=&g_mfd[i]; for(int k=0;k<m->nframes;k++)pframe_free(m->frames[k]); if(m->frames)kfree(m->frames); m->frames=0; m->used=0; }
static long mfd_io(struct vfile* v,u64 buf,u64 len,long off,int wr){
    if(v->fs_idx<0||v->fs_idx>=MFD_MAX||!g_mfd[v->fs_idx].used)return -LX_EBADF; struct memfd* m=&g_mfd[v->fs_idx]; u64 o=off<0?(u64)v->off:(u64)off;
    if(wr){ if(m->seals&8)return -LX_EPERM; if(o+len>m->size){ if(m->seals&4)return -LX_EPERM; long e=mfd_truncate(v->fs_idx,o+len); if(e)return e; } }
    else { if(o>=m->size)return 0; if(len>m->size-o)len=m->size-o; }
    u64 done=0; while(done<len){ u64 pg=(o+done)>>12, po=(o+done)&4095, n=4096-po; if(n>len-done)n=len-done; u8* f=(u8*)m->frames[pg]+po;
        if(wr?lx_get(f,buf+done,n):lx_put(buf+done,f,n))return done?(long)done:-LX_EFAULT; done+=n; }
    if(off<0)v->off=(int)(o+done); return (long)done; }
#define LX_EVFD 20
#define LX_TMFD 21
#define EV_MAX 128
#define TM_MAX 64
struct evfd { int used,sema; u64 cnt; };
static struct evfd g_ev[EV_MAX];
struct tmfd { int used,clk; u64 next,ival,exp; };
static struct tmfd g_tm[TM_MAX];
static u64 lx_now_ns(void);
static u64 tm_now(int clk){ return (clk==0||clk==8)?lx_now_ns():hr_ns(); }
static void tm_update(struct tmfd* t){ if(!t->next)return; u64 now=tm_now(t->clk); if(now<t->next)return;
    if(t->ival){ u64 n=1+(now-t->next)/t->ival; t->exp+=n; t->next+=n*t->ival; } else { t->exp+=1; t->next=0; } }
void evtm_close(int dev,int i){ if(dev==LX_EVFD&&i>=0&&i<EV_MAX)g_ev[i].used=0; if(dev==LX_TMFD&&i>=0&&i<TM_MAX)g_tm[i].used=0; }
static long lx_eventfd(u64 initval,int flags){ int i=-1; for(int k=0;k<EV_MAX;k++) if(!g_ev[k].used){ i=k; break; } if(i<0)return -LX_EMFILE;
    g_ev[i].used=1; g_ev[i].cnt=(u32)initval; g_ev[i].sema=(flags&1)!=0;
    int vi=jc_newvf(LX_EVFD,i,O_RDWR|(flags&0x800)); if(vi<0){ g_ev[i].used=0; return -LX_EMFILE; } int fd=lx_fd_install(vi,(flags&0x80000)!=0,0); if(fd<0)vf_unref(vi); return fd; }
static int ev_read(int i,u64 buf,u64 len){ if(len<8)return -LX_EINVAL; struct evfd* e=&g_ev[i]; if(!e->cnt)return JS_BLOCK; u64 v=e->sema?1:e->cnt; e->cnt-=v; if(lx_put(buf,&v,8))return -LX_EFAULT; return 8; }
static int ev_write(int i,u64 buf,u64 len){ if(len<8)return -LX_EINVAL; u64 v; if(lx_get(&v,buf,8))return -LX_EFAULT; if(v==~0ULL)return -LX_EINVAL; struct evfd* e=&g_ev[i]; if(e->cnt+v<e->cnt||e->cnt+v>0xFFFFFFFFFFFFFFFEULL)return JS_BLOCK; e->cnt+=v; return 8; }
static long lx_timerfd_create(int clk,int flags){ if(clk!=0&&clk!=1&&clk!=7&&clk!=8&&clk!=9)return -LX_EINVAL; int i=-1; for(int k=0;k<TM_MAX;k++) if(!g_tm[k].used){ i=k; break; } if(i<0)return -LX_EMFILE;
    g_tm[i].used=1; g_tm[i].clk=clk; g_tm[i].next=0; g_tm[i].ival=0; g_tm[i].exp=0;
    int vi=jc_newvf(LX_TMFD,i,O_RDWR|(flags&0x800)); if(vi<0){ g_tm[i].used=0; return -LX_EMFILE; } int fd=lx_fd_install(vi,(flags&0x80000)!=0,0); if(fd<0)vf_unref(vi); return fd; }
static long lx_timerfd_get(struct tmfd* t,u64 out){ tm_update(t); u64 o[4]={0,0,0,0}; o[0]=t->ival/1000000000ULL; o[1]=t->ival%1000000000ULL; if(t->next){ u64 now=tm_now(t->clk); u64 r=t->next>now?t->next-now:1; o[2]=r/1000000000ULL; o[3]=r%1000000000ULL; } return lx_put(out,o,32); }
static long lx_timerfd_set(int fd,int flags,u64 nv,u64 ov){ struct vfile* v=lx_vf(fd); if(!v)return -LX_EBADF; if(v->dev!=LX_TMFD)return -LX_EINVAL; struct tmfd* t=&g_tm[v->fs_idx];
    if(ov){ long e=lx_timerfd_get(t,ov); if(e)return e; }
    u64 s[4]; if(lx_get(s,nv,32))return -LX_EFAULT; if(s[1]>=1000000000ULL||s[3]>=1000000000ULL)return -LX_EINVAL;
    u64 val=s[2]*1000000000ULL+s[3], ival=s[0]*1000000000ULL+s[1];
    t->exp=0; t->ival=ival; if(!val){ t->next=0; return 0; }
    u64 now=tm_now(t->clk); t->next=(flags&1)?(val>now?val:now):now+val; if(!t->next)t->next=1; return 0; }
static int tm_read(int i,u64 buf,u64 len){ if(len<8)return -LX_EINVAL; struct tmfd* t=&g_tm[i]; tm_update(t); if(!t->exp)return JS_BLOCK; u64 v=t->exp; t->exp=0; if(lx_put(buf,&v,8))return -LX_EFAULT; return 8; }
#define LX_INOFD 22
#define LX_SIGFD 23
#define INO_MAX 16
#define INO_W 64
#define INO_PL 160
#define INO_Q 8192
struct inofd { int used, nw, nextwd, qn, ovf; int wd[INO_W]; u32 mask[INO_W]; char path[INO_W][INO_PL]; u8 q[INO_Q]; u32 lmask; int lwd; char lname[64]; };
static struct inofd g_ino[INO_MAX];
static int g_ino_any;
static u32 g_ino_cookie;
static int ino_eq(const char* a,const char* b){ int i=0; while(a[i]&&a[i]==b[i])i++; return a[i]==b[i]; }
static void ino_push(struct inofd* f,int wd,u32 mask,u32 cookie,const char* name){
    int nl=name?lx_slen(name):0; u32 len=nl?(u32)((nl+1+3)&~3):0;
    if(f->qn>0&&f->lwd==wd&&f->lmask==mask&&mask==2u&&ino_eq(f->lname,name?name:""))return;
    if(f->qn+16+(int)len>INO_Q){ if(!f->ovf&&f->qn+16<=INO_Q){ u32 h[4]={(u32)-1,0x4000u,0,0}; for(int k=0;k<16;k++)f->q[f->qn+k]=((u8*)h)[k]; f->qn+=16; } f->ovf=1; return; }
    u32 h[4]={(u32)wd,mask,cookie,len}; for(int k=0;k<16;k++)f->q[f->qn+k]=((u8*)h)[k];
    for(u32 k=0;k<len;k++)f->q[f->qn+16+k]=(int)k<nl?(u8)name[k]:0;
    f->qn+=16+(int)len; f->lwd=wd; f->lmask=mask; int k=0; if(name)for(;name[k]&&k<63;k++)f->lname[k]=name[k]; f->lname[k]=0;
}
static void ino_ev(const char* path,u32 mask,u32 cookie,int isdir){
    if(!g_ino_any||!path||!path[0])return;
    int L=lx_slen(path); while(L>1&&path[L-1]=='/')L--;
    int s=L; while(s>0&&path[s-1]!='/')s--;
    char dir[INO_PL]; int dl=s>1?s-1:1; if(dl>=INO_PL)return; for(int i=0;i<dl;i++)dir[i]=path[i]; dir[dl]=0;
    char name[64]; int nl=L-s; if(nl>63)nl=63; for(int i=0;i<nl;i++)name[i]=path[s+i]; name[nl]=0;
    char self[INO_PL]; if(L>=INO_PL)L=INO_PL-1; for(int i=0;i<L;i++)self[i]=path[i]; self[L]=0;
    u32 dm=isdir?0x40000000u:0;
    for(int i=0;i<INO_MAX;i++){ struct inofd* f=&g_ino[i]; if(!f->used)continue;
        for(int w=0;w<f->nw;w++){
            if(nl&&ino_eq(f->path[w],dir)&&(f->mask[w]&mask))ino_push(f,f->wd[w],mask|dm,cookie,name);
            if(ino_eq(f->path[w],self)){ u32 sm=mask==0x200u?0x400u:mask==0x40u?0x800u:(mask==0x100u||mask==0x80u)?0:mask; if(sm&&(f->mask[w]&sm))ino_push(f,f->wd[w],sm|dm,0,0);
                if(sm==0x400u){ ino_push(f,f->wd[w],0x8000u,0,0); f->nw--; f->wd[w]=f->wd[f->nw]; f->mask[w]=f->mask[f->nw]; for(int k=0;k<INO_PL;k++)f->path[w][k]=f->path[f->nw][k]; w--; } } } }
}
