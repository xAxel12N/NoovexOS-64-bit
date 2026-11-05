static int boot_gsp(void){
    if(G.state>=2){ nvl_s("  GSP is already running"); nvl_nl(); return 1; }
    if(G.state<1){ int r=gsp_pre(); if(r<=0)return r; }
    return gsp_post(); }

static int bar_test(volatile u32* cpu,u64 vram,const char* what){
    nvp_wr32(vram,0x4E565831u); nvp_wr32(vram+4,0xA5A55A5Au); u32 a=cpu[0], b=cpu[1];
    cpu[2]=0x13572468u; cpu_mb(); (void)cpu[2]; u32 c=nvp_rd32(vram+8);
    nvl_s("  "); nvl_s(what); nvl_s(": read "); nvl_x(a); nvl_s(" "); nvl_x(b); nvl_s(", write seen in VRAM as "); nvl_x(c); nvl_nl();
    return a==0x4E565831u&&b==0xA5A55A5Au&&c==0x13572468u; }
static nvvmm g_bar2; static u64 g_bt2, g_bt1, g_bva1;
static int bars(void){
    nvl_s("[9] BAR2 (instance memory window)"); nvl_nl();
    u64 lim=G.bar3_len?G.bar3_len/2:0x1000000ULL;
    if(!nvm_vmm_new(&g_bar2,lim))return nv_fail(9,"no VRAM for the BAR2 page directory");
    u64 tv=nvv_alloc(0x10000,0x10000); if(!tv)return nv_fail(9,"no VRAM for the BAR2 test page");
    g_bt2=tv; if(!nvm_map(&g_bar2,0,tv,0x10000,0,0,0))return nv_fail(9,"could not build BAR2 page tables");
    struct { u32 type, pad; u64 val, shift; } up={1,0,nvm_pd3_entry0(&g_bar2),47};
    nvl_kv("BAR2 PD3[0]",up.val);
    if(!cmdq_push(70,&up,24,0))return nv_fail(9,"could not send UPDATE_BAR_PDE"); u8 rr[64]; u32 n=0; if(rpc_wait(70,rr,sizeof(rr),&n,5000)<=0)return nv_fail(9,"UPDATE_BAR_PDE was refused");
    nvm_tlb_flush(G.bar2_pde,1);
    u64 va3=acpi_hi_map(G.bar3); int ok2=va3?bar_test((volatile u32*)va3,tv,"BAR2 window"):0; if(!va3){ nvl_s("  BAR3 aperture cannot be mapped by the CPU"); nvl_nl(); }
    nvl_s("[10] BAR1 (VRAM window)"); nvl_nl();
    nvvmm b1; b1.pd3=G.bar1_pde; b1.limit=G.bar1_len; b1.ok=1;
    for(int i=0;i<4;i++)nvp_wr64(G.bar1_pde+(u64)i*8,0);
    int ok1=1;
    if(g_con_end){ if(!nvm_map(&b1,0,0,g_con_end,0,0,0))ok1=0; nvl_kv("console framebuffer mapped through BAR1, bytes",g_con_end); }
    u64 t1=nvv_alloc(0x10000,0x10000); u64 bva=G.bar1_len>0x2000000ULL?G.bar1_len-0x1000000ULL:g_con_end;
    g_bt1=t1; g_bva1=bva; if(!t1||!nvm_map(&b1,bva,t1,0x10000,0,0,0))ok1=0;
    nvm_tlb_flush(G.bar1_pde,1);
    u64 va1=acpi_hi_map(G.bar1); if(!va1){ nvl_s("  BAR1 aperture cannot be mapped by the CPU"); nvl_nl(); ok1=0; }
    else if(ok1)ok1=bar_test((volatile u32*)(va1+bva),t1,"BAR1 window");
    if(!ok2)return nv_fail(9,"BAR2 window does not reach VRAM");
    if(!ok1)return nv_fail(10,"BAR1 window does not reach VRAM");
    G.state=4; return 1; }

#define HC(c) (0xC1D00000u|(c))
#define HDEV 0xDE1D0000u
#define HSUB 0x5D1D0000u
#define HVAS 0x90F10000u
#define HCH(c) (0xF1F00000u|(c))
static int client_dev(u32 c){
    u8 p[108]; for(int i=0;i<108;i++)p[i]=0; *(u32*)p=HC(c); *(u32*)(p+4)=0xFFFFFFFFu;
    if(rm_alloc(HC(c),HC(c),HC(c),0x0000,p,108))return 0;
    u8 d[56]; for(int i=0;i<56;i++)d[i]=0; *(u32*)(d+4)=HC(c); if(rm_alloc(HC(c),HC(c),HDEV,0x0080,d,56))return 0;
    u32 s=0; if(rm_alloc(HC(c),HDEV,HSUB,0x2080,&s,4))return 0; return 1; }
