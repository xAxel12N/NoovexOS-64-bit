void nvgsp_tick(void){
    if(g_run_on||G.state<3||!G.shm)return;
    for(int k=0;k<16;k++){ u32 fn=0,nn=0; int r=msg_one(&fn,g_evbuf,sizeof(g_evbuf),&nn,0); if(r<=0)break; ev_handle(fn,g_evbuf,nn); nvl_nl();
        if(fn==0x1004&&nn>=8){ u32 ch=*(u32*)(g_evbuf+4); if(ch!=g_ch[0].chid&&ch!=g_ch[1].chid){ g_rc_other++; continue; } }
        if(fn==0x1004)g_rc_dead=1;
        if((fn==0x1004||fn==0x1005)&&G.state>=7&&!g_nv_disabled){ g_tick_err++; nvgsp_all_off(fn==0x1004?"GSP-RM reported a channel error on our channel (robust channel recovery)":"GSP-RM reported a GPU page fault"); save_log(0,"(running)"); } }
    if(g_auto_at&&hr_ns()>=g_auto_at&&G.state>=7){ extern u64 g_last_input_t; extern volatile u64 g_ticks; if(g_ticks-g_last_input_t<200){ g_auto_at=hr_ns()+1000000000ULL; return; } g_auto_at=0; g_auto_tries++; char m[120];
        if(!g_nv_disabled){ nvl_s("automatic re-check (once per boot, while the mouse and keyboard are idle) after: "); nvl_s(g_auto_why); nvl_nl(); nvgsp_all_off("automatic re-check after a GPU timeout"); }
        nvgsp_on(m,120); klog(m); }
    if(G.state>=7&&!g_nv_disabled&&hr_ns()-g_tick_live>1000000000ULL){ g_tick_live=hr_ns(); u32 b=0;
        if(nv_gone(g_nv_boot0,&b)){ g_tick_err++; nvgsp_all_off("the card stopped answering register reads (0xFFFFFFFF or a wrong BOOT0, twice in a row)"); save_log(0,"(running)"); } } }
int nvgsp_off(void){ return g_nv_disabled; }
u64 nvgsp_off_age_s(void){ return g_nv_disabled&&g_off_t?(hr_ns()-g_off_t)/1000000000ULL:0; }
int nvgsp_on(char* msg,int max){
    const char* w=0; char why[80]; why[0]=0; int already=0;
    if(G.state<7)w="the NVIDIA card is not running - run nvgsp all first";
    else if(!g_nv_disabled){ w="NVIDIA drawing is already on"; already=1; }
    else if(nv_gone(g_nv_boot0,0))w="the card does not answer register reads - restart the computer";
    else if(g_rpc_dead)w="GSP-RM stopped answering - restart the computer (or nvgsp unload, then nvgsp all)";
    else if(g_hung_p[0]||g_hung_p[1])w="a GPU channel is still waiting for work that timed out - nvgsp unload, then nvgsp all";
    else if(g_rc_dead)w="GSP-RM stopped one of our GPU channels after an error - nvgsp unload, then nvgsp all, makes new ones";
    if(w){ int i=0; while(w[i]&&i<max-1){ msg[i]=w[i]; i++; } msg[i]=0; return already; }
    g_nv_disabled=0; g_off_why[0]=0; g_off_t=0; g_on_count++; g_tick_live=hr_ns();
    klog("nvgsp: NVIDIA drawing turned back on - checking the copy engine, 2D engine and compute shaders again");
    if(g_sc.ok){ g_sc.on=1; g_sc.fails=0; g_sc.why[0]=0; g_sc.frames=0; }
    if(g_2d_obj)twod();
    if(g_cp_ready){ u32 us=0; extern int nvd_selftest(char*,int,u32*); g_cp_ok=1; if(!nvd_selftest(why,80,&us)){ g_cp_ok=0; cp_why(why); } }
    if(g_cp_ok){ extern void nvd_request(void); nvd_request(); }
    char* q=msg; char* e=msg+max-1; const char* parts[6]={"NVIDIA on: screen copy ",g_sc.on?"on":"off",", 2D engine ",g_2d_ok?"working":"off",", GPU drawing ",g_cp_ok?"starting":"off"};
    for(int k=0;k<6;k++) for(const char* t=parts[k];*t&&q<e;t++)*q++=*t; *q=0; klog(msg); return 1; }
