int g_nvflip_want=0, g_nvflip_live=0;
static struct { int head, win, cur, pend, mon, ce, rsz, nat, pn, pfull, cemiss; u32 mw, mh, pct, w, h, pitch, khz, gen, b1gen, flips, waits, timeouts, fallbacks, fww, fwh, ceframes, cpuframes, rframes, idle, done, cegen, ssw, sh, cechk; u64 vram[NVFLIP_NBUF], fwv, bva[NVFLIP_NBUF], sva; const u32* sptr; u32 fwpitch; volatile u32* cpu[NVFLIP_NBUF]; dmg_rect pd[NVFLIP_RMAX]; char why[112], cewhy[96]; } NF;
extern int fb_desktop_resize(u32 w,u32 h,int apply) __attribute__((weak));
extern u32 fb_desk_scale(void) __attribute__((weak));

static int nf_fail(const char* w){ char* q=NF.why; q=scopy(q,w); *q=0; nvl_s("  "); nvl_s(w); nvl_nl(); return 0; }
int nvflip_active(void){ if(!g_nvflip_live)return 0; if(NF.gen!=g_gsp_boots||!KD.up||KD.gen!=g_gsp_boots||G.state<7){ g_nvflip_live=0; return 0; } return 1; }
static void nf_lost(const char* why){ nvflip_off(why); nvset_try_end(NVSET_MODESET,0,why); }
static void nf_ce_off(const char* why){ if(!NF.ce)return; NF.ce=0; char* q=NF.cewhy; q=scopy(q,why); *q=0; char b[150]; q=b; q=scopy(q,"nvgsp: screen frames copied by the CPU - "); q=scopy(q,why); *q=0; klog(b); nvset_note(NVSET_MODESET,NVSET_ST_FALLBACK,b+7); }
int nvflip_native(u32* fww,u32* fwh){ if(fww)*fww=NF.fww; if(fwh)*fwh=NF.fwh; return nvflip_active()&&(NF.w!=NF.fww||NF.h!=NF.fwh); }
int nvflip_head(void){ return nvflip_active()?NF.head:-1; }

static int nf_bufs(u32 w,u32 h){
    u32 pitch=(w*4u+255u)&~255u; u64 len=((u64)pitch*h+0xFFFFULL)&~0xFFFFULL;
    if(NF.b1gen==g_ce_gen&&NF.vram[0]&&NF.pitch==pitch&&NF.h==h){ NF.w=w; return 1; }
    if(!G.bar1||!G.bar1_len)return nf_fail("no BAR1 window for the screen buffers");
    if(g_b1_gen!=g_ce_gen){ g_b1_gen=g_ce_gen; g_b1_next=(g_con_end+0x200000ULL+0x1FFFFFULL)&~0x1FFFFFULL; g_vs_bytes=0; g_vs_n=0; }
    u64 lim=G.bar1_len>0x3000000ULL?G.bar1_len-0x2000000ULL:0; if(g_b1_next+len*NVFLIP_NBUF>lim)return nf_fail("the BAR1 window has no room for two screen buffers");
    u64 cp=acpi_hi_map(G.bar1); if(!cp)return nf_fail("BAR1 cannot be mapped by the CPU");
    nvvmm b1; b1.pd3=G.bar1_pde; b1.limit=G.bar1_len; b1.ok=1; u64 v[NVFLIP_NBUF]; int n=0;
    for(;n<NVFLIP_NBUF;n++){ v[n]=nvv_alloc_raw(len,0x10000); if(!v[n]||(v[n]&0xFFu)||v[n]+len>KD.vtop)break;
        if(!nvm_map(&b1,g_b1_next+(u64)n*len,v[n],len,0,0,0)){ nvv_free(v[n]); break; } }
    if(n<NVFLIP_NBUF){ for(int i=0;i<n;i++)nvv_free(v[i]); return nf_fail("no VRAM for two screen buffers"); }
    nvm_tlb_flush(G.bar1_pde,1);
    for(int i=0;i<NVFLIP_NBUF;i++){ NF.vram[i]=v[i]; NF.cpu[i]=(volatile u32*)(cp+g_b1_next+(u64)i*len); for(u64 k=0;k<len/4;k++)NF.cpu[i][k]=0xFF000000u; }
    g_b1_next+=len*NVFLIP_NBUF; g_vs_bytes+=len*NVFLIP_NBUF; NF.bva[0]=0; NF.pitch=pitch; NF.w=w; NF.h=h; NF.b1gen=g_ce_gen; return 1; }