static void memdesc(u8* p,u64 base,u64 size,u32 as,u32 ca){ *(u64*)p=base; *(u64*)(p+8)=size; *(u32*)(p+16)=as; *(u32*)(p+20)=ca; }
static int chan_alloc(u32 c,u32 chid,u32 et,int priv,u64 inst,u64 userd,u64 mthd,u32 mthd_as,u64 gpva,u32 gpn){
    static u8 p[360]; for(int i=0;i<360;i++)p[i]=0;
    *(u64*)(p+0x08)=gpva; *(u32*)(p+0x10)=gpn; *(u32*)(p+0x14)=((chid%8)<<8)|((chid/8)<<12)|(1u<<21)|((priv?1u:0u)<<5); *(u32*)(p+0x1C)=HVAS; *(u32*)(p+0x80)=et;
    memdesc(p+0x90,inst,0x1000,2,1); memdesc(p+0xA8,userd,0x200,2,1); memdesc(p+0xC0,inst,0x200,2,1); memdesc(p+0xD8,mthd,G.mthd_size,mthd_as,mthd_as==1?0:1);
    *(u32*)(p+0xF4)=priv?0x15u:0x14u;
    return rm_alloc(HC(c),HDEV,HCH(chid),0xC46F,p,360)==0; }

static struct { u32 idx, id, global, init, ro; u64 size, align; u64 mem; } g_cb[10]; static int g_ncb=0;
static int ctxbuf_info(void){
    static u8 p[1600]; for(int i=0;i<1600;i++)p[i]=0;
    if(rm_ctrl(G.hclient,G.hsubdev,0x20800A32u,p,1600))return 0;
    static const u32 T[][5]={{0x00,0,0,1,0},{0x10,2,0,1,0},{0x11,3,1,0,0},{0x0D,4,1,0,0},{0x13,5,1,0,0},{0x14,6,1,0,0},{0x17,9,1,1,0},{0x18,10,1,1,1},{0x18,11,1,1,1}};
    g_ncb=0; for(int i=0;i<9;i++){ u32 sz=*(u32*)(p+T[i][0]*8); if(!sz)continue; g_cb[g_ncb].idx=T[i][0]; g_cb[g_ncb].id=T[i][1]; g_cb[g_ncb].global=T[i][2]; g_cb[g_ncb].init=T[i][3]; g_cb[g_ncb].ro=T[i][4];
        u64 s=sz; if(T[i][1]==0)s=((s+0xFFFULL)&~0xFFFULL)+64*0x1000ULL; u64 pg=s>=0x200000ULL?0x200000ULL:s>=0x10000ULL?0x10000ULL:0x1000ULL; u64 al=pg;
        if(T[i][1]==5){ al=1; while(al<s)al<<=1; }
        g_cb[g_ncb].size=s; g_cb[g_ncb].align=al; g_cb[g_ncb].mem=0; g_ncb++;
        nvl_s("  context buffer id "); nvl_d(T[i][1]); nvl_s(" size "); nvl_x(s); nvl_nl(); }
    return g_ncb>0; }
static u64 g_va_next=0x200000000ULL;
static u64 va_take(u64 size,u64 align){ if(align<0x1000)align=0x1000; u64 v=(g_va_next+align-1)&~(align-1); g_va_next=v+((size+0xFFFULL)&~0xFFFULL); return v; }
static int promote(u32 c,u32 chid,nvvmm* vm,int golden){
    static u8 p[560]; for(int i=0;i<560;i++)p[i]=0; u32 n=0;
    *(u32*)p=1; *(u32*)(p+12)=HC(c); *(u32*)(p+16)=HCH(chid);
    for(int i=0;i<g_ncb&&n<16;i++){ int alloc=golden||!g_cb[i].global; if(!alloc&&g_cb[i].id==11)continue;
        u8* e=p+48+n*32; u64 mem=g_cb[i].mem;
        if(alloc){ mem=nvv_alloc(g_cb[i].size,g_cb[i].align); if(!mem){ nvl_kv("no VRAM for context buffer",g_cb[i].id); return 0; } if(g_cb[i].global)g_cb[i].mem=mem; }
        if(!mem)continue;
        *(u16*)(e+28)=(u16)g_cb[i].id; int initz=g_cb[i].init&&alloc; e[30]=(u8)initz;
        if(g_cb[i].id==10&&alloc)e[31]=1;
        else { u64 va=va_take(g_cb[i].size,g_cb[i].align); if(!nvm_map(vm,va,mem,g_cb[i].size,0,1,(int)g_cb[i].ro))return 0; *(u64*)(e+8)=va; }
        if(initz){ *(u64*)e=mem; *(u64*)(e+16)=g_cb[i].size; *(u32*)(e+24)=4; }
        nvl_s("  promote id "); nvl_d(g_cb[i].id); nvl_s(" va "); nvl_x(*(u64*)(e+8)); nvl_s(" pa "); nvl_x(*(u64*)e); nvl_s(" size "); nvl_x(*(u64*)(e+16)); nvl_s(e[30]?" init":""); nvl_s(e[31]?" not-mapped":" mapped"); nvl_nl();
        n++; }
    *(u32*)(p+40)=n; nvm_tlb_flush(vm->pd3,0);
    return rm_ctrl(HC(c),HSUB,0x2080012Bu,p,560)==0; }