int nvgsp_shutdown(void){
    if(G.state<1)return 1;
    klog("nvgsp: unloading GSP-RM before power off or restart");
    nvgsp_all_off("the computer is shutting down");
    int keep=g_nv_log_n; (void)keep; nvl_s("[shutdown] unloading GSP-RM, FWSEC-SB, booter_unload"); nvl_nl();
    int r=unload(); klog(r>0?"nvgsp: GSP-RM unloaded, WPR2 cleared":"nvgsp: GSP-RM unload did not finish"); return r; }
static u64 g_b1_next=0, g_b1_gen=0xFFFFFFFFULL, g_vs_bytes=0; static u32 g_vs_n=0;
int nvgsp_vram_surface(u64 bytes,u64* gpu_va,u32** cpu){
    if(!nvgsp_ce_ready()||!G.bar1||!G.bar1_len||!bytes)return 0;
    if(g_b1_gen!=g_ce_gen){ g_b1_gen=g_ce_gen; g_b1_next=(g_con_end+0x200000ULL+0x1FFFFFULL)&~0x1FFFFFULL; g_vs_bytes=0; g_vs_n=0; }
    u64 len=(bytes+0xFFFFULL)&~0xFFFFULL; u64 lim=G.bar1_len>0x3000000ULL?G.bar1_len-0x2000000ULL:0; if(g_b1_next+len>lim)return 0;
    u64 vram=nvv_alloc_raw(len,0x10000); if(!vram)return 0;
    u64 va=va_take(len,0x10000); if(!nvm_map(&g_uv,va,vram,len,0,0,0)){ nvv_free(vram); return 0; }
    nvvmm b1; b1.pd3=G.bar1_pde; b1.limit=G.bar1_len; b1.ok=1; u64 bo=g_b1_next;
    if(!nvm_map(&b1,bo,vram,len,0,0,0)){ nvv_free(vram); return 0; }
    nvm_tlb_flush(g_uv.pd3,0); nvm_tlb_flush(G.bar1_pde,1);
    if(!nvgsp_ce_fill(va,0x1000,0x400,(u32)(len/0x1000),0)){ nvv_free(vram); return 0; }
    u64 cp=acpi_hi_map(G.bar1); if(!cp){ nvv_free(vram); return 0; }
    g_b1_next+=len; g_vs_bytes+=len; g_vs_n++; *gpu_va=va; *cpu=(u32*)(cp+bo); return 1; }
void nvgsp_vram_stats(u32* n,u64* bytes,u64* bar1_left){ u64 lim=G.bar1_len>0x3000000ULL?G.bar1_len-0x2000000ULL:0; if(n)*n=g_b1_gen==g_ce_gen?g_vs_n:0; if(bytes)*bytes=g_b1_gen==g_ce_gen?g_vs_bytes:0; if(bar1_left)*bar1_left=g_b1_gen==g_ce_gen&&lim>g_b1_next?lim-g_b1_next:lim; }
int nvgsp_info(nvgsp_info_t* o){
    extern int g_nvd_on, g_nvd_auto; extern u16 g_nv_dev; extern int nv2d_ready(void);
    o->state=G.state; o->state_name=g_stname[G.state<8?G.state:0]; o->dev=g_nv_dev;
    o->present=nvgsp_present_on(); o->present_ok=g_sc.ok; o->twod=nv2d_ready(); o->twod_obj=g_2d_obj; o->compute=nvc_ready(); o->compute_loaded=g_cp_ready; o->drawing=g_nvd_on; o->autostart=g_nvd_auto;
    o->off=g_nv_disabled; o->off_why=g_nv_disabled?g_off_why:""; o->off_age_s=nvgsp_off_age_s(); o->cp_why=g_cp_why;
    o->frames=g_sc.frames; o->rframes=g_sc.rframes; o->ce_timeouts=g_ce_timeouts; o->readback_bad=g_sc.bad; o->twod_ops=g_2d_ops; o->twod_timeouts=g_2d_timeouts;
    o->cp_ops=g_cp_ops; o->cp_subs=g_cp_subs; o->cp_timeouts=g_cp_timeouts; o->cp_last_us=g_cp_last_us; o->gsp_errors=G.err_count; o->off_count=g_off_count; o->on_count=g_on_count;
    o->vram_total=G.state>=3?nvv_total():0; o->vram_free=G.state>=3?nvv_free_bytes():0; nvgsp_vram_stats(&o->vram_surfaces,&o->vram_surface_bytes,0);
    return G.state; }