static int nf_wait_ntfy(u32 ms){ return nvvb_wait(NF.head,KD.sync+NVFLIP_NTFY_OFF,ms); }

static int nf_push_flip(u64 vram,int vsync){
    kchan* q=&KD.win[NF.win]; if(!q->ok||!kc_room(q,16))return 0;
    for(u32 i=0;i<16;i+=4)nvp_wr32(KD.sync+NVFLIP_NTFY_OFF+i,0);
    u32 n=nvflip_emit_flip(q->pb+q->cur,KD_PBMAX-q->cur,(u32)(vram>>8),1u<<NF.win,vsync,NVFLIP_HSYNC,NVFLIP_NTFY_OFF); if(!n)return 0;
    nvvb_clear(NF.head); q->cur+=n; kc_kick(q); return q->ok; }

int nvflip_wait(u32 ms){
    if(!nvflip_active())return 0; if(!NF.pend)return 1;
    NF.waits++; if(!nf_wait_ntfy(ms)){ NF.timeouts++; nf_lost("a page flip did not finish within the wait"); return 0; }
    NF.pend=0; NF.done++; return 1; }

int nvflip_flip(int vsync){
    if(!nvflip_active())return 0;
    if(NF.pend&&!nvflip_wait(100))return 0;
    int b=NF.cur^1; if(!nf_push_flip(NF.vram[b],vsync)){ nf_lost("the window channel did not take the page flip"); return 0; }
    NF.cur=b; NF.pend=1; NF.flips++; return 1; }

volatile u32* nvflip_back(u32* pitch_bytes){
    if(!nvflip_active())return 0; if(NF.pend&&!nvflip_wait(100))return 0;
    if(pitch_bytes)*pitch_bytes=NF.pitch; return NF.cpu[NF.cur^1]; }

static int nf_ce_bind(const u32* src,u32 sp,u32 h){
    if(!NF.ce||!nvgsp_ce_ready())return 0;
    if(NF.cegen!=g_ce_gen||!NF.bva[0]){ u64 len=(u64)NF.pitch*NF.h; for(int i=0;i<NVFLIP_NBUF;i++){ u64 va=va_take((len+0xFFFFULL)&~0xFFFFULL,0x10000); if(!va||!nvm_map(&g_uv,va,NF.vram[i],(len+0xFFFFULL)&~0xFFFFULL,0,0,0)){ nf_ce_off("the screen buffers could not be mapped for the copy engine"); return 0; } NF.bva[i]=va; }
        nvm_tlb_flush(g_uv.pd3,0); NF.cegen=g_ce_gen; NF.sptr=0; NF.sva=0; }
    if(NF.sptr!=src||NF.ssw!=sp||NF.sh<h){ if(NF.sva)nvgsp_ce_unmap(NF.sva,(u64)NF.ssw*NF.sh*4); NF.sptr=0; NF.sva=nvgsp_ce_map((u64)src,(u64)sp*h*4); if(!NF.sva){ nf_ce_off("the desktop picture could not be mapped for the copy engine"); return 0; } NF.sptr=src; NF.ssw=sp; NF.sh=h; }
    return 1; }
static void nf_cpu(volatile u32* d,const u32* src,u32 sp,const dmg_rect* r,int n){
    for(int i=0;i<n;i++)for(int y=0;y<r[i].h;y++){ volatile u32* o=d+(u64)(r[i].y+y)*(NF.pitch/4)+r[i].x; const u32* s=src+(u64)(r[i].y+y)*sp+r[i].x; for(int x=0;x<r[i].w;x++)o[x]=s[x]; } }
