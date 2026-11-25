#define NVEV_CLS 0x7Eu
#define NVEV_RMIDX 0x04000000u
#define NVEV_HPD 1u
#define NVEV_DPIRQ 7u
#define NVEV_H(i) (0xEE070000u|(i))
#define NVVB_RAW(h) (0x611800u+(u32)(h)*4u)
#define NVVB_BITS 0x2u
#define NVVB_RSVD 0xFFFFFFF8u
#define NVVB_BAD_MAX 4u
#define NVVB_MS_MAX 1000u
#define NVVB_MISS_MAX 8u
#define NVK_VRAM_RESERVE 0x2000000ULL
static struct { u32 gen, hd; int subs; u32 posts, hpd, dpirq, other, bad, rgline, plug, unplug; u32 vb[4], vbflips, pollflips, vbmiss, vbbad, vbraw; int vboff; char vbwhy[80]; } EV;
static volatile u32 g_nvev_kick, g_nvev_np, g_nvev_nu; static u32 g_nvev_conn, g_nvev_s1, g_nvev_s1lost; static int g_nvev_cset;

static u32 nvev_u32(const u8* p){ return (u32)p[0]|((u32)p[1]<<8)|((u32)p[2]<<16)|((u32)p[3]<<24); }
static int nvev_sub1(u32 id){
    u8 a[24]; for(int i=0;i<24;i++)a[i]=0; *(u32*)(a+0)=HC(2); *(u32*)(a+8)=NVEV_CLS; *(u32*)(a+12)=NVEV_RMIDX|id;
    if(rm_alloc(HC(2),HSUB,NVEV_H(id),NVEV_CLS,a,24))return 0;
    u8 c[20]; for(int i=0;i<20;i++)c[i]=0; *(u32*)(c+0)=id; *(u32*)(c+4)=2;
    if(rm_ctrl(HC(2),HSUB,0x20800301u,c,20)){ rm_free(HC(2),NVEV_H(id)); return 0; }
    return 1; }
int nvev_ensure(void){
    if(G.state<7||!KD.up||KD.gen!=g_gsp_boots)return 0;
    if(EV.subs&&EV.gen==g_gsp_boots&&EV.hd==KD.hd)return EV.subs;
    EV.gen=g_gsp_boots; EV.hd=KD.hd; EV.subs=0; g_nvev_cset=0; g_nvev_np=0; g_nvev_nu=0;
    if(nvev_sub1(NVEV_HPD))EV.subs|=1; if(nvev_sub1(NVEV_DPIRQ))EV.subs|=2;
    nvl_s(EV.subs?"  GSP-RM display events subscribed (hot-plug":"  GSP-RM refused the display event subscriptions - hot-plug stays polled"); if(EV.subs){ nvl_s(EV.subs&2?", DisplayPort IRQ)":")"); } nvl_nl();
    return EV.subs; }
static int nvev_post(const u8* p,u32 n){
    EV.posts++; if(n<32){ EV.bad++; return 0; }
    u32 he=nvev_u32(p+4), ds=nvev_u32(p+24); if(ds>n-29u){ EV.bad++; return 0; }
    if(he==NVEV_H(NVEV_HPD)){ EV.hpd++; if(ds>=8){ u32 pl=nvev_u32(p+29), up=nvev_u32(p+33); EV.plug|=pl; EV.unplug|=up; g_nvev_np|=pl; g_nvev_nu|=up; } g_nvev_kick=1; return 1; }
    if(he==NVEV_H(NVEV_DPIRQ)){ EV.dpirq++; g_nvev_kick=1; return 1; }
    EV.other++; return 1; }
int display_hpd(int present);
int display_hpd_s1(int present);
void display_sizes(u32* w,u32* h,u32* w2,u32* h2);
u32 nvev_s1_id(const nvgsp_mon_t* m,int n,u32 w,u32 h,u32 w2,u32 h2){
    if(!w2||!h2||(w==w2&&h==h2))return 0;
    u32 a=0; int na=0,nb=0;
    for(int i=0;i<n&&i<8;i++){ if(!m[i].connected||!m[i].lit)continue; if(m[i].act_w==w&&m[i].act_h==h){ a=m[i].id; na++; } else if(m[i].act_w==w2&&m[i].act_h==h2)nb++; }
    return na==1&&nb==1?a:0; }