static int golden(void){
    nvl_s("[12] GR golden context"); nvl_nl();
    if(!client_dev(0))return nv_fail(12,"golden client/device/subdevice allocation failed");
    nvvmm gv; if(!nvm_vmm_new(&gv,1ULL<<49))return nv_fail(12,"no VRAM for the golden page directory");
    u8 va[48]; for(int i=0;i<48;i++)va[i]=0; if(rm_alloc(HC(0),HDEV,HVAS,0x90F1,va,48))return nv_fail(12,"golden VA space allocation failed");
    u64 pd2=nvv_alloc(0x1000,0x1000), pd1=nvv_alloc(0x1000,0x1000); if(!pd2||!pd1)return nv_fail(12,"no VRAM");
    nvp_wr64(gv.pd3,(pd2>>4)|2ULL); nvp_wr64(pd2,(pd1>>4)|2ULL);
    static u8 cp[184]; for(int i=0;i<184;i++)cp[i]=0;
    *(u64*)(cp+8)=0x20000000ULL; *(u64*)(cp+16)=0x100000000ULL; *(u64*)(cp+24)=0x11FFFFFFFULL; *(u32*)(cp+32)=3;
    *(u64*)(cp+40)=gv.pd3; *(u64*)(cp+48)=0x20; *(u32*)(cp+56)=1; cp[60]=47;
    *(u64*)(cp+64)=pd2; *(u64*)(cp+72)=0x1000; *(u32*)(cp+80)=1; cp[84]=38;
    *(u64*)(cp+88)=pd1; *(u64*)(cp+96)=0x1000; *(u32*)(cp+104)=1; cp[108]=29;
    if(rm_ctrl(HC(0),HVAS,0x90F10106u,cp,184))return nv_fail(12,"COPY_SERVER_RESERVED_PDES failed");
    u64 gi=nvv_alloc(0x12000,0x1000); if(!gi)return nv_fail(12,"no VRAM for the golden channel");
    nvm_inst_join(&gv,gi);
    if(!chan_alloc(0,0,1,1,gi,gi+0x1000,gi+0x2000,1,0,512))return nv_fail(12,"golden channel allocation failed");
    if(!ctxbuf_info())return nv_fail(12,"could not read the GR context buffer sizes");
    if(!promote(0,0,&gv,1))return nv_fail(12,"PROMOTE_CTX for the golden channel failed");
    if(rm_alloc(HC(0),HCH(0),0x97000000u,0xC597,0,0))return nv_fail(12,"TURING_A on the golden channel failed");
    rm_free(HC(0),0x97000000u); rm_free(HC(0),HCH(0));
    nvl_s("  golden context created"); nvl_nl(); return 1; }

static int find_eng(u32 type,u32* runl){ for(u32 i=0;i<G.neng;i++) if(G.eng[i].type==type){ *runl=G.eng[i].runl; return 1; } return 0; }
static int fifo_info(void){
    static u8 p[3212]; for(int i=0;i<3212;i++)p[i]=0;
    if(rm_ctrl(G.hclient,G.hsubdev,0x20801112u,p,3212))return 0;
    u32 n=*(u32*)(p+4); if(n>32)n=32; G.neng=0;
    for(u32 i=0;i<n;i++){ u8* e=p+12+i*100; G.eng[G.neng].type=*(u32*)(e+2*4); G.eng[G.neng].runl=*(u32*)(e+3*4); char nm[17]; for(int k=0;k<16;k++)nm[k]=(char)e[84+k]; nm[16]=0;
        nvl_s("  engine "); nvl_s(nm); nvl_s(" type "); nvl_d(G.eng[G.neng].type); nvl_s(" runlist "); nvl_d(G.eng[G.neng].runl); nvl_nl(); G.neng++; }
    u32 ms=0; if(rm_ctrl(G.hclient,G.hsubdev,0x20802A08u,&ms,4))return 0; G.mthd_size=ms; nvl_kv("method buffer size",ms);
    return 1; }

static struct { u64 inst, userd; u8* mthd; u8* ring; u64 ringva; u32 chid, runl, et, put, token; } g_ch[2];
static int g_2d_obj=0, g_2d_ok=0;
static nvvmm g_uv;
static int g_nv_disabled=0; static char g_off_why[96]; static u64 g_off_t=0; static u32 g_on_count=0; static u32 g_off_count=0, g_tick_err=0, g_rc_other=0; static u64 g_tick_live=0;
static int g_rc_dead=0; static volatile u32* g_hung_p[2]; static u32 g_hung_v[2]; static u32 g_hung_n=0; static u32 g_auto_tries=0; static u64 g_auto_at=0; static char g_auto_why[80];
static void nv_auto_need(const char* why){ if(g_auto_at||g_auto_tries>=1)return; g_auto_at=hr_ns()+10000000000ULL; char* q=g_auto_why; q=scopy(q,why); *q=0; }
static int nv_gone(u32 want,u32* got){ u32 b=nvr32(0); if(b!=0xFFFFFFFFu&&(!want||b==want))return 0; hr_delay_us(1000); b=nvr32(0); if(got)*got=b; return b==0xFFFFFFFFu||(want&&b!=want); }
static const char* g_off_why_p(void){ return g_off_why; }
static u32* g_cepb=0; static volatile u32* g_cesem=0; static u64 g_cepb_va=0, g_cesem_va=0; static u32 g_ce_seq=0, g_ce_copies=0, g_ce_fills=0, g_ce_timeouts=0, g_ce_gen=0; static int g_ce_busy=0;
static struct { int ok, on; u64 vram, va, len; u32 pitch, h; const void* sptr; u32 ssw, sh; u64 sva; u32 frames, fails, bad, rframes, rects; char why[80]; } g_sc;
static void ch_kick(int k,u64 pbva,int n){
    u32* ring=(u32*)g_ch[k].ring; u32 put=g_ch[k].put; ring[put*2]=(u32)(pbva&0xFFFFFFFCULL); ring[put*2+1]=(u32)((pbva>>32)&0xFFu)|((u32)n<<10); cflush(ring,0x1000);
    put=(put+1)%512; g_ch[k].put=put; nvp_wr32(g_ch[k].userd+0x8C,put); (void)nvp_rd32(g_ch[k].userd+0x8C); cpu_mb(); nvw32(0xBB0090,g_ch[k].token); }