static void nf_copy(int b,const u32* src,u32 sp,const dmg_rect* r,int n){
    if(nf_ce_bind(src,sp,NF.h)){
        if(ce_rects(NF.sva,sp*4,NF.bva[b],NF.pitch,r,n)){ NF.cemiss=0; NF.cechk++;
            if(NF.cechk<=30||(NF.cechk&31)==0){ for(int i=0;i<n&&i<8;i++){ u32 x0=(u32)r[i].x, y0=(u32)r[i].y, x1=x0+(u32)r[i].w-1, y1=y0+(u32)r[i].h-1, xs[3]={x0,x1,(x0+x1)/2}, ys[3]={y0,y1,(y0+y1)/2};
                for(int k=0;k<3;k++) if(nvp_rd32(NF.vram[b]+(u64)ys[k]*NF.pitch+(u64)xs[k]*4)!=src[(u64)ys[k]*sp+xs[k]]){ nf_ce_off("the picture read back from the screen buffer did not match"); goto cpu; } } }
            NF.ceframes++; return; }
        if(++NF.cemiss>=3)nf_ce_off("the copy engine missed three frames"); }
    cpu: nf_cpu(NF.cpu[b],src,sp,r,n); NF.cpuframes++; }

int nvflip_present_rects(const u32* src,u32 sp,u32 w,u32 h,const dmg_rect* r,int n){
    if(!nvflip_active()||!src||sp<w)return 0;
    if(NF.rsz){ if(w==NF.w&&h==NF.h)NF.rsz=0;
        else if(w==NF.fww&&h==NF.fwh){ NF.rsz=0; if(!fb_desktop_resize||!fb_desktop_resize(NF.w,NF.h,1)){ nf_lost("the desktop could not switch to the native size"); return 0; } NF.nat=1; return 1; } }
    if(w!=NF.w||h!=NF.h){ nvflip_off("the desktop size changed"); return 0; }
    if(n==0){ NF.idle++; return 1; }
    if(NF.pend&&!nvflip_wait(100))return 0;
    dmg_rect a[NVFLIP_RMAX*2]; int m=0, bad=n<0||n>NVFLIP_RMAX||!r;
    for(int i=0;i<n&&!bad;i++){ if(r[i].x<0||r[i].y<0||r[i].w<=0||r[i].h<=0||(u32)(r[i].x+r[i].w)>w||(u32)(r[i].y+r[i].h)>h)bad=1; else a[m++]=r[i]; }
    int full=bad||NF.pfull; for(int i=0;i<NF.pn&&!full;i++)a[m++]=NF.pd[i];
    if(full){ a[0].x=0; a[0].y=0; a[0].w=(int)w; a[0].h=(int)h; m=1; } else NF.rframes++;
    nf_copy(NF.cur^1,src,sp,a,m);
    NF.pfull=bad; NF.pn=bad?0:n; for(int i=0;i<NF.pn;i++)NF.pd[i]=r[i];
    return nvflip_flip(1); }
int nvflip_present(const u32* src,u32 src_pitch_px,u32 w,u32 h){ return nvflip_present_rects(src,src_pitch_px,w,h,0,-1); }

static int nf_find(int* head,int* mi,int* sor,u32* fwsors){
    *head=-1; *mi=-1; *sor=-1; *fwsors=0; u32 hmask=g_mon_hmask?g_mon_hmask:0x3;
    for(int k=0;k<4;k++){ if(!(hmask&(1u<<k)))continue; u32 id=0;
        if(KD.head[k].or>=0){ if(!KD.head[k].fw)continue; id=KD.head[k].id; }
        else { u32 g[4]={0,(u32)k,0,0}; if(rm_ctrl(HC(2),KD.hd,0x730126u,g,16)||!g[3])continue; id=g[3]; }
        for(int i=0;i<g_mon_n&&i<8;i++){ if(!g_mon[i].connected||g_mon[i].id!=id)continue;
            u8 o[56]; for(int z=0;z<56;z++)o[z]=0; *(u32*)(o+4)=id;
            if(KD.head[k].or>=0)*sor=KD.head[k].or; else if(rm_ctrl(HC(2),KD.hd,0x73028Bu,o,56)==0&&*(u32*)(o+8)<4)*sor=(int)*(u32*)(o+8);
            if(*head<0){ *head=k; *mi=i; } else if(*sor>=0)*fwsors|=1u<<*sor; break; } }
    return *head>=0; }