void nvev_hpd_tick(void){
    u32 pl=g_nvev_np, up=g_nvev_nu; if(!(pl|up))return; g_nvev_np=0; g_nvev_nu=0;
    if(!g_nvev_cset){ if(g_mon_n<=0)return; g_nvev_conn=0; for(int i=0;i<g_mon_n&&i<8;i++)if(g_mon[i].connected)g_nvev_conn|=g_mon[i].id; g_nvev_cset=1;
        u32 w,h,w2,h2; display_sizes(&w,&h,&w2,&h2); g_nvev_s1=nvev_s1_id(g_mon,g_mon_n,w,h,w2,h2); }
    u32 c=(g_nvev_conn&~up)|pl, gone=g_nvev_conn&~c; int was=(g_nvev_conn&(g_nvev_conn-1))!=0, now=(c&(c-1))!=0; g_nvev_conn=c;
    if(was==now)return;
    if(!now&&g_nvev_s1&&(gone&g_nvev_s1)){ g_nvev_s1lost=1; nvl_s("  hot-plug: the screen-1 monitor was unplugged - the desktop is mirrored onto the monitor that is left"); nvl_nl(); display_hpd_s1(0); return; }
    if(now&&g_nvev_s1lost){ g_nvev_s1lost=0; nvl_s("  hot-plug: the screen-1 monitor is back - the extended desktop returns"); nvl_nl(); display_hpd_s1(1); return; }
    nvl_s(now?"  hot-plug: a second monitor is connected again - screen 2 comes back":"  hot-plug: only one monitor is left - windows on screen 2 move to screen 1"); nvl_nl();
    display_hpd(now); }
static int nvev_rgline(const u8* p,u32 n){ (void)p; (void)n; EV.rgline++; return 1; }

static int nvvb_usable(int h){ return h>=0&&h<4&&!EV.vboff; }
static void nvvb_off(const char* why,const char* lg){ EV.vboff=1; char* q=EV.vbwhy; q=scopy(q,why); *q=0; klog(lg); }
static int nvvb_take(int h){ u32 s=nvr32(NVVB_RAW(h)); EV.vbraw=s;
    if(s&NVVB_RSVD){ if(++EV.vbbad>=NVVB_BAD_MAX&&!EV.vboff)nvvb_off("the head status register returned invalid values - flips are polled","nvgsp: head status register at 0x611800 reads invalid values - page flips fall back to polling the notifier"); return 0; }
    if(!(s&NVVB_BITS))return 0;
    nvw32(NVVB_RAW(h),NVVB_BITS); EV.vb[h]++; return 1; }
static void nvvb_clear(int h){ if(nvvb_usable(h))(void)nvvb_take(h); }
static void nvvb_result(int seen){
    if(EV.vboff)return;
    if(seen){ EV.vbflips++; EV.vbmiss=0; return; }
    if(++EV.vbmiss<NVVB_MISS_MAX)return;
    nvvb_off("the head never reported a vertical blank - flips are polled","nvgsp: no vertical-blank status from the display head - page flips fall back to polling the notifier"); }
static int nvvb_wait(int h,u64 ntfy,u32 ms){
    if(ms>NVVB_MS_MAX)ms=NVVB_MS_MAX;
    u64 t=hr_ns()+(u64)ms*1000000ULL; int vb=nvvb_usable(h), seen=0; u32 us=50, it=0, itmax=ms*50u+64u;
    for(;;){ if(++it>itmax)return 0;
        if(vb&&!seen&&nvvb_take(h))seen=1;
        if(NV_DISP_NTFY_STATUS(nvp_rd32(ntfy))!=NV_DISP_NTFY_NOT_BEGUN){ if(vb)nvvb_result(seen); else EV.pollflips++; return 1; }
        if(hr_ns()>t)return 0;
        hr_delay_us(seen?20:us); if(us<400)us+=50; } }