static void ce_kick(u64 pbva,int n){ ch_kick(1,pbva,n); }
static int user_chan(nvvmm* vm,int k,u32 chid,u32 et,u32 runl){
    g_ch[k].chid=chid; g_ch[k].et=et; g_ch[k].runl=runl; g_ch[k].put=0;
    g_ch[k].inst=nvv_alloc(0x1000,0x1000); g_ch[k].userd=nvv_alloc(0x1000,0x1000); g_ch[k].mthd=sa_contig(G.mthd_size?G.mthd_size:0x1000,0x1000); g_ch[k].ring=sa(0x1000,0x1000);
    if(!g_ch[k].inst||!g_ch[k].userd||!g_ch[k].mthd||!g_ch[k].ring)return 0;
    g_ch[k].ringva=va_take(0x1000,0x1000); if(!nvm_map(vm,g_ch[k].ringva,PA(g_ch[k].ring),0x1000,1,0,0))return 0; nvm_tlb_flush(vm->pd3,0);
    nvm_inst_join(vm,g_ch[k].inst);
    if(!chan_alloc(1,chid,et,0,g_ch[k].inst,g_ch[k].userd,PA(g_ch[k].mthd),1,g_ch[k].ringva,512))return 0;
    if(rm_ctrl(HC(1),HCH(chid),0xA06F0104u,&et,4))return 0;
    u8 sch[2]={1,0}; if(rm_ctrl(HC(1),HCH(chid),0xA06F0103u,sch,2))return 0;
    g_ch[k].token=(runl<<16)|chid; u32 tok=0; if(rm_ctrl(HC(1),HCH(chid),0xC36F0108u,&tok,4)==0){ g_ch[k].token=tok; nvl_s("  channel "); nvl_d(chid); nvl_s(" work-submit token "); nvl_x(tok); nvl_s(" (computed "); nvl_x((runl<<16)|chid); nvl_s(")"); nvl_nl(); }
    return 1; }

static int ce_test(nvvmm* vm){
    nvl_s("[15] copy-engine test (DMA 4 KB through the new channel)"); nvl_nl();
    u8* src=sa(0x1000,0x1000); u8* dst=sa(0x1000,0x1000); u32* pb=(u32*)sa(0x1000,0x1000); if(!src||!dst||!pb)return 0;
    for(int i=0;i<0x1000;i++){ src[i]=(u8)(i*7+3); dst[i]=0; }
    u64 vs=va_take(0x1000,0x1000), vd=va_take(0x1000,0x1000), vp=va_take(0x1000,0x1000);
    if(!nvm_map(vm,vs,PA(src),0x1000,1,0,0)||!nvm_map(vm,vd,PA(dst),0x1000,1,0,0)||!nvm_map(vm,vp,PA(pb),0x1000,1,0,0))return 0; nvm_tlb_flush(vm->pd3,0);
    int n=0; u32 sc=4u<<13;
    pb[n++]=(1u<<29)|(1u<<16)|sc|(0x0000u>>2); pb[n++]=0xC5B5u;
    pb[n++]=(1u<<29)|(4u<<16)|sc|(0x0400u>>2); pb[n++]=(u32)(vs>>32); pb[n++]=(u32)vs; pb[n++]=(u32)(vd>>32); pb[n++]=(u32)vd;
    pb[n++]=(1u<<29)|(4u<<16)|sc|(0x0410u>>2); pb[n++]=0x1000; pb[n++]=0x1000; pb[n++]=0x1000; pb[n++]=1;
    pb[n++]=(1u<<29)|(1u<<16)|sc|(0x0300u>>2); pb[n++]=0x186u;
    cflush(src,0x1000); cflush(dst,0x1000); cflush(pb,0x1000);
    nvl_kv("doorbell token",g_ch[1].token); ce_kick(vp,n);
    u64 t0=hr_ns(); int ok=0; u32 e0=G.err_count; while(hr_ns()-t0<2000000000ULL){ cflush(dst,0x1000); int same=1; for(int i=0;i<0x1000;i++) if(dst[i]!=src[i]){ same=0; break; } if(same){ ok=1; break; } hr_delay_us(1000);
        u32 fn=0,nn=0; if(msg_one(&fn,g_evbuf,sizeof(g_evbuf),&nn,0)>0)ev_handle(fn,g_evbuf,nn); if(G.err_count!=e0)break; }
    nvl_kv("USERD GPGet",nvp_rd32(g_ch[1].userd+0x88)); nvl_kv("USERD GPPut",nvp_rd32(g_ch[1].userd+0x8C));
    for(int k=0;k<20;k++){ u32 fn=0,nn=0; if(msg_one(&fn,g_evbuf,sizeof(g_evbuf),&nn,1)<=0)break; ev_handle(fn,g_evbuf,nn); }
    return ok; }

