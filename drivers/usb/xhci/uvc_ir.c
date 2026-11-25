typedef struct { u8 unit, sel; } uvc_xuc_t;
typedef struct { int n, havep, haved; long long prev, pdiff; u64 vsum, msum; } uvc_iran_t;
static int uvc_xu_list(const u8* c,int n,int vsif,uvc_xuc_t* out,int cap,int* vcif){
    int cif=-1,ccls=0,csub=0,curvc=-1,k=0;
    for(int i=0;i+2<=n;){ int bl=c[i]; if(bl<2||i+bl>n)break; int t=c[i+1];
        if(t==4&&bl>=9){ cif=c[i+2]; ccls=c[i+5]; csub=c[i+6];
            if(ccls==14&&csub==1&&c[i+3]==0&&cif!=curvc){ curvc=cif; k=0; }
            if(ccls==14&&csub==2&&cif==vsif){ if(vcif)*vcif=curvc; return curvc>=0?k:0; } }
        else if(t==0x24&&ccls==14&&csub==1&&cif==curvc&&bl>=24&&c[i+2]==6){ int unit=c[i+3], p=c[i+21];
            if(22+p<bl){ int csz=c[i+22+p]; if(csz>4)csz=4; for(int b=0;b<csz*8&&23+p+b/8<bl;b++) if(c[i+23+p+b/8]&(1<<(b&7))){ if(k<cap){ out[k].unit=(u8)unit; out[k].sel=(u8)(b+1); } k++; } } }
        i+=bl; }
    return 0; }
static void uvc_iran_reset(uvc_iran_t* a){ a->n=0; a->havep=0; a->haved=0; a->prev=0; a->pdiff=0; a->vsum=0; a->msum=0; }
static long long uvc_ir_sum(const u32* rgb,int w,int h){ long long s=0; for(int y=0;y<h;y+=4) for(int x=0;x<w;x+=4) s+=(long long)(rgb[(long)y*w+x]&0xFF); return s; }
static void uvc_iran_feed(uvc_iran_t* a,long long sum){
    if(a->havep){ long long d=sum-a->prev; if(a->haved){ long long v=d-a->pdiff; a->vsum+=(u64)(v<0?-v:v); } a->pdiff=d; a->haved=1; }
    a->prev=sum; a->havep=1; a->msum+=(u64)(sum<0?0:sum); a->n++; }
static int uvc_iran_works(const uvc_iran_t* base,const uvc_iran_t* t,u32 npx){
    if(base->n<3||t->n<3)return 0;
    u64 bv=base->vsum/(u64)base->n, tv=t->vsum/(u64)t->n, bm=base->msum/(u64)base->n, tm=t->msum/(u64)t->n;
    if(tv>bv*2u+(u64)npx*6u)return 1;
    if(tm>bm+bm/4u+(u64)npx*12u)return 1;
    return 0; }
static int uvc_xu_step(int init,int max){ int r=max-init; if(r<=0)return 1; return r/16+1; }
static int uvc_xu_next(u8* cur,const u8* init,const u8* max,int len,int* byte,int* tries){
    for(;;){ if(*byte>=len)return 0; int b=*byte, mx=max[b], v=cur[b], st=uvc_xu_step(init[b],mx);
        if(*tries>=16||v>=mx||v+st>255){ cur[b]=init[b]; *tries=0; (*byte)++; continue; }
        cur[b]=(u8)(v+st>mx?mx:v+st); (*tries)++; return 1; } }
enum { UIR_IDLE=0, UIR_BASE, UIR_TRY, UIR_ON, UIR_NONE, UIR_SAVED };
static struct { int st, nctl, ci, vcif, byte, tries, skip, ntrial, len, found, havemax; u8 cur[16], init[16], max[16]; uvc_xuc_t ctl[32]; uvc_iran_t base, t; u32 lastseq, npx; u64 t0; } uir;
static int uvc_irk=-1, uvc_irn=0; static u8* uvc_irkey=0; static u16 uvc_irv=0, uvc_irp=0;
int uvc_ir_stream(void){
    if(!cam.ok||!cam.desc)return -1;
    if(uvc_irkey==cam.desc&&uvc_irn==cam.descn&&uvc_irv==uvc_vid&&uvc_irp==uvc_pid)return uvc_irk;
    if(uvc_state==2)return -1;
    int cur=uvc_stream_cur(), r=-1;
    for(int k=0;k<cam.nvs&&r<0;k++){ uvc_choose(cam.desc,cam.descn,k); if(cam.nfr&&!uvc_has_color())r=k; }
    uvc_choose(cam.desc,cam.descn,cur);
    uvc_irkey=cam.desc; uvc_irn=cam.descn; uvc_irv=uvc_vid; uvc_irp=uvc_pid; uvc_irk=r;
    if(r>=0){ char b[80],*p=b; p=scopy(p,"camera: infrared stream "); p=udec(p,(u32)r+1); p=scopy(p," of "); p=udec(p,(u32)cam.nvs); *p=0; klog(b); }
    return r; }
static int uvc_xu_get(u8 req,int unit,int sel,u8* d,int len){ return uvc_req(0xA1,req,(u16)(sel<<8),(u16)((unit<<8)|uir.vcif),(u16)len,1,d); }
static int uvc_xu_set(int unit,int sel,u8* d,int len){ return uvc_req(0x21,0x01,(u16)(sel<<8),(u16)((unit<<8)|uir.vcif),(u16)len,0,d); }
static int uvc_ir_load_ctl(void){
    while(uir.ci<uir.nctl){ uvc_xuc_t* c=&uir.ctl[uir.ci]; u8 info[1]={0}, ln[2]={0,0};
        if(uvc_xu_get(0x86,c->unit,c->sel,info,1)&&(info[0]&3)==3&&uvc_xu_get(0x85,c->unit,c->sel,ln,2)){ int len=ln[0]|(ln[1]<<8);
            if(len>=1&&len<=16&&uvc_xu_get(0x81,c->unit,c->sel,uir.init,len)){
                uir.len=len; for(int k=0;k<len;k++){ uir.cur[k]=uir.init[k]; uir.max[k]=255; }
                uir.havemax=uvc_xu_get(0x83,c->unit,c->sel,uir.max,len); if(!uir.havemax)for(int k=0;k<len;k++)uir.max[k]=255;
                uir.byte=0; uir.tries=0; return 1; } }
        uir.ci++; }
    return 0; }