static int nvk_dtd(const u8* d,kmode* m){
    u32 pk=(u32)(d[0]|(d[1]<<8)); if(!pk)return 0;
    u32 ha=(u32)(d[2]|((d[4]&0xF0)<<4)), hb=(u32)(d[3]|((d[4]&0x0F)<<8)), va=(u32)(d[5]|((d[7]&0xF0)<<4)), vb=(u32)(d[6]|((d[7]&0x0F)<<8));
    u32 ho=(u32)(d[8]|((d[11]&0xC0)<<2)), hw=(u32)(d[9]|((d[11]&0x30)<<4)), vo=(u32)((d[10]>>4)|((d[11]&0x0C)<<2)), vw=(u32)((d[10]&0x0F)|((d[11]&0x03)<<4));
    if(d[17]&0x80)return 0;
    if(ha<320||va<200||ha>8192||va>8192||!hw||!vw||ho+hw>=hb||vo+vw>=vb)return 0;
    m->khz=pk*10; m->hact=(u16)ha; m->hfp=(u16)ho; m->hsw=(u16)hw; m->hbp=(u16)(hb-ho-hw); m->vact=(u16)va; m->vfp=(u16)vo; m->vsw=(u16)vw; m->vbp=(u16)(vb-vo-vw);
    m->nhs=1; m->nvs=1; if((d[17]&0x18)==0x18){ m->nvs=(d[17]&0x04)?0:1; m->nhs=(d[17]&0x02)?0:1; }
    return 1; }
u64 nvk_mode_bytes(u32 w,u32 h,u32 nbuf){ u64 pitch=((u64)w*4u+255u)&~255ULL; return (((u64)pitch*h+0xFFFFULL)&~0xFFFFULL)*nbuf; }
int nvk_edid_pick(const u8* e,int n,u32 maxkhz,u64 budget,kmode* out){
    if(n<128)return 0; const u8* c[12]; int nc=0;
    for(int b=54;b<=108;b+=18)c[nc++]=e+b;
    if(n>=256&&e[126]&&e[128]==0x02){ int d=e[130]; if(d>=4&&d<=127) for(int o=128+d;o+18<=128+127&&nc<12;o+=18)c[nc++]=e+o; }
    int best=-1; kmode bm, m; u64 ba=0;
    for(int i=0;i<nc;i++){ if(!nvk_dtd(c[i],&m))continue;
        if(maxkhz&&m.khz>maxkhz)continue; if(nvk_mode_bytes(m.hact,m.vact,NVFLIP_NBUF)>budget)continue;
        u64 a=(u64)m.hact*m.vact; if(best<0||a>ba||(a==ba&&m.khz>bm.khz)){ best=i; bm=m; ba=a; } }
    if(best<0)return 0;
    for(int i=0;i<nc;i++){ if(!nvk_dtd(c[i],&m))continue; if((!maxkhz||m.khz<=maxkhz)&&nvk_mode_bytes(m.hact,m.vact,NVFLIP_NBUF)<=budget){ *out=m; return 1; } break; }
    *out=bm; return 2; }
static u64 nvk_vram_budget(void){
    u64 f=nvv_free_bytes(); f=f>NVK_VRAM_RESERVE?f-NVK_VRAM_RESERVE:0;
    if(G.bar1_len){ u64 lim=G.bar1_len>0x3000000ULL?G.bar1_len-0x2000000ULL:0; u64 nx=g_b1_gen==g_ce_gen?g_b1_next:((g_con_end+0x200000ULL+0x1FFFFFULL)&~0x1FFFFFULL); u64 r=lim>nx?lim-nx:0; if(r<f)f=r; }
    return f; }
int nvev_info(u32* posts,u32* hpd,u32* dpirq,u32* vbflips,u32* pollflips,int* vboff,const char** why){
    if(posts)*posts=EV.posts; if(hpd)*hpd=EV.hpd; if(dpirq)*dpirq=EV.dpirq; if(vbflips)*vbflips=EV.vbflips; if(pollflips)*pollflips=EV.pollflips; if(vboff)*vboff=EV.vboff; if(why)*why=EV.vbwhy; return EV.subs; }