int nvgsp_present_set(int on){ if(on&&(!g_sc.ok||g_nv_disabled))return 0; g_sc.on=on; if(on){ g_sc.fails=0; g_sc.why[0]=0; g_sc_wrong=0; } return 1; }

static nvgsp_mon_t g_mon[8]; static int g_mon_n=-1; static u32 g_mon_heads=0, g_mon_hmask=0; static char g_mon_why[96]; static u64 g_disp_inst=0; static u32 g_disp_gen=0xFFFFFFFFu;
static int edid_read(const u8* e,int n,nvgsp_mon_t* m){
    static const u8 hdr[8]={0,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0}; if(n<128)return 0; for(int i=0;i<8;i++) if(e[i]!=hdr[i])return 0;
    u8 cs=0; for(int i=0;i<128;i++)cs=(u8)(cs+e[i]); if(cs)return 0;
    u16 mf=(u16)((e[8]<<8)|e[9]); m->maker[0]=(char)('A'-1+((mf>>10)&31)); m->maker[1]=(char)('A'-1+((mf>>5)&31)); m->maker[2]=(char)('A'-1+(mf&31)); m->maker[3]=0; m->product=(u16)(e[10]|(e[11]<<8));
    m->name[0]=0; m->nmodes=0;
    for(int b=54;b<=108;b+=18){ const u8* d=e+b; u32 pk=(u32)(d[0]|(d[1]<<8));
        if(pk){ if(m->nmodes>=4)continue; u32 ha=(u32)(d[2]|((d[4]&0xF0)<<4)), hb=(u32)(d[3]|((d[4]&0x0F)<<8)), va=(u32)(d[5]|((d[7]&0xF0)<<4)), vb=(u32)(d[6]|((d[7]&0x0F)<<8));
            u64 tot=(u64)(ha+hb)*(va+vb); if(!ha||!va||!tot)continue; int k=m->nmodes++; m->mw[k]=(u16)ha; m->mh[k]=(u16)va; m->mhz[k]=(u16)(((u64)pk*10000ULL+tot/2)/tot); }
        else if(d[3]==0xFC){ int o=0; for(int i=5;i<18&&o<13;i++){ char c=(char)d[i]; if(c=='\n')break; if(c<32||c>126)c='?'; m->name[o++]=c; } while(o>0&&m->name[o-1]==' ')o--; m->name[o]=0; } }
    return 1; }
const char* nvgsp_mon_sig(const nvgsp_mon_t* m){
    if(m->or_type==1)return "VGA (analog)"; if(m->or_type!=2)return m->or_type?"other output":"not routed";
    if(m->proto==8||m->proto==9)return "DisplayPort"; if(m->proto==1||m->proto==2)return (m->dfp_flags&(1u<<8))?"HDMI":"DVI"; if(m->proto==5)return "DVI dual link"; return "digital"; }