static int objects(void){
    if(G.state<3)return nv_fail(11,"GSP is not running - use nvgsp boot first");
    { extern void nvd_halt(const char*); nvd_halt("the NVIDIA GPU was restarted"); }
    g_rc_dead=0; g_hung_p[0]=0; g_hung_p[1]=0; g_nv_disabled=0; g_off_why[0]=0;
    g_cepb=0; g_cesem=0; g_sc.ok=0; g_sc.on=0; g_sc.sptr=0; g_ce_gen++; g_2d_obj=0; g_2d_ok=0; extern void nv2d_reset(void); nv2d_reset();
    nvl_s("[11] FIFO engines"); nvl_nl();
    if(!fifo_info())return nv_fail(11,"FIFO device info control failed");
    if(G.state<5){ int r=golden(); if(r<=0)return r; G.state=5; }
    nvl_s("[13] client, device, subdevice, GPU address space"); nvl_nl();
    if(!client_dev(1))return nv_fail(13,"client/device/subdevice allocation failed");
    if(!nvm_vmm_new(&g_uv,1ULL<<49))return nv_fail(13,"no VRAM for the page directory");
    u8 va[48]; for(int i=0;i<48;i++)va[i]=0; *(u32*)(va+4)=8; if(rm_alloc(HC(1),HDEV,HVAS,0x90F1,va,48))return nv_fail(13,"FERMI_VASPACE_A allocation failed");
    u8 pd[32]; for(int i=0;i<32;i++)pd[i]=0; *(u64*)pd=g_uv.pd3; *(u32*)(pd+8)=4; *(u32*)(pd+16)=HVAS;
    if(rm_ctrl(HC(1),HDEV,0x00801813u,pd,32))return nv_fail(13,"SET_PAGE_DIRECTORY failed");
    nvl_kv("address space root PD",g_uv.pd3);
    nvl_s("[14] channels and engine objects"); nvl_nl();
    u32 grl=0, cel=0, cet=0; if(!find_eng(1,&grl))return nv_fail(14,"no GR engine in the device table");
    for(u32 i=0;i<G.neng&&!cet;i++) if(G.eng[i].type>=9&&G.eng[i].type<19&&G.eng[i].runl!=grl){ cet=G.eng[i].type; cel=G.eng[i].runl; }
    if(!cet) for(u32 t=9;t<19;t++) if(find_eng(t,&cel)){ cet=t; break; }
    if(!cet)return nv_fail(14,"no copy engine in the device table");
    nvl_s("  copy engine for our channel: type "); nvl_d(cet); nvl_s(" runlist "); nvl_d(cel); nvl_s(cel==grl?" (shares the graphics runlist)":" (its own runlist)"); nvl_nl();
    if(!user_chan(&g_uv,0,8,1,grl))return nv_fail(14,"GR channel allocation failed");
    if(!promote(1,8,&g_uv,0))return nv_fail(14,"PROMOTE_CTX for the GR channel failed");
    if(rm_alloc(HC(1),HCH(8),0xC5970001u,0xC597,0,0))return nv_fail(14,"TURING_A (3D) object failed");
    nvl_s("  TURING_A (3D, class c597) created"); nvl_nl();
    if(rm_alloc(HC(1),HCH(8),0xC5C00001u,0xC5C0,0,0))return nv_fail(14,"TURING_COMPUTE_A object failed");
    nvl_s("  TURING_COMPUTE_A (class c5c0) created"); nvl_nl();
    g_2d_obj=rm_alloc(HC(1),HCH(8),0x902D0001u,0x902D,0,0)==0; g_2d_ok=0;
    nvl_s(g_2d_obj?"  FERMI_TWOD_A (2D engine, class 902d) created":"  FERMI_TWOD_A (2D engine) could not be created - no 2D drawing"); nvl_nl();
    if(!user_chan(&g_uv,1,9,cet,cel))return nv_fail(14,"copy-engine channel allocation failed");
    u32 cp[2]={1,cet}; if(rm_alloc(HC(1),HCH(9),0xC5B50001u,0xC5B5,cp,8))return nv_fail(14,"TURING_DMA_COPY_A object failed");
    nvl_s("  TURING_DMA_COPY_A (class c5b5) created"); nvl_nl();
    G.state=6;
    if(!ce_test(&g_uv))return nv_fail(15,"the copy engine did not copy the test buffer within 2 s");
    nvl_s("  copy engine copied 4096 bytes correctly - the GPU executes commands"); nvl_nl();
    G.state=7; return 1; }