int nvflip_on(void){
    NF.why[0]=0; nvl_s("[21] main screen through GSP-RM: native mode, two VRAM buffers, page flips on vertical blank"); nvl_nl();
    if(!g_nvflip_want)return nf_fail("gpu modeset is off");
    if(nvflip_active())return 1;
    if(G.state<7)return nf_fail("the NVIDIA card is not running - run nvgsp all first");
    if(G.chip>=0x170&&!nvgsp_gen_pci_ok(g_nv_dev))return nf_fail("RTX 30/40 support is off (gpu ampere on / gpu ada on) - the firmware picture stays");
    u64 fbpa=*(volatile u64*)(NV_BOOTINFO); u32 fpitch=*(volatile u32*)(NV_BOOTINFO+8); u32 fw=*(volatile u16*)(NV_BOOTINFO+12), fh=*(volatile u16*)(NV_BOOTINFO+14);
    if(!G.bar1||fbpa<G.bar1||fbpa>=G.bar1+G.bar1_len)return nf_fail("the desktop picture is not in NVIDIA video memory");
    if(!fw||!fh||fw>8192||fh>8192||(fpitch&63u)||fpitch<fw*4u||((fbpa-G.bar1)&0xFFu)||fbpa-G.bar1+(u64)fpitch*fh>G.bar1_len)return nf_fail("the firmware screen has a layout the display engine cannot scan out");
    if(!mon_scan())return nf_fail(g_mon_why[0]?g_mon_why:"the monitors could not be read");
    if(!kd_init())return nf_fail(KD.why[0]?KD.why:"the display channels did not start");
    nvgsp_mode_marks(); nvev_ensure();
    int h, mi, sor; u32 fws; if(!nf_find(&h,&mi,&sor,&fws))return nf_fail("no monitor shows the firmware picture on the NVIDIA card");
    nvgsp_mon_t* m=&g_mon[mi]; kmode md;
    int dp=m->or_type==2&&(m->proto==8||m->proto==9);
    if(dp&&!kd_dp_ok(m))return nf_fail("the main monitor is on DisplayPort and DisplayPort link training is off");
    if(m->or_type!=2||!(m->proto==1||m->proto==2||dp))return nf_fail("the main monitor is not on HDMI, single-link DVI or DisplayPort");
    if(!m->edid_ok||!nvk_edid_mode(g_mon_edid[mi],m->edid_len>256?256:m->edid_len,&md))return nf_fail("the main monitor has no usable native mode in its EDID");
    int hdmi=nvk_edid_hdmi(g_mon_edid[mi],m->edid_len>256?256:m->edid_len); if(dp)hdmi=0;
    { kmode pm; int pk=nvk_edid_pick(g_mon_edid[mi],m->edid_len>256?256:m->edid_len,dp?0:(hdmi?340000u:165000u),nvk_vram_budget(),&pm);
      if(!pk)return nf_fail(!dp&&md.khz>(hdmi?340000u:165000u)?"the native mode needs a pixel clock above what this link carries":"no EDID mode fits this link and the free video memory");
      if(pk==2){ nvl_s("  preferred mode "); nvl_d(md.hact); nvl_s("x"); nvl_d(md.vact); nvl_s(" does not fit the link or the free video memory - using "); nvl_d(pm.hact); nvl_s("x"); nvl_d(pm.vact); nvl_nl(); }
      md=pm; }
    int nat=0; u32 dw=fw, dh=fh, pct=0;
    { u32 ps[DESK_NPCT]; int np=fb_desktop_resize?desk_ladder(md.hact,md.vact,fb_desk_scale?fb_desk_scale():0,ps,DESK_NPCT):0;
      for(int i=0;i<np&&!nat;i++){ u32 w=0, h=0; if(!desk_dims(md.hact,md.vact,ps[i],&w,&h))continue; if(w==fw&&h==fh){ pct=ps[i]; break; }
          if(!fb_desktop_resize(w,h,0)){ nvl_s("  desktop "); nvl_d(w); nvl_s("x"); nvl_d(h); nvl_s(" needs more memory than is free - trying a larger scale"); nvl_nl(); continue; }
          if(nf_bufs(w,h)){ nat=1; dw=w; dh=h; pct=ps[i]; } else { NF.why[0]=0; nvl_s("  video memory too short for a "); nvl_d(w); nvl_s("x"); nvl_d(h); nvl_s(" desktop - trying a larger scale"); nvl_nl(); } }
      if(!nat&&np){ fb_desktop_resize(fw,fh,0); if(!pct){ nvl_s("  no native desktop size fits - the desktop stays at the firmware size, scaled"); nvl_nl(); } } }
    if(!nat&&!nf_bufs(fw,fh))return 0;
    NF.mw=md.hact; NF.mh=md.vact; NF.pct=pct;
    NF.fwv=fbpa-G.bar1; NF.fwpitch=fpitch; NF.fww=fw; NF.fwh=fh;
    { u32 cw=fw<NF.w?fw:NF.w, ch=fh<NF.h?fh:NF.h; for(u32 y=0;y<ch;y++){ volatile u32* s=(volatile u32*)(acpi_hi_map(G.bar1)+NF.fwv+(u64)y*fpitch); volatile u32* d=NF.cpu[0]+(u64)y*(NF.pitch/4); for(u32 x=0;x<cw;x++)d[x]=s[x]; } }
    u32 excl=(KD.sors|fws)&0xFu; if(sor>=0)excl&=~(1u<<sor);
    kd_src.on=1; kd_src.fbv=NF.vram[0]; kd_src.pitch=NF.pitch; kd_src.w=dw; kd_src.h=dh;
    int ok=kd_head_on(h,m,&md,hdmi,(int)m->proto,excl,1); kd_src.on=0;
    if(!ok){ NF.fallbacks++; return nf_fail(KD.why[0]?KD.why:"the mode set did not finish"); }
    NF.head=h; NF.win=KD.head[h].win; NF.cur=0; NF.pend=0; NF.mon=mi; NF.khz=md.khz; NF.gen=g_gsp_boots; NF.rsz=nat; NF.nat=0; NF.pn=0; NF.pfull=1; NF.cemiss=0; NF.cechk=0; NF.ce=1; NF.cewhy[0]=0; g_nvflip_live=1;
    nvl_s("  main screen on head "); nvl_d((u64)h); nvl_s(": native "); nvl_d(md.hact); nvl_s("x"); nvl_d(md.vact); nvl_s(", desktop "); nvl_d(dw); nvl_s("x"); nvl_d(dh); nvl_s(nat?" (switching from the firmware size)":""); nvl_s(" double-buffered in VRAM"); nvl_nl();
    return 1; }