int nvgsp_mon_get(const nvgsp_mon_t** list,u32* heads,u32* headmask,const char** why){ if(list)*list=g_mon; if(heads)*heads=g_mon_heads; if(headmask)*headmask=g_mon_hmask; if(why)*why=g_mon_why; return g_mon_n; }
static int mon_fail(const char* w){ char* q=g_mon_why; q=scopy(q,w); *q=0; nvl_s("  "); nvl_s(w); nvl_nl(); return 0; }
int nvgsp_monitors(void){
    nvl_s("[19] monitors through GSP-RM (read only, like nouveau's r535_disp_oneinit): NV04_DISPLAY_COMMON, supported and connected outputs, output type, EDID"); nvl_nl();
    g_mon_n=-1; g_mon_why[0]=0;
    if(G.state<7)return mon_fail("the NVIDIA card is not running - run nvgsp all first");
    if(g_disp_gen!=g_gsp_boots||!g_disp_inst){ u64 v=nvv_alloc(0x10000,0x10000); if(!v)return mon_fail("no VRAM for the display instance memory");
        u8 w[24]; for(int i=0;i<24;i++)w[i]=0; *(u64*)w=v; *(u64*)(w+8)=0x10000; *(u32*)(w+16)=2; *(u32*)(w+20)=2;
        if(rm_ctrl(G.hclient,G.hsubdev,0x20800A49u,w,24)){ nvv_free(v); return mon_fail("GSP-RM refused the display instance memory (WRITE_INST_MEM)"); }
        g_disp_inst=v; g_disp_gen=g_gsp_boots; nvl_kv("display instance memory (64 KB VRAM)",v); }
    if(!client_dev(2)){ rm_free(HC(2),HC(2)); return mon_fail("could not create the display client"); }
    int ok=0; u32 hd=0x00730000u;
    if(rm_alloc(HC(2),HDEV,hd,0x0073,0,0)){ mon_fail("GSP-RM refused NV04_DISPLAY_COMMON"); goto done; }
    { u32 p[3]={0,0,0}; if(rm_ctrl(HC(2),hd,0x730102u,p,12)==0)g_mon_heads=p[2]; u32 q[2]={0,0}; if(rm_ctrl(HC(2),hd,0x730287u,q,8)==0)g_mon_hmask=q[1]; }
    nvl_kv("display heads",g_mon_heads); nvl_kv("head mask",g_mon_hmask);
    u32 sup=0, con=0; { u32 p[3]={0,0,0}; if(rm_ctrl(HC(2),hd,0x730120u,p,12)){ mon_fail("GET_SUPPORTED failed"); goto done; } sup=p[1]; }
    nvl_kv("supported outputs (display id mask)",sup);
    { u32 p[4]={0,0,sup,0}; if(rm_ctrl(HC(2),hd,0x730122u,p,16)==0)con=p[2]&sup; }
    nvl_kv("connected outputs",con);
    int n=0;
    for(int b=0;b<32&&n<8;b++){ if(!(sup&(1u<<b)))continue; nvgsp_mon_t* m=&g_mon[n]; u8* z=(u8*)m; for(u32 i=0;i<sizeof(*m);i++)z[i]=0; m->id=1u<<b; m->connected=(con>>b)&1;
        { u8 o[56]; for(int i=0;i<56;i++)o[i]=0; *(u32*)(o+4)=m->id; if(rm_ctrl(HC(2),hd,0x73028Bu,o,56)==0){ m->or_type=*(u32*)(o+12); m->proto=*(u32*)(o+16); } }
        if(m->or_type==0)continue;
        if(m->connected){ u32 f[4]={0,m->id,0,0}; if(rm_ctrl(HC(2),hd,0x731140u,f,16)==0)m->dfp_flags=f[2];
            static u8 ed[16+2048]; for(int i=0;i<(int)sizeof(ed);i++)ed[i]=0; *(u32*)(ed+4)=m->id;
            if(rm_ctrl(HC(2),hd,0x730245u,ed,sizeof(ed))==0){ u32 len=*(u32*)(ed+8); if(len>2048)len=2048; m->edid_len=(int)len; m->edid_ok=edid_read(ed+16,(int)len,m); } }
        nvl_s("  output "); nvl_x(m->id); nvl_s(": "); nvl_s(nvgsp_mon_sig(m)); nvl_s(m->connected?", connected":", nothing connected");
        if(m->connected){ if(m->edid_ok){ nvl_s(", monitor "); nvl_s(m->name[0]?m->name:"(no name)"); nvl_s(" ("); nvl_s(m->maker); nvl_s(")"); for(int k=0;k<m->nmodes;k++){ nvl_s(k?", ":", native "); nvl_d(m->mw[k]); nvl_s("x"); nvl_d(m->mh[k]); nvl_s("@"); nvl_d(m->mhz[k]); } }
            else { nvl_s(", EDID not read ("); nvl_d((u64)m->edid_len); nvl_s(" bytes)"); } }
        nvl_nl(); n++; }
    g_mon_n=n; ok=1;
    { int c=0; for(int i=0;i<n;i++)if(g_mon[i].connected)c++; nvl_s("  "); nvl_d((u64)c); nvl_s(" monitor(s) connected; nothing on the screen was changed (mode setting is stage 6)"); nvl_nl(); }
    done:
    rm_free(HC(2),hd); rm_free(HC(2),HC(2));
    return ok; }