static int unload(void){ { extern void nvd_halt(const char*); nvd_halt("the NVIDIA driver was unloaded"); }
    g_cepb=0; g_cesem=0; g_sc.ok=0; g_sc.on=0; g_sc.sptr=0; g_ce_gen++; g_2d_obj=0; g_2d_ok=0; nv2d_reset();
    if(G.state<1){ nvl_s("  GSP was not started by this session"); nvl_nl(); return 1; }
    if(G.state>=4&&!g_rpc_dead){ struct { u32 type, pad; u64 val, shift; } up={1,0,0,47}; if(cmdq_push(70,&up,24,0)){ u8 rr[64]; u32 n=0; rpc_wait(70,rr,sizeof(rr),&n,2000); } }
    if(G.state>=2){ u8 u[8]={0}; cmdq_push(47,u,8,0); u8 r[64]; u32 n=0; rpc_wait(47,r,sizeof(r),&n,5000); nv_poll(0x110040,0xFFFFFFFFu,0x80000000u,2000); }
    nvf_reset(NVF_GSP);
    int r=nv_run_fwsec(g_fw_acr.d,g_fw_acr.n,0x19); if(r<=0)return r;
    if(!nvr32(0x1FA828)){ nvl_s("  WPR2 not set"); nvl_nl(); G.state=0; return 1; }
    r=nv_booter(g_fw_bunload.d,g_fw_bunload.n,0,1); if(r<=0)return r;
    nvl_kv("WPR2 hi after unload",nvr32(0x1FA828)); G.state=0; return nvr32(0x1FA828)==0?1:nv_fail(5,"WPR2 is still set after booter_unload"); }

int nvgsp_ce_ready(void){ return G.state>=7&&g_uv.ok&&!g_nv_disabled; }
const char* nvgsp_off_why(void){ return g_nv_disabled?g_off_why_p():""; }
u32 nvgsp_ce_gen(void){ return g_ce_gen; }
void nvgsp_ce_unmap(u64 va,u64 len){ if(G.state<7||!g_uv.ok||!va||!len)return; u64 a=va&~0xFFFULL, n=((va+len+0xFFFULL)&~0xFFFULL)-a; nvm_unmap(&g_uv,a,n); nvm_tlb_flush(g_uv.pd3,0); }
u64 nvgsp_ce_map(u64 kva,u64 len){
    u64 pa=kva;
    if(!nvgsp_ce_ready()||!len)return 0;
    u64 a=pa&~0xFFFULL, n=((pa+len+0xFFFULL)&~0xFFFULL)-a; u64 va=va_take(n,0x10000);
    for(u64 o=0;o<n;){ u64 p=kva_to_pa(a+o); if(!p){ if(o)nvm_unmap(&g_uv,va,o); return 0; } p&=~0xFFFULL; u64 run=0x1000; while(o+run<n&&kva_to_pa(a+o+run)==p+run)run+=0x1000; if(!nvm_map(&g_uv,va+o,p,run,1,0,0)){ if(o)nvm_unmap(&g_uv,va,o); nvm_tlb_flush(g_uv.pd3,0); return 0; } o+=run; }
    nvm_tlb_flush(g_uv.pd3,0); return va+(pa-a); }
static int ce_setup(void){
    if(g_cepb)return 1;
    u32* pb=(u32*)sa(0x1000,0x1000); u32* sem=(u32*)sa(0x1000,0x1000); if(!pb||!sem)return 0;
    u64 pv=va_take(0x1000,0x1000), sv=va_take(0x1000,0x1000);
    if(!nvm_map(&g_uv,pv,PA(pb),0x1000,1,0,0)||!nvm_map(&g_uv,sv,PA(sem),0x1000,1,0,0))return 0; nvm_tlb_flush(g_uv.pd3,0);
    g_cepb=pb; g_cesem=sem; g_cepb_va=pv; g_cesem_va=sv; return 1; }
static int ch_ready(int k){ if(!g_hung_p[k])return 1; if(*g_hung_p[k]==g_hung_v[k]){ g_hung_p[k]=0; return 1; } return 0; }
static void ch_hung(int k,volatile u32* p,u32 v){ g_hung_p[k]=p; g_hung_v[k]=v; g_hung_n++; }
static int ce_run(u64 src,u32 spitch,u64 dst,u32 dpitch,u32 len,u32 lines,int fill,u32 val){
    if(!nvgsp_ce_ready()||!len||!lines||g_ce_busy||!ch_ready(1))return 0;
    if(!ce_setup())return 0;
    g_ce_busy=1;
    u32 seq=++g_ce_seq; if(!seq)seq=g_ce_seq=1; g_cesem[0]=0; cpu_mb();
    u32* pb=g_cepb; int n=0; u32 sc=4u<<13;
    pb[n++]=(1u<<29)|(1u<<16)|sc|(0x0000u>>2); pb[n++]=0xC5B5u;
    pb[n++]=(1u<<29)|(3u<<16)|sc|(0x0240u>>2); pb[n++]=(u32)(g_cesem_va>>32); pb[n++]=(u32)g_cesem_va; pb[n++]=seq;
    pb[n++]=(1u<<29)|(4u<<16)|sc|(0x0400u>>2); pb[n++]=(u32)(src>>32); pb[n++]=(u32)src; pb[n++]=(u32)(dst>>32); pb[n++]=(u32)dst;
    pb[n++]=(1u<<29)|(4u<<16)|sc|(0x0410u>>2); pb[n++]=spitch; pb[n++]=dpitch; pb[n++]=len; pb[n++]=lines;
    if(fill){ pb[n++]=(1u<<29)|(1u<<16)|sc|(0x0700u>>2); pb[n++]=val; pb[n++]=(1u<<29)|(1u<<16)|sc|(0x0708u>>2); pb[n++]=4u|(3u<<16); }
    pb[n++]=(1u<<29)|(1u<<16)|sc|(0x0300u>>2); pb[n++]=0x2u|0x4u|(1u<<3)|(1u<<7)|(1u<<8)|(1u<<9)|(fill?(1u<<10):0u);
    cflush(pb,0x1000); ce_kick(g_cepb_va,n);
    u64 t0=hr_ns(); while(g_cesem[0]!=seq){ if(hr_ns()-t0>200000000ULL){ g_ce_timeouts++; g_ce_busy=0; ch_hung(1,g_cesem,seq); return 0; } cpu_pause(); }
    if(fill)g_ce_fills++; else g_ce_copies++; g_ce_busy=0; return 1; }
