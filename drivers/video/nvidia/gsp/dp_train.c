#include "include/nvx/drivers/video/dp_link.h"
#include "include/nvx/drivers/video/nvidia/nvsettings.h"
typedef struct { u32 hd, id; int busy; } nvdp_ctx;
static int nvdp_aux(nvdp_ctx* x,int rdq,u32 a,u8* b,int n){
    if(n<1||n>16)return -1;
    for(int t=0;t<4;t++){ u8 p[48]; for(int i=0;i<48;i++)p[i]=0; *(u32*)(p+4)=x->id; *(u32*)(p+12)=8u|(rdq?1u:0u); *(u32*)(p+16)=a; if(!rdq)for(int i=0;i<n;i++)p[20+i]=b[i]; *(u32*)(p+36)=(u32)(n-1);
        int r=rm_ctrl(HC(2),x->hd,0x731341u,p,48);
        if(r==-3){ x->busy++; hr_delay_us(10000); continue; }
        if(r)return -1; u32 rt=*(u32*)(p+40);
        if(rt==2||rt==8){ x->busy++; hr_delay_us(1000); continue; }
        if(rt)return -1; if(rdq)for(int i=0;i<n;i++)b[i]=p[20+i]; return n; }
    return -1; }
static int nvdp_rd(void* c,u32 a,u8* b,int n){ int d=0; while(d<n){ int k=n-d>16?16:n-d; if(nvdp_aux(c,1,a+(u32)d,b+d,k)!=k)return -1; d+=k; } return n; }
static int nvdp_wr(void* c,u32 a,const u8* b,int n){ int d=0; while(d<n){ int k=n-d>16?16:n-d; if(nvdp_aux(c,0,a+(u32)d,(u8*)b+d,k)!=k)return -1; d+=k; } return n; }
static int nvdp_drive(void* c,int lanes,const u8* vs,const u8* pe){ nvdp_ctx* x=c; u32 p[11]; for(int i=0;i<11;i++)p[i]=0; p[1]=x->id; p[2]=(u32)lanes; for(int i=0;i<lanes&&i<4;i++)p[3+i]=(u32)(pe[i]&3)|((u32)(vs[i]&3)<<2); return rm_ctrl(HC(2),x->hd,0x731346u,p,44)==0; }
static int nvdp_hw(void* c,int lanes,int bw,int enh){ nvdp_ctx* x=c;
    for(int t=0;t<3;t++){ u32 p[7]={0,x->id,1u|2u|(1u<<13)|(enh?(1u<<7):0u),(u32)lanes|((u32)bw<<8),0,0,0};
        int r=rm_ctrl(HC(2),x->hd,0x731343u,p,28);
        if(r==-3){ x->busy++; hr_delay_us(20000); continue; }
        if(r)return 0; return p[4]?0:1; }
    return 0; }
static void nvdp_delay(void* c,u32 us){ (void)c; hr_delay_us(us>20000?20000:us); }
static const dp_link_ops g_nvdp_ops={nvdp_rd,nvdp_wr,0,nvdp_drive,nvdp_hw,nvdp_delay};
static int nvdp_src_bw(u32 hd){ u8 p[52]; for(int i=0;i<52;i++)p[i]=0; *(u32*)(p+4)=0xFFFFFFFFu; if(rm_ctrl(HC(2),hd,0x731369u,p,52))return DP_BW_HBR2; switch(*(u32*)(p+8)&7){ case 1: return DP_BW_RBR; case 2: return DP_BW_HBR; case 3: return DP_BW_HBR2; case 4: return DP_BW_HBR3; } return DP_BW_HBR2; }
int nvgsp_dp(int train){
    nvl_s(train?"[21] DisplayPort link training through GSP-RM (NV0073 DP_AUXCH_CTRL, DP_CTRL, like nouveau r535)":"[21] DisplayPort sinks through GSP-RM: DPCD over the AUX channel (read only)"); nvl_nl();
    if(G.state<7){ nvl_s("  the NVIDIA card is not running - run nvgsp all first"); nvl_nl(); return 0; }
    if(g_mon_n<0&&!mon_scan())return 0;
    int keep=g_kd_live&&g_kd_live_gen==g_gsp_boots; u32 hd=keep?KD.hd:0x00730000u; int ok=0, seen=0;
    if(!keep){ if(!client_dev(2)){ rm_free(HC(2),HC(2)); nvl_s("  could not create the display client"); nvl_nl(); return 0; }
        if(rm_alloc(HC(2),HDEV,hd,0x0073,0,0)){ nvl_s("  GSP-RM refused NV04_DISPLAY_COMMON"); nvl_nl(); rm_free(HC(2),HC(2)); return 0; } }
    int sbw=nvdp_src_bw(hd); nvl_kv("source max link rate (DPCD units)",(u64)sbw);
    for(int i=0;i<g_mon_n;i++){ nvgsp_mon_t* m=&g_mon[i]; if(m->or_type!=2||(m->proto!=8&&m->proto!=9)||!m->connected)continue; seen++;
        nvdp_ctx x={hd,m->id,0}; dp_link_t l; u8* z=(u8*)&l; for(u32 k=0;k<sizeof(l);k++)z[k]=0; l.o=&g_nvdp_ops; l.c=&x; l.src_bw=sbw; l.src_lanes=4;
        nvl_s("  output "); nvl_x(m->id); nvl_s(": ");
        if(!dp_link_caps(&l)){ nvl_s(l.why); nvl_nl(); continue; }
        nvl_s("DPCD "); nvl_d(l.dpcd[0]>>4); nvl_s("."); nvl_d(l.dpcd[0]&15); nvl_s(", sink max "); nvl_d((u64)l.dpcd[1]*27/10); nvl_s("00 Mbit/s x"); nvl_d(l.dpcd[2]&31); nvl_s(l.enh?", enhanced framing":""); nvl_s(l.tps3?", TPS3":"");
        if(train){ int r=dp_link_train(&l); nvl_s(r?" - trained ":" - training failed: "); if(r){ nvl_d((u64)l.bw*27/10); nvl_s("00 Mbit/s x"); nvl_d((u64)l.lanes); } else nvl_s(l.why); nvl_s(" ("); nvl_d((u64)l.tries); nvl_s(" tries)"); if(r)ok++; }
        else ok++;
        nvl_nl(); }
    if(!seen){ nvl_s("  no connected DisplayPort output"); nvl_nl(); }
    if(!keep){ rm_free(HC(2),hd); rm_free(HC(2),HC(2)); }
    return ok>0; }