void nvflip_off(const char* why){
    if(!g_nvflip_live)return; g_nvflip_live=0; NF.pend=0;
    char* q=NF.why; q=scopy(q,why?why:"turned off"); *q=0;
    if(NF.gen!=g_gsp_boots||!KD.up||KD.gen!=g_gsp_boots)return;
    kchan* w=&KD.win[NF.win]; int ok=1;
    if(NF.w!=NF.fww||NF.h!=NF.fwh){ kchan* c=&KD.core; u32 ow=0, oh=0; nvk_fit(NF.fww,NF.fwh,KD.head[NF.head].m.hact,KD.head[NF.head].m.vact,&ow,&oh); ok=0;
        if(kc_room(c,8)){ k1(c,NVC57D_HEAD_SET_VIEWPORT_IN(NF.head),NF.fww|(NF.fwh<<16)); k1(c,NVC57D_HEAD_SET_VIEWPORT_OUT(NF.head),ow|(oh<<16)); ok=c->ok&&kd_commit(0); if(ok){ KD.head[NF.head].ow=ow; KD.head[NF.head].oh=oh; } } }
    if(ok){ ok=0; if(w->ok&&kc_room(w,32)){ u32 n=nvflip_emit_image(w->pb+w->cur,KD_PBMAX-w->cur,NF.fww,NF.fwh,NF.fwpitch,KD_HFB,(u32)(NF.fwv>>8)); if(n){ w->cur+=n; ok=nf_push_flip(NF.fwv,1)&&nf_wait_ntfy(200); } } }
    if(!ok){ NF.fallbacks++; kd_head_off(NF.head); } }