int nvgsp_ce_copy2d(u64 src,u32 spitch,u64 dst,u32 dpitch,u32 wbytes,u32 lines){ return ce_run(src,spitch,dst,dpitch,wbytes,lines,0,0); }
#define CE_PBCAP 1000
static int ce_rects(u64 src,u32 spitch,u64 dst,u32 dpitch,const dmg_rect* r,int n){
    if(!nvgsp_ce_ready()||g_ce_busy||n<=0||!ch_ready(1))return 0;
    if(!ce_setup())return 0;
    int i=0;
    while(i<n){
        g_ce_busy=1;
        u32 seq=++g_ce_seq; if(!seq)seq=g_ce_seq=1; g_cesem[0]=0; cpu_mb();
        u32* pb=g_cepb; int k=0; u32 sc=4u<<13; int cnt=0;
        pb[k++]=(1u<<29)|(1u<<16)|sc|(0x0000u>>2); pb[k++]=0xC5B5u;
        pb[k++]=(1u<<29)|(3u<<16)|sc|(0x0240u>>2); pb[k++]=(u32)(g_cesem_va>>32); pb[k++]=(u32)g_cesem_va; pb[k++]=seq;
        while(i<n&&k+12<=CE_PBCAP){
            const dmg_rect* q=&r[i]; int last=(i+1==n)||(k+24>CE_PBCAP);
            u64 so=src+(u64)q->y*spitch+(u64)q->x*4, d=dst+(u64)q->y*dpitch+(u64)q->x*4;
            pb[k++]=(1u<<29)|(4u<<16)|sc|(0x0400u>>2); pb[k++]=(u32)(so>>32); pb[k++]=(u32)so; pb[k++]=(u32)(d>>32); pb[k++]=(u32)d;
            pb[k++]=(1u<<29)|(4u<<16)|sc|(0x0410u>>2); pb[k++]=spitch; pb[k++]=dpitch; pb[k++]=(u32)q->w*4u; pb[k++]=(u32)q->h;
            pb[k++]=(1u<<29)|(1u<<16)|sc|(0x0300u>>2); pb[k++]=0x2u|(1u<<7)|(1u<<8)|(1u<<9)|(last?(0x4u|(1u<<3)):0u);
            i++; cnt++; if(last)break; }
        cflush(pb,0x1000); ce_kick(g_cepb_va,k);
        u64 t0=hr_ns(); while(g_cesem[0]!=seq){ if(hr_ns()-t0>200000000ULL){ g_ce_timeouts++; g_ce_busy=0; ch_hung(1,g_cesem,seq); return 0; } cpu_pause(); }
        g_ce_copies+=(u32)cnt; g_ce_busy=0; }
    return 1; }
int nvgsp_ce_fill(u64 dst,u32 dpitch,u32 wpx,u32 lines,u32 val){ return ce_run(dst,dpitch,dst,dpitch,wpx,lines,1,val); }
void nvgsp_ce_stats(u32* copies,u32* timeouts){ if(copies)*copies=g_ce_copies; if(timeouts)*timeouts=g_ce_timeouts; }