int nvgsp_dp_present(void){ if(G.state<7||(g_mon_n<0&&!mon_scan()))return 0; for(int i=0;i<g_mon_n;i++){ nvgsp_mon_t* m=&g_mon[i]; if(m->or_type==2&&(m->proto==8||m->proto==9)&&m->connected)return 1; } return 0; }
static struct { int on, fails, fw; u32 gen, id; u64 next; nvdp_ctx x; dp_link_t l; } NDP[4];
static u32 g_nvdp_hpd_events, g_nvdp_retrains;
static u32 g_nvdp_sst_ovr, g_nvdp_sst_auto; static nvdp_sst_t g_nvdp_sst[4];
static void nvdp_stream_prm(u8* p,int h,int or,int link,int enh,const nvdp_sst_t* s){ for(int i=0;i<116;i++)p[i]=0; *(u32*)(p+4)=(u32)h; *(u32*)(p+8)=(u32)or; *(u32*)(p+12)=link==9?1u:0u; p[68]=(u8)(enh?1:0); *(u32*)(p+72)=64;
    if(s){ p[16]=1; *(u32*)(p+24)=s->hbs; *(u32*)(p+28)=s->vbs; *(u32*)(p+72)=s->tu; *(u32*)(p+76)=s->wm; } }
static int nvdp_stream(int h,int or,int link,int enh,const kmode* md,const dp_link_t* l){
    u8 p[116]; nvdp_sst_t s; int have=md&&l&&h>=0&&h<4&&nvdp_sst_calc(md->khz,md->hact,(u32)md->hact+md->hfp+md->hsw+md->hbp,8,(u32)l->bw,(u32)l->lanes,enh,0,&s);
    if(have){ nvdp_stream_prm(p,h,or,link,enh,&s); if(rm_ctrl(HC(2),KD.hd,0x731362u,p,116)==0){ g_nvdp_sst[h]=s; g_nvdp_sst_ovr++; return 1; }
        nvl_s("  GSP-RM refused the computed DisplayPort stream values - letting it compute them"); nvl_nl(); }
    if(h>=0&&h<4){ g_nvdp_sst[h].wm=0; g_nvdp_sst[h].hbs=0; g_nvdp_sst[h].vbs=0; g_nvdp_sst[h].tu=0; }
    nvdp_stream_prm(p,h,or,link,enh,0); if(rm_ctrl(HC(2),KD.hd,0x731362u,p,116))return 0; g_nvdp_sst_auto++; return 1; }