static void uvc_ir_restore(void){ if(uir.ci<uir.nctl&&uir.len)uvc_xu_set(uir.ctl[uir.ci].unit,uir.ctl[uir.ci].sel,uir.init,uir.len); }
static int uvc_ir_next_trial(void){
    for(int g=0;g<40;g++){ if(uir.ci>=uir.nctl)return 0;
        if(uvc_xu_next(uir.cur,uir.init,uir.max,uir.len,&uir.byte,&uir.tries)){ if(uvc_xu_set(uir.ctl[uir.ci].unit,uir.ctl[uir.ci].sel,uir.cur,uir.len))return 1; }
        uvc_ir_restore(); uir.ci++; uir.len=0; if(!uvc_ir_load_ctl())return 0; }
    return 0; }
int uvc_ir_emitter_apply(int unit,int sel,int len,const u8* v){
    if(!cam.ok||len<1||len>16)return 0; int vc=-1; uvc_xuc_t tmp[32]; uvc_xu_list(cam.desc,cam.descn,cam.vs_if,tmp,32,&vc); if(vc<0)return 0; uir.vcif=vc;
    u8 d[16]; for(int k=0;k<len;k++)d[k]=v[k]; if(!uvc_xu_set(unit,sel,d,len))return 0;
    uir.st=UIR_SAVED; uir.found=1; uir.ctl[0].unit=(u8)unit; uir.ctl[0].sel=(u8)sel; uir.ci=0; uir.nctl=1; uir.len=len; for(int k=0;k<len;k++)uir.cur[k]=d[k]; return 1; }
int uvc_ir_search_begin(void){
    uir.st=UIR_NONE; uir.found=0; uir.len=0; uir.ci=0; uir.ntrial=0; if(!cam.ok||!cam.desc)return 0;
    int vc=-1; int n=uvc_xu_list(cam.desc,cam.descn,cam.vs_if,uir.ctl,32,&vc); if(n>32)n=32; if(vc<0||n<=0){ klog("camera: the infrared camera has no extension controls for its light"); return 0; }
    uir.vcif=vc; uir.nctl=n; uvc_iran_reset(&uir.base); uir.st=UIR_BASE; uir.skip=3; uir.lastseq=uvc_seq; uir.t0=hr_ns();
    { char b[80],*p=b; p=scopy(p,"camera: looking for the infrared light among "); p=udec(p,(u32)n); p=scopy(p," controls"); *p=0; klog(b); }
    return 1; }
void uvc_ir_poll(void){
    if(uir.st!=UIR_BASE&&uir.st!=UIR_TRY)return;
    if(uvc_state!=2||!uvc_rgb){ if(uir.st==UIR_TRY)uvc_ir_restore(); uir.st=UIR_NONE; return; }
    if(hr_ns()-uir.t0>60000000000ULL||uir.ntrial>160){ if(uir.st==UIR_TRY)uvc_ir_restore(); uir.st=UIR_NONE; klog("camera: infrared light not found in time"); return; }
    if(uvc_seq==uir.lastseq)return; uir.lastseq=uvc_seq;
    if(uir.skip>0){ uir.skip--; return; }
    uir.npx=(u32)(((uvc_w+3)/4)*((uvc_h+3)/4)); long long s=uvc_ir_sum(uvc_rgb,uvc_w,uvc_h);
    if(uir.st==UIR_BASE){ uvc_iran_feed(&uir.base,s); if(uir.base.n<12)return;
        if(!uvc_ir_load_ctl()){ uir.st=UIR_NONE; klog("camera: no writable extension control on the infrared camera"); return; }
        if(!uvc_ir_next_trial()){ uir.st=UIR_NONE; return; }
        uir.st=UIR_TRY; uir.ntrial++; uvc_iran_reset(&uir.t); uir.skip=3; return; }
    uvc_iran_feed(&uir.t,s); if(uir.t.n<8)return;
    if(uvc_iran_works(&uir.base,&uir.t,uir.npx)){ uir.st=UIR_ON; uir.found=1;
        char b[96],*p=b; p=scopy(p,"camera: infrared light on, unit "); p=udec(p,uir.ctl[uir.ci].unit); p=scopy(p," control "); p=udec(p,uir.ctl[uir.ci].sel); *p=0; klog(b); return; }
    if(!uvc_ir_next_trial()){ uir.st=UIR_NONE; klog("camera: no extension control turned the infrared light on"); return; }
    uir.ntrial++; uvc_iran_reset(&uir.t); uir.skip=3; }
int uvc_ir_emitter(void){ return uir.st; }
int uvc_ir_emitter_ctl(int* unit,int* sel,int* len,u8* v){ if(!uir.found||uir.ci>=uir.nctl)return 0; *unit=uir.ctl[uir.ci].unit; *sel=uir.ctl[uir.ci].sel; *len=uir.len; for(int k=0;k<uir.len;k++)v[k]=uir.cur[k]; return 1; }
void uvc_ir_reset(void){ if(uir.st==UIR_TRY)uvc_ir_restore(); uir.st=UIR_IDLE; uir.found=0; uir.len=0; }