static int g_sc_wrong=0;
extern void fb_shadow_invalidate(void) __attribute__((weak));
static int sc_off(const char* why){ g_sc.on=0; if(fb_shadow_invalidate)fb_shadow_invalidate(); nv_auto_need(why); char* p=g_sc.why; p=scopy(p,why); *p=0; char b[120]; p=b; p=scopy(p,"nvgsp: screen copies back on the CPU - "); p=scopy(p,why); *p=0; klog(b); return 0; }
static int scanout(void){
    nvl_s("[16] screen through the copy engine"); nvl_nl();
    g_sc.ok=0; g_sc.on=0; g_sc.sptr=0; g_sc.frames=0; g_sc.fails=0; g_sc.bad=0; g_sc.why[0]=0; g_sc_wrong=0;
    u64 fbpa=*(volatile u64*)(NV_BOOTINFO); u32 pitch=*(volatile u32*)(NV_BOOTINFO+8); u32 h=*(volatile u16*)(NV_BOOTINFO+14);
    nvl_kv("console framebuffer",fbpa); nvl_kv("pitch",pitch); nvl_kv("lines",h);
    if(!fbpa||!pitch||!h||!G.bar1||fbpa<G.bar1||fbpa+(u64)pitch*h>G.bar1+G.bar1_len){ nvl_s("  the screen is not in this card's VRAM window - it stays on CPU copies"); nvl_nl(); return 0; }
    u64 off=fbpa-G.bar1, a=off&~0xFFFULL, len=((off+(u64)pitch*h+0xFFFULL)&~0xFFFULL)-a;
    if(a+len>g_con_end+0x100000ULL){ nvl_s("  the screen is outside the reserved console VRAM - it stays on CPU copies"); nvl_nl(); return 0; }
    u64 va=va_take(len,0x200000ULL), tv=va_take(0x10000,0x10000);
    if(!g_bt1||!nvm_map(&g_uv,va,a,len,0,0,0)||!nvm_map(&g_uv,tv,g_bt1,0x10000,0,0,0)){ nvl_s("  could not map the screen into the GPU address space"); nvl_nl(); return 0; }
    nvm_tlb_flush(g_uv.pd3,0);
    nvl_kv("screen VRAM offset",off); nvl_kv("screen GPU address",va+(off-a));
    for(u32 i=0;i<0x2000;i+=4)nvp_wr32(g_bt1+i,0);
    int okf=nvgsp_ce_fill(tv,0x1000,256,2,0x5A3C1E0Fu);
    int f1=okf&&nvp_rd32(g_bt1)==0x5A3C1E0Fu&&nvp_rd32(g_bt1+255*4)==0x5A3C1E0Fu&&nvp_rd32(g_bt1+0x1000+255*4)==0x5A3C1E0Fu&&nvp_rd32(g_bt1+256*4)==0&&nvp_rd32(g_bt1+0x1000+256*4)==0;
    nvl_s("  copy-engine fill into VRAM (256 x 2 pixels): "); nvl_s(f1?"correct":okf?"WRONG DATA":"no completion"); nvl_nl();
    u32* src=(u32*)sa(0x1000,0x1000); u64 sv=src?nvgsp_ce_map((u64)src,0x1000):0; int f2=0, okc=0;
    if(sv){ for(u32 i=0;i<1024;i++)src[i]=0x9E3779B9u*(i+1); cflush(src,0x1000); okc=nvgsp_ce_copy2d(sv,0x1000,tv+0x4000,0x1000,0x1000,1); f2=okc; for(u32 i=0;i<1024&&f2;i++) if(nvp_rd32(g_bt1+0x4000+i*4)!=src[i])f2=0; }
    nvl_s("  copy-engine copy from system RAM into VRAM (4 KB): "); nvl_s(f2?"correct":okc?"WRONG DATA":"no completion"); nvl_nl();
    if(!f1||!f2){ nvl_s("  the screen stays on CPU copies"); nvl_nl(); return 0; }
    g_sc.vram=off; g_sc.va=va+(off-a); g_sc.len=len; g_sc.pitch=pitch; g_sc.h=h; g_sc.ok=1; g_sc.on=1;
    nvl_s("  desktop frames now go to the screen through the copy engine (nvgsp present off to stop)"); nvl_nl();
    return 1; }
int nvgsp_present_on(void){ return g_sc.ok&&g_sc.on&&nvgsp_ce_ready(); }
static int sc_prep(const u32* scene,u32 sw,u32 w,u32 h,volatile u32* fb,u32 pitchb){
    if(g_ce_busy||!nvgsp_present_on()||!scene||!fb||!w||!h||pitchb!=g_sc.pitch||h>g_sc.h||(u64)w*4>pitchb||w>sw)return 0;
    if(g_sc.sptr!=scene||g_sc.ssw!=sw||g_sc.sh<h){ if(g_sc.sptr&&g_sc.sva)nvgsp_ce_unmap(g_sc.sva,(u64)g_sc.ssw*g_sc.sh*4); g_sc.sptr=0; g_sc.sva=0; u64 v=nvgsp_ce_map((u64)scene,(u64)sw*h*4); if(!v)return sc_off("the screen buffer could not be mapped for the GPU"); g_sc.sva=v; g_sc.sptr=scene; g_sc.ssw=sw; g_sc.sh=h; }
    return 1; }
int nvgsp_fb_present_rects(const u32* scene,u32 sw,u32 w,u32 h,const dmg_rect* r,int n,volatile u32* fb,u32 pitchb){
    if(n<=0||!sc_prep(scene,sw,w,h,fb,pitchb))return 0;
    for(int i=0;i<n;i++) if(r[i].x<0||r[i].y<0||r[i].w<=0||r[i].h<=0||(u32)(r[i].x+r[i].w)>w||(u32)(r[i].y+r[i].h)>h)return 0;
    if(!ce_rects(g_sc.sva,sw*4,g_sc.va,pitchb,r,n)){ g_sc.fails++; if(g_sc.fails>=3)sc_off("the copy engine missed three frames"); return 0; }
    g_sc.frames++; g_sc.rframes++; g_sc.rects+=(u32)n;
    if(g_sc.frames<=30||(g_sc.frames&31)==0){ for(int i=0;i<n&&i<16;i++){ u32 x0=(u32)r[i].x, y0=(u32)r[i].y, x1=x0+(u32)r[i].w-1, y1=y0+(u32)r[i].h-1, xm=(x0+x1)/2, ym=(y0+y1)/2;
        if(fb[(u64)y0*(pitchb/4)+x0]!=scene[(u64)y0*sw+x0]||fb[(u64)y1*(pitchb/4)+x1]!=scene[(u64)y1*sw+x1]||fb[(u64)ym*(pitchb/4)+xm]!=scene[(u64)ym*sw+xm]){ g_sc.bad++; g_sc_wrong=1; return sc_off("the picture read back from the screen did not match"); } } }
    return 1; }