int nvflip_setting(int on){
    g_nvflip_want=on?1:0;
    { extern void nvd_flip_remember(int) __attribute__((weak)); if(nvd_flip_remember)nvd_flip_remember(g_nvflip_want); }
    if(!on){ nvflip_off("gpu modeset off"); return 1; }
    return nvflip_on(); }

int nvflip_info(nvflip_info_t* o){
    u8* z=(u8*)o; for(u32 i=0;i<sizeof(*o);i++)z[i]=0; o->up=nvflip_active(); o->why=NF.why; o->flips=NF.flips; o->waits=NF.waits; o->timeouts=NF.timeouts; o->fallbacks=NF.fallbacks;
    o->ce=NF.ce; o->native=NF.nat; o->fww=NF.fww; o->fwh=NF.fwh; o->ceframes=NF.ceframes; o->cpuframes=NF.cpuframes; o->rframes=NF.rframes; o->idle=NF.idle; o->done=NF.done; o->cewhy=NF.cewhy; o->mw=NF.mw; o->mh=NF.mh; o->scale=NF.pct;
    if(!o->up)return 0; o->head=NF.head; o->win=NF.win; o->sor=KD.head[NF.head].or; o->cur=NF.cur; o->pend=NF.pend; o->w=NF.w; o->h=NF.h; o->pitch=NF.pitch; o->khz=NF.khz;
    for(int i=0;i<NVFLIP_NBUF;i++)o->vram[i]=NF.vram[i]; return 1; }

int nvflip_core_methods(const u32* mv,int npairs){
    if(!nvflip_active()||npairs<0)return 0; kchan* c=&KD.core; if(!kc_room(c,(u32)npairs*2u+4u))return 0;
    for(int i=0;i<npairs;i++)k1(c,mv[i*2],mv[i*2+1]); return c->ok; }
int nvflip_core_update(u32 win_mask){ if(!nvflip_active())return 0; return kd_commit(win_mask); }
char* nvflip_why(void){ return NF.why; }
int nvflip_cursor_buf(u32 len,u64* vram,volatile u32** cpu){
    static u64 v, n; static volatile u32* c; static u32 gen, bg;
    if(!nvflip_active()||!len||len>0x10000u||!G.bar1||!G.bar1_len)return 0;
    u64 L=0x10000ULL; if(v&&gen==g_ce_gen&&bg==g_gsp_boots&&n>=len){ *vram=v; *cpu=c; return 1; }
    if(g_b1_gen!=g_ce_gen)return 0;
    u64 lim=G.bar1_len>0x3000000ULL?G.bar1_len-0x2000000ULL:0; if(g_b1_next+L>lim)return 0;
    u64 cp=acpi_hi_map(G.bar1); if(!cp)return 0;
    u64 a=nvv_alloc_raw(L,0x10000); if(!a)return 0; if((a&0xFFu)||a+L>KD.vtop){ nvv_free(a); return 0; }
    nvvmm b1; b1.pd3=G.bar1_pde; b1.limit=G.bar1_len; b1.ok=1; if(!nvm_map(&b1,g_b1_next,a,L,0,0,0)){ nvv_free(a); return 0; }
    nvm_tlb_flush(G.bar1_pde,1);
    c=(volatile u32*)(cp+g_b1_next); for(u64 k=0;k<L/4;k++)c[k]=0;
    g_b1_next+=L; g_vs_bytes+=L; v=a; n=L; gen=g_ce_gen; bg=g_gsp_boots; *vram=v; *cpu=c; return 1; }