static int nvdp_head_link(int h,int or,nvgsp_mon_t* m,const kmode* md,int byfw){
    if(h<0||h>3)return kd_fail("no display head for the DisplayPort monitor");
    if(!nvset_allowed(NVSET_DPTRAIN))return kd_fail("DisplayPort link training is off (gpu feature on dptrain)");
    NDP[h].on=0; NDP[h].fails=0; NDP[h].fw=byfw; NDP[h].id=m->id; NDP[h].x.hd=KD.hd; NDP[h].x.id=m->id; NDP[h].x.busy=0;
    dp_link_t* l=&NDP[h].l; u8* z=(u8*)l; for(u32 k=0;k<sizeof(*l);k++)z[k]=0; l->o=&g_nvdp_ops; l->c=&NDP[h].x; l->src_bw=nvdp_src_bw(KD.hd); l->src_lanes=4; l->need_kbs=(int)(md->khz*3u);
    int ok;
    if(byfw){ ok=dp_link_inherit(l); if(!ok&&l->losses){ nvl_s("  output "); nvl_x(m->id); nvl_s(": the firmware link was lost - training it again"); nvl_nl(); if(!nvset_try_begin(NVSET_DPTRAIN))return kd_fail("DisplayPort link training is off"); ok=dp_link_train(l); g_nvdp_retrains++; nvset_try_end(NVSET_DPTRAIN,ok,l->why?l->why:"training the lost firmware link again failed"); } }
    else { if(!nvset_try_begin(NVSET_DPTRAIN))return kd_fail("DisplayPort link training is off"); ok=dp_link_train(l); nvset_try_end(NVSET_DPTRAIN,ok,l->why?l->why:"link training failed"); }
    if(!ok)return kd_fail(l->why?l->why:"DisplayPort link training failed");
    if(!nvdp_stream(h,or,(int)m->proto,l->enh,md,l))return kd_fail("GSP-RM refused the DisplayPort stream setup (DP_CONFIG_STREAM)");
    nvev_ensure(); NDP[h].on=1; NDP[h].gen=g_gsp_boots; NDP[h].next=hr_ns()+1000000000ULL;
    nvl_s("  output "); nvl_x(m->id); nvl_s(l->kept?": DisplayPort link kept from the firmware, ":": DisplayPort link trained, "); nvl_d((u64)l->bw*27/10); nvl_s("00 Mbit/s x"); nvl_d((u64)l->lanes); nvl_nl();
    return 1; }
static void nvdp_head_gone(int h){ if(h>=0&&h<4)NDP[h].on=0; }
void nvdp_service(void){
    if(G.state<7||!KD.up||KD.gen!=g_gsp_boots)return; u64 now=hr_ns();
    if(g_nvev_kick){ g_nvev_kick=0; for(int h=0;h<4;h++)NDP[h].next=0; }
    for(int h=0;h<4;h++){ if(!NDP[h].on)continue; if(NDP[h].gen!=g_gsp_boots||KD.head[h].or<0||KD.head[h].id!=NDP[h].id){ NDP[h].on=0; continue; }
        if(now<NDP[h].next)continue; dp_link_t* l=&NDP[h].l; NDP[h].next=now+1000000000ULL;
        if(dp_link_status(l)>0){ NDP[h].fails=0; continue; }
        g_nvdp_hpd_events++; klog("nvgsp: DisplayPort link lost (hot-plug or link loss) - training it again");
        int ok=0; l->max_bw=0; if(!dp_link_caps(l)){ l->losses++; } else if(nvset_try_begin(NVSET_DPTRAIN)){ ok=dp_link_recover(l,DP_HPD_MAX_TRIES,20000); g_nvdp_retrains+=(u32)l->retrains; l->retrains=0; nvset_try_end(NVSET_DPTRAIN,ok,l->why?l->why:"retraining after link loss failed"); }
        if(ok){ NDP[h].fails=0; continue; }
        if(++NDP[h].fails<DP_HPD_MAX_TRIES){ NDP[h].next=now+(1000000000ULL<<NDP[h].fails); continue; }
        NDP[h].on=0; klog("nvgsp: DisplayPort link stays down - screen turned off");
        if(nvflip_head()==h)nvflip_off("the DisplayPort link was lost and could not be trained again");
        else if(!NDP[h].fw)kd_head_off(h); } }
int nvdp_info(int h,int* bw,int* lanes,int* kept,u32* events,u32* retrains){ if(events)*events=g_nvdp_hpd_events; if(retrains)*retrains=g_nvdp_retrains; if(h<0||h>3||!NDP[h].on)return 0; if(bw)*bw=NDP[h].l.bw; if(lanes)*lanes=NDP[h].l.lanes; if(kept)*kept=NDP[h].l.kept; return 1; }
int nvdp_sst_info(int h,u32* wm,u32* hbs,u32* vbs,u32* ovr,u32* aut){ if(ovr)*ovr=g_nvdp_sst_ovr; if(aut)*aut=g_nvdp_sst_auto; if(h<0||h>3||!g_nvdp_sst[h].tu)return 0; if(wm)*wm=g_nvdp_sst[h].wm; if(hbs)*hbs=g_nvdp_sst[h].hbs; if(vbs)*vbs=g_nvdp_sst[h].vbs; return 1; }
