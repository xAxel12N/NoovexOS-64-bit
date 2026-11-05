int nvgsp_fb_present(const u32* scene,u32 sw,u32 w,u32 h,volatile u32* fb,u32 pitchb){
    if(!sc_prep(scene,sw,w,h,fb,pitchb))return 0;
    if(!nvgsp_ce_copy2d(g_sc.sva,sw*4,g_sc.va,pitchb,w*4,h)){ g_sc.fails++; if(g_sc.fails>=3)sc_off("the copy engine missed three frames"); return 0; }
    g_sc.frames++;
    if(g_sc.frames<=30||(g_sc.frames&31)==0){ u32 px[5]={0,w-1,w/2,0,w-1}, py[5]={0,0,h/2,h-1,h-1};
        for(int k=0;k<5;k++){ u32 x=px[k], y=py[k]; if(fb[(u64)y*(pitchb/4)+x]!=scene[(u64)y*sw+x]){ g_sc.bad++; g_sc_wrong=1; return sc_off("the picture read back from the screen did not match"); } } }
    return 1; }
const char* nvgsp_gpu_name(void){ return G.name[0]?G.name:"NVIDIA GPU"; }

static u32* g_grpb=0; static volatile u32* g_grsem=0; static u64 g_grpb_va=0, g_grsem_va=0; static u32 g_gr_seq=0; static int g_gr_busy=0, g_gpn=0, g_2d_init=0;
static u32 g_2d_ops=0, g_2d_subs=0, g_2d_timeouts=0, g_2d_frames=0; static char g_2d_why[80];
void nv2d_reset(void){ g_grpb=0; g_grsem=0; g_gr_busy=0; g_gpn=0; g_2d_init=0; }
#define GPB_CAP 1000
static int g_gp_over=0;
static void gm(u32 subc,u32 m,int n){ if(g_gpn+n+1>GPB_CAP-16){ g_gp_over=1; g_gpn=0; } g_grpb[g_gpn++]=(1u<<29)|((u32)n<<16)|(subc<<13)|(m>>2); }
static void g1(u32 m,u32 v){ gm(3,m,1); g_grpb[g_gpn++]=v; }
static int gr_setup(void){
    if(g_grpb)return 1;
    u32* pb=(u32*)sa(0x1000,0x1000); u32* sem=(u32*)sa(0x1000,0x1000); if(!pb||!sem)return 0;
    u64 pv=va_take(0x1000,0x1000), sv=va_take(0x1000,0x1000);
    if(!nvm_map(&g_uv,pv,PA(pb),0x1000,1,0,0)||!nvm_map(&g_uv,sv,PA(sem),0x1000,1,0,0))return 0; nvm_tlb_flush(g_uv.pd3,0);
    g_grpb=pb; g_grsem=sem; g_grpb_va=pv; g_grsem_va=sv; return 1; }
static int gr_begin(void){
    if(!g_2d_obj||!nvgsp_ce_ready()||g_gr_busy||!ch_ready(0))return 0;
    if(!gr_setup())return 0;
    g_gr_busy=1; g_gpn=0; g_gp_over=0;
    if(!g_2d_init){ gm(3,0x0000,1); g_grpb[g_gpn++]=0x902Du; g1(0x0290,0); g1(0x029C,0); g1(0x0884,0x3F); g1(0x0888,1); g1(0x026C,1); g1(0x088C,0); g1(0x02AC,3); g_2d_init=1; }
    return 1; }
static int gr_end(int flush_l2){
    if(g_gp_over){ g_gp_over=0; g_gpn=0; g_gr_busy=0; g_2d_init=0; return 0; }
    u32 seq=++g_gr_seq; if(!seq)seq=g_gr_seq=1; g_grsem[0]=0; cpu_mb();
    if(flush_l2){ gm(0,0x0028,4); g_grpb[g_gpn++]=0; g_grpb[g_gpn++]=0; g_grpb[g_gpn++]=0; g_grpb[g_gpn++]=0x10u<<27; }
    gm(0,0x005C,5); g_grpb[g_gpn++]=(u32)g_grsem_va&0xFFFFFFFCu; g_grpb[g_gpn++]=(u32)(g_grsem_va>>32); g_grpb[g_gpn++]=seq; g_grpb[g_gpn++]=0; g_grpb[g_gpn++]=1u|(1u<<20);
    cflush(g_grpb,0x1000); ch_kick(0,g_grpb_va,g_gpn); g_2d_subs++;
    u64 t0=hr_ns(); while(g_grsem[0]!=seq){ if(hr_ns()-t0>200000000ULL){ g_2d_timeouts++; g_gr_busy=0; ch_hung(0,g_grsem,seq);
            if(g_2d_timeouts>=3&&g_2d_ok){ g_2d_ok=0; char* q=g_2d_why; q=scopy(q,"the 2D engine missed three times - drawing went back to the CPU"); *q=0; klog(g_2d_why); nv_auto_need(g_2d_why); }
            return 0; } cpu_pause(); }
    g_gr_busy=0; return 1; }
static void s_dst(const nv2d_surf* d){ gm(3,0x0200,2); g_grpb[g_gpn++]=d->fmt; g_grpb[g_gpn++]=1; gm(3,0x0214,5); g_grpb[g_gpn++]=d->pitch; g_grpb[g_gpn++]=d->w; g_grpb[g_gpn++]=d->h; g_grpb[g_gpn++]=(u32)(d->va>>32); g_grpb[g_gpn++]=(u32)d->va; }
static void s_src(const nv2d_surf* s){ gm(3,0x0230,2); g_grpb[g_gpn++]=s->fmt; g_grpb[g_gpn++]=1; gm(3,0x0244,5); g_grpb[g_gpn++]=s->pitch; g_grpb[g_gpn++]=s->w; g_grpb[g_gpn++]=s->h; g_grpb[g_gpn++]=(u32)(s->va>>32); g_grpb[g_gpn++]=(u32)s->va; }
static void s_op(u32 alpha){ if(alpha==NV2D_COPY){ g1(0x02AC,3); return; } u32 b=alpha==NV2D_OVER?255u:(alpha>255u?255u:alpha); g1(0x02A8,(b<<24)|(b<<16)|(b<<8)|b); g1(0x02AC,6); }
static int r_ok(const nv2d_surf* s,int x,int y,int w,int h){ return s&&s->va&&w>0&&h>0&&x>=0&&y>=0&&(u32)(x+w)<=s->w&&(u32)(y+h)<=s->h; }
static void q_fill(const nv2d_surf* d,int x,int y,int w,int h,u32 c,u32 alpha){
    s_dst(d); s_op(alpha); g1(0x0580,4); g1(0x0584,d->fmt); g1(0x0588,c);
    gm(3,0x0600,4); g_grpb[g_gpn++]=(u32)x; g_grpb[g_gpn++]=(u32)y; g_grpb[g_gpn++]=(u32)(x+w); g_grpb[g_gpn++]=(u32)(y+h); g_2d_ops++; }
static void q_blit(const nv2d_surf* d,int dx,int dy,int dw,int dh,const nv2d_surf* s,int sx,int sy,int sw,int sh,u32 alpha){
    s_dst(d); s_src(s); s_op(alpha);
    u64 du=((u64)(u32)sw<<32)/(u32)dw, dv=((u64)(u32)sh<<32)/(u32)dh; int scaled=(sw!=dw||sh!=dh);
    u64 sx0=((u64)(u32)sx<<32)+(scaled?du/2:0), sy0=((u64)(u32)sy<<32)+(scaled?dv/2:0); g1(0x088C,scaled?1u:0u);
    gm(3,0x08B0,12); g_grpb[g_gpn++]=(u32)dx; g_grpb[g_gpn++]=(u32)dy; g_grpb[g_gpn++]=(u32)dw; g_grpb[g_gpn++]=(u32)dh;
    g_grpb[g_gpn++]=(u32)du; g_grpb[g_gpn++]=(u32)(du>>32); g_grpb[g_gpn++]=(u32)dv; g_grpb[g_gpn++]=(u32)(dv>>32);
    g_grpb[g_gpn++]=(u32)sx0; g_grpb[g_gpn++]=(u32)(sx0>>32); g_grpb[g_gpn++]=(u32)sy0; g_grpb[g_gpn++]=(u32)(sy0>>32); g_2d_ops++; }
int nv2d_ready(void){ return g_2d_ok&&g_2d_obj&&nvgsp_ce_ready(); }
int nv2d_fill(const nv2d_surf* d,int x,int y,int w,int h,u32 c,u32 alpha){
    if(!nv2d_ready()||!r_ok(d,x,y,w,h)||!gr_begin())return 0;
    q_fill(d,x,y,w,h,c,alpha); return gr_end(!d->vram); }
int nv2d_blit(const nv2d_surf* d,int dx,int dy,int dw,int dh,const nv2d_surf* s,int sx,int sy,int sw,int sh,u32 alpha){
    if(!nv2d_ready()||!r_ok(d,dx,dy,dw,dh)||!r_ok(s,sx,sy,sw,sh)||!gr_begin())return 0;
    q_blit(d,dx,dy,dw,dh,s,sx,sy,sw,sh,alpha); return gr_end(!d->vram); }
int nv2d_screen(nv2d_surf* out){ if(!g_sc.ok||g_sc_wrong||!nv2d_ready())return 0; out->va=g_sc.va; out->pitch=g_sc.pitch; out->w=g_sc.pitch/4; out->h=g_sc.h; out->fmt=0xE6u; out->vram=1; return 1; }
static u64 g_ts_va=0, g_ts_len=0; static u32 g_ts_gen=0;
int nv2d_to_screen(const u32* src,u32 spitch_px,int sw,int sh,int dx,int dy,int scale,u64* src_va,const void** src_ptr){
    nv2d_surf sc; if(scale<1||sw<=0||sh<=0||(u32)sw>spitch_px||!nv2d_screen(&sc))return 0;
    u64 need=(u64)spitch_px*(u32)sh*4;
    if(g_ts_gen!=g_ce_gen){ g_ts_gen=g_ce_gen; g_ts_va=0; g_ts_len=0; }
    if(*src_ptr!=(const void*)src||!*src_va||*src_va!=g_ts_va||need>g_ts_len){ if(g_ts_va)nvgsp_ce_unmap(g_ts_va,g_ts_len); g_ts_va=0; g_ts_len=0; *src_va=nvgsp_ce_map((u64)src,need); *src_ptr=src; if(!*src_va)return 0; g_ts_va=*src_va; g_ts_len=need; }
    nv2d_surf s; s.va=*src_va; s.pitch=spitch_px*4; s.w=spitch_px; s.h=(u32)sh; s.fmt=0xE6u; s.vram=0;
    if(!nv2d_blit(&sc,dx,dy,sw*scale,sh*scale,&s,0,0,sw,sh,256))return 0;
    g_2d_frames++; return 1; }
void nv2d_stats(u32* ops,u32* subs,u32* timeouts,u32* frames){ if(ops)*ops=g_2d_ops; if(subs)*subs=g_2d_subs; if(timeouts)*timeouts=g_2d_timeouts; if(frames)*frames=g_2d_frames; }

static int px_near(u32 a,u32 b,int tol){ for(int k=0;k<24;k+=8){ int x=(int)((a>>k)&255u), y=(int)((b>>k)&255u); if(x-y>tol||y-x>tol)return 0; } return 1; }
static int twod(void){
    nvl_s("[17] 2D engine (FERMI_TWOD_A): fill, copy, 2x scale and blend, checked by reading VRAM back"); nvl_nl();
    g_2d_ok=0; g_2d_why[0]=0;
    if(!g_2d_obj){ nvl_s("  no 2D object - skipped"); nvl_nl(); return 0; }
    u64 vm=nvv_alloc(0x10000,0x10000); u64 va=vm?va_take(0x10000,0x10000):0;
    u32* sp=(u32*)sa(0x4000,0x1000); u64 sva=sp?nvgsp_ce_map((u64)sp,0x4000):0;
    if(!vm||!sp||!sva||!nvm_map(&g_uv,va,vm,0x10000,0,0,0)){ nvl_s("  no memory for the test surfaces"); nvl_nl(); return 0; }
    nvm_tlb_flush(g_uv.pd3,0);
    for(u32 i=0;i<0x10000;i+=4)nvp_wr32(vm+i,0xDEADBEEFu);
    for(int y=0;y<16;y++) for(int x=0;x<64;x++) sp[y*64+x]=0x00000000u|((u32)(x*4)<<16)|((u32)(y*16)<<8)|(u32)(x^y);
    for(int y=0;y<16;y++) for(int x=0;x<64;x++) sp[1024+y*64+x]=0x80404040u;
    cflush(sp,0x4000);
    nv2d_surf A={va,1024,256,64,0xCFu,1}; nv2d_surf B={sva,256,64,16,0xCFu,0}; nv2d_surf C={sva+0x1000,256,64,16,0xCFu,0};
    int ok=gr_begin();
    if(ok){ q_fill(&A,0,0,256,64,0x00102030u,256); q_fill(&A,10,5,40,15,0x00FF8000u,256); q_blit(&A,100,30,64,16,&B,0,0,64,16,256); q_blit(&A,180,0,64,32,&B,0,0,32,16,256);
        q_fill(&A,0,40,64,16,0x00C0C0C0u,256); q_blit(&A,0,40,64,16,&C,0,0,64,16,NV2D_OVER); ok=gr_end(0); }
    if(!ok){ nvl_kv("2D push buffers sent",g_2d_subs); nvl_kv("graphics channel GPGet",nvp_rd32(g_ch[0].userd+0x88)); nvl_kv("graphics channel GPPut",nvp_rd32(g_ch[0].userd+0x8C));
        for(int k=0;k<20;k++){ u32 fn=0,nn=0; if(msg_one(&fn,g_evbuf,sizeof(g_evbuf),&nn,1)<=0)break; ev_handle(fn,g_evbuf,nn); }
        nvl_s("  the 2D engine did not finish within 200 ms"); nvl_nl(); nvm_unmap(&g_uv,va,0x10000); nvm_tlb_flush(g_uv.pd3,0); nvv_free(vm); return 0; }
    struct { int x, y; u32 want; int tol; const char* what; } T[]={
        {0,0,0x00102030u,0,"fill"},{255,63,0x00102030u,0,"fill corner"},{10,5,0x00FF8000u,0,"rectangle start"},{49,19,0x00FF8000u,0,"rectangle last pixel"},{50,19,0x00102030u,0,"right of rectangle"},{49,20,0x00102030u,0,"below rectangle"},
        {100,30,sp[0],0,"copy first pixel"},{163,45,sp[15*64+63],0,"copy last pixel"},{164,45,0x00102030u,0,"right of copy"},
        {180,0,sp[0],0,"2x scale"},{181,1,sp[0],0,"2x scale same source pixel"},{182,0,sp[1],0,"2x scale next pixel"},{243,31,sp[15*64+31],0,"2x scale last"},
        {0,40,0x00A0A0A0u,3,"blend"},{63,55,0x00A0A0A0u,3,"blend last"}};
    int bad=0;
    for(u32 k=0;k<sizeof(T)/sizeof(T[0]);k++){ u32 g=nvp_rd32(vm+(u64)T[k].y*1024+(u64)T[k].x*4); int good=T[k].tol?px_near(g,T[k].want,T[k].tol):((g&0x00FFFFFFu)==(T[k].want&0x00FFFFFFu));
        if(!good){ bad++; nvl_s("  "); nvl_s(T[k].what); nvl_s(" at "); nvl_d((u64)T[k].x); nvl_s(","); nvl_d((u64)T[k].y); nvl_s(": wanted "); nvl_x(T[k].want); nvl_s(" got "); nvl_x(g); nvl_nl(); } }
    nvl_s("  "); nvl_d((u64)(sizeof(T)/sizeof(T[0])-(u32)bad)); nvl_s(" of "); nvl_d((u64)(sizeof(T)/sizeof(T[0]))); nvl_s(" pixels as expected"); nvl_nl();
    nvm_unmap(&g_uv,va,0x10000); nvm_tlb_flush(g_uv.pd3,0); nvv_free(vm);
    if(bad){ char* q=g_2d_why; q=scopy(q,"the 2D engine test pattern was wrong"); *q=0; return 0; }
    g_2d_ok=1; nvl_s("  the 2D engine draws correctly - Gears and screen copies can use it"); nvl_nl(); return 1; }

#include "include/nvx/drivers/video/nvidia/gqk_cubin.h"
static u32 lu16(const u8* p){ return (u32)p[0]|((u32)p[1]<<8); }
static u32 lu32(const u8* p){ return lu16(p)|(lu16(p+2)<<16); }
static u64 lu64(const u8* p){ return (u64)lu32(p)|((u64)lu32(p+4)<<32); }
static int nm_eq(const u8* a,const char* b){ while(*b){ if(*a!=(u8)*b)return 0; a++; b++; } return *a==0; }
static int nm_pre(const u8* a,const char* b){ while(*b){ if(*a!=(u8)*b)return 0; a++; b++; } return 1; }
int nvc_parse(const u8* f,u32 n,nvkern* k){
    for(u32 i=0;i<sizeof(*k);i++)((u8*)k)[i]=0; k->pofs=0x160; k->psz=136; const char* why=0;
    if(n<0x40||f[0]!=0x7F||f[1]!='E'||f[2]!='L'||f[3]!='F'||f[4]!=2||lu16(f+0x12)!=190){ why="not a 64-bit CUDA ELF"; goto bad; }
    { u32 fl=lu32(f+0x30); if((fl&0xFFu)!=75u&&((fl>>8)&0xFFu)!=75u){ why="not built for sm_75 (Turing)"; goto bad; } }
    u64 sho=lu64(f+0x28); u32 she=lu16(f+0x3A), shn=lu16(f+0x3C), sst=lu16(f+0x3E);
    if(she!=64||!shn||sst>=shn||sho+(u64)shn*64>n){ why="bad section table"; goto bad; }
    const u8* ss=f+sho+(u64)sst*64; u64 so=lu64(ss+24), sz=lu64(ss+32); if(so+sz>n){ why="bad section names"; goto bad; }
    const u8* names=f+so; int ti=-1;
    for(u32 i=0;i<shn;i++){ const u8* h=f+sho+(u64)i*64; u32 nm=lu32(h); if(nm>=sz)continue; const u8* s=names+nm; u64 o=lu64(h+24), l=lu64(h+32); u32 ty=lu32(h+4); if(ty!=8&&o+l>n){ why="section outside the file"; goto bad; }
        if(nm_eq(s,".text.gqk")){ ti=(int)i; k->code=f+o; k->code_len=(u32)l; k->regs=lu32(h+44)>>24; k->bars=(u32)((lu64(h+8)>>20)&0x1Fu); }
        else if(nm_pre(s,".rel")&&(nm_eq(s+4,".text.gqk")||nm_eq(s+5,".text.gqk"))){ why="the shader needs relocations"; goto bad; }
        else if(nm_pre(s,".nv.constant")&&s[12]>='0'&&s[12]<='7'&&nm_eq(s+13,".gqk")){ u32 b=(u32)(s[12]-'0'); k->cb[b]=f+o; k->cbl[b]=(u32)l; }
        else if(nm_pre(s,".nv.shared.gqk")){ k->smem=(u32)l; }
        else if(nm_pre(s,".nv.local")){ why="the shader needs local memory"; goto bad; }
        else if(nm_eq(s,".nv.info.gqk")||nm_eq(s,".nv.info")){ const u8* p=f+o; const u8* e=p+l;
            while(p+4<=e){ u32 fm=p[0], at=p[1], v=lu16(p+2); const u8* d=p+4; p+=fm==4?4+v:4; if(p>e)break;
                if(fm==4&&at==0x0A&&v>=8){ k->pofs=lu16(d+4); k->psz=lu16(d+6); }
                if(fm==3&&at==0x19)k->psz=v;
                if(fm==4&&(at==0x11||at==0x12||at==0x23)&&v>=8&&lu32(d+4)){ why="the shader needs a stack"; goto bad; }
                if(fm==4&&at==0x2F&&v>=8&&lu32(d+4)>k->regs)k->regs=lu32(d+4); } } }
    if(ti<0||!k->code_len||(k->code_len&15)){ why="no gqk code section"; goto bad; }
    if(!k->regs||k->regs>253){ why="register count out of range"; goto bad; }
    if(k->psz<136||k->pofs<0x30||(k->pofs&3)){ why="parameter block does not match the draw operation"; goto bad; }
    if(!k->cbl[0])k->cbl[0]=k->pofs+k->psz;
    if(k->cbl[0]<k->pofs+136){ why="constant bank 0 too small"; goto bad; }
    if(k->smem){ why="the shader needs shared memory"; goto bad; }
    k->ok=1; return 1;
    bad: k->ok=0; { char* q=k->why; const char* t=why; while(*t&&q<k->why+60)*q++=*t++; *q=0; } return 0; }

#define CP_MAXOPS 1600
#define CP_PBCAP 0x4000
static nvkern g_kn; static u64 g_kcode_va=0, g_kcode_vram=0, g_kcb_va[8]; static u8* g_kcb_mem=0;
static u8* g_cpd=0; static u64 g_cpd_va=0; static u32 g_cp_slot=768, g_cpi=0; static u32* g_cppb=0; static u64 g_cppb_va=0; static int g_cpn=0;
static volatile u32* g_cpsem=0; static u64 g_cpsem_va=0; static u32 g_cp_seq=0; static int g_cp_ok=0, g_cp_ready=0, g_cp_gen=-1;
static u32 g_cp_ops=0, g_cp_subs=0, g_cp_waits=0, g_cp_timeouts=0, g_cp_last_us=0; static char g_cp_why[80];
static void cpm(u32 m,int n){ g_cppb[g_cpn++]=(1u<<29)|((u32)n<<16)|(1u<<13)|(m>>2); }
static void cp1(u32 m,u32 v){ cpm(m,1); g_cppb[g_cpn++]=v; }
static void qset(u32* q,u32 lo,u32 hi,u32 v){ u32 w=lo>>5, b=lo&31, n=hi-lo+1; u32 m=n>=32?0xFFFFFFFFu:((1u<<n)-1u); q[w]=(q[w]&~(m<<b))|((v&m)<<b); }
static int cp_why(const char* s){ char* q=g_cp_why; q=scopy(q,s); *q=0; return 0; }
static int cp_load(void){
    if(g_cp_ready&&g_cp_gen==(int)g_ce_gen)return 1;
    g_cp_ready=0; g_cp_ok=0; g_cpn=0; g_cpi=0;
    if(!g_gqk_cubin_len)return cp_why("the compiled shader is not built in");
    if(!nvc_parse(g_gqk_cubin,g_gqk_cubin_len,&g_kn))return cp_why(g_kn.why);
    u64 cl=((u64)g_kn.code_len+0x1000+0xFFFULL)&~0xFFFULL;
    g_kcode_vram=nvv_alloc(cl,0x10000); if(!g_kcode_vram)return cp_why("no VRAM for the shader code");
    for(u64 i=0;i<cl;i+=4){ u32 v=i<g_kn.code_len?lu32(g_kn.code+i):0; nvp_wr32(g_kcode_vram+i,v); }
    g_kcode_va=va_take(cl,0x10000); if(!nvm_map(&g_uv,g_kcode_va,g_kcode_vram,cl,0,0,1))return cp_why("could not map the shader code");
    g_kcb_mem=sa(0x1000,0x1000); if(!g_kcb_mem)return cp_why("out of memory for shader constants"); u32 off=0;
    for(int b=1;b<8;b++){ g_kcb_va[b]=0; if(!g_kn.cbl[b])continue; u32 l=(g_kn.cbl[b]+255u)&~255u; if(off+l>0x1000)return cp_why("shader constants too large");
        for(u32 i=0;i<g_kn.cbl[b];i++)g_kcb_mem[off+i]=g_kn.cb[b][i]; g_kcb_va[b]=off; off+=l; }
    u64 kv=nvgsp_ce_map((u64)g_kcb_mem,0x1000); if(!kv)return cp_why("could not map the shader constants"); for(int b=1;b<8;b++) if(g_kn.cbl[b])g_kcb_va[b]+=kv;
    g_cp_slot=256u+((g_kn.cbl[0]+255u)&~255u);
    u64 dl=(u64)g_cp_slot*CP_MAXOPS; g_cpd=sa(dl,0x1000); g_cppb=(u32*)sa(CP_PBCAP*4,0x1000); u32* sem=(u32*)sa(0x1000,0x1000);
    if(!g_cpd||!g_cppb||!sem)return cp_why("out of memory for launch descriptors");
    g_cpd_va=nvgsp_ce_map((u64)g_cpd,dl); g_cppb_va=nvgsp_ce_map((u64)g_cppb,CP_PBCAP*4); g_cpsem_va=nvgsp_ce_map((u64)sem,0x1000); g_cpsem=sem;
    if(!g_cpd_va||!g_cppb_va||!g_cpsem_va)return cp_why("could not map the launch descriptors");
    nvm_tlb_flush(g_uv.pd3,0); g_cp_gen=(int)g_ce_gen; g_cp_ready=1; return 1; }
int nvc_ready(void){ return g_cp_ok&&g_cp_ready&&g_cp_gen==(int)g_ce_gen&&nvgsp_ce_ready(); }
static void cp_head(void){
    g_cpn=0; g_cpi=0;
    cp1(0x0000,0xC5C0u);
    cpm(0x02A0,2); g_cppb[g_cpn++]=0; g_cppb[g_cpn++]=0xFEu<<24;
    cpm(0x07B0,2); g_cppb[g_cpn++]=0; g_cppb[g_cpn++]=0xFFu<<24;
    cp1(0x021C,1u|(1u<<4)|(1u<<12)); }
int nvc_op(const nvcop* o,int wait){
    if(!g_cp_ready||g_cp_gen!=(int)g_ce_gen)return -1;
    if(g_cpi>=CP_MAXOPS||g_cpn+48>CP_PBCAP)return -1;
    if(!g_cpn)cp_head();
    if(wait&&g_cpi){ cp1(0x0110,0); g_cp_waits++; }
    u32 gx=(o->w+31u)/32u, gy=(o->h+3u)/4u; if(!gx||!gy)return 0;
    u8* sl=g_cpd+(u64)g_cpi*g_cp_slot; u64 sv=g_cpd_va+(u64)g_cpi*g_cp_slot; g_cpi++;
    u32* q=(u32*)sl; for(int i=0;i<64;i++)q[i]=0;
    u32* c=(u32*)(sl+256); u32 cl=(g_kn.cbl[0]+255u)&~255u; for(u32 i=0;i<cl/4;i++)c[i]=0;
    c[0]=32; c[1]=4; c[2]=1; c[3]=gx; c[4]=gy; c[5]=1;
    u32* p=(u32*)(sl+256+g_kn.pofs);
    p[0]=o->k; p[1]=o->x; p[2]=o->y; p[3]=o->w; p[4]=o->h; p[5]=(u32)o->sdx; p[6]=(u32)o->sdy; p[7]=(u32)o->mdx; p[8]=(u32)o->mdy; p[9]=o->c0; p[10]=o->c1; p[11]=o->a; p[12]=o->inv; p[13]=o->key; p[14]=o->goff; p[15]=0;
    for(int s=0;s<3;s++){ const nvcsurf* f=s==0?&o->d:s==1?&o->s:&o->m; u32* e=p+16+s*6; e[0]=(u32)f->va; e[1]=(u32)(f->va>>32); e[2]=f->pitch; e[3]=f->w; e[4]=f->h; e[5]=0; }
    u32 regs=(g_kn.regs+2u+7u)&~7u; if(regs>255u)regs=255u; u64 cb0=sv+256;
    qset(q,134,134,1); qset(q,189,189,1); qset(q,191,191,1); qset(q,366,366,1); qset(q,368,369,1); qset(q,378,378,1);
    qset(q,384,415,gx); qset(q,416,431,gy); qset(q,448,463,1);
    qset(q,544,561,0); qset(q,562,568,3); qset(q,569,575,17); qset(q,657,663,3);
    qset(q,576,579,2); qset(q,580,583,2);
    qset(q,592,607,32); qset(q,608,623,4); qset(q,624,639,1);
    qset(q,648,656,regs); qset(q,955,959,g_kn.bars);
    for(int b=0;b<8;b++){ u64 a=b?g_kcb_va[b]:cb0; u32 l=b?g_kn.cbl[b]:cl; if(!a||!l)continue; u32 base=1024u+(u32)b*64u;
        qset(q,640u+(u32)b,640u+(u32)b,1); qset(q,base,base+31,(u32)a); qset(q,base+32,base+48,(u32)(a>>32)); qset(q,base+51,base+63,(l+15u)>>4); }
    qset(q,1536,1567,(u32)g_kcode_va); qset(q,1568,1584,(u32)(g_kcode_va>>32));
    cp1(0x02B4,(u32)(sv>>8)); cp1(0x02BC,3); g_cp_ops++; return 0; }
int nvc_submit(void){
    if(!g_cpn)return 0;
    if(!g_cp_ready||g_cp_gen!=(int)g_ce_gen||g_gr_busy||!ch_ready(0)){ g_cpn=0; g_cpi=0; return -1; }
    g_gr_busy=1;
    u32 seq=++g_cp_seq; if(!seq)seq=g_cp_seq=1; g_cpsem[0]=0; g_cpsem[4]=0; cpu_mb();
    cp1(0x0110,0);
    for(int k=1;k>=0;k--){ u64 a=g_cpsem_va+(u64)k*16u;
        g_cppb[g_cpn++]=(1u<<29)|(5u<<16)|(0x005Cu>>2); g_cppb[g_cpn++]=(u32)a&0xFFFFFFFCu; g_cppb[g_cpn++]=(u32)(a>>32); g_cppb[g_cpn++]=seq; g_cppb[g_cpn++]=0; g_cppb[g_cpn++]=1u|(1u<<20);
        if(k)g_cppb[g_cpn++]=(1u<<29)|(4u<<16)|(0x0028u>>2), g_cppb[g_cpn++]=0, g_cppb[g_cpn++]=0, g_cppb[g_cpn++]=0, g_cppb[g_cpn++]=0x10u<<27; }
    cflush(g_cpd,(u64)g_cpi*g_cp_slot); cflush(g_cppb,(u64)g_cpn*4); ch_kick(0,g_cppb_va,g_cpn); g_cp_subs++;
    g_cpn=0; g_cpi=0;
    u64 t0=hr_ns(); while(g_cpsem[0]!=seq){ if(hr_ns()-t0>500000000ULL){ g_cp_timeouts++; g_gr_busy=0; g_cp_ok=0; ch_hung(0,g_cpsem,seq); nv_auto_need("a compute batch timed out"); cp_why(g_cpsem[4]==seq?"the GPU finished but the cache flush did not complete":"a compute batch did not finish within 500 ms"); klog("nvgsp: GPU drawing stopped - a compute batch did not finish"); return -1; } cpu_pause(); }
    g_cp_last_us=(u32)((hr_ns()-t0)/1000ULL); g_gr_busy=0; return 0; }
void nvc_abort(void){ g_cpn=0; g_cpi=0; }
void nvc_stats(u32* ops,u32* subs,u32* waits,u32* tmo){ if(ops)*ops=g_cp_ops; if(subs)*subs=g_cp_subs; if(waits)*waits=g_cp_waits; if(tmo)*tmo=g_cp_timeouts; }
const char* nvc_why(void){ return g_cp_why; }
void nvc_set_ok(int ok){ g_cp_ok=ok; }
static int compute(void){
    nvl_s("[18] compute shaders (TURING_COMPUTE_A): the desktop drawing shader, checked against the CPU"); nvl_nl();
    g_cp_ok=0; g_cp_why[0]=0;
    if(!cp_load()){ nvl_s("  "); nvl_s(g_cp_why); nvl_nl(); return 0; }
    nvl_kv("shader code bytes",g_kn.code_len); nvl_kv("registers",g_kn.regs); nvl_kv("parameter offset in constant bank 0",g_kn.pofs); nvl_kv("constant bank 2 bytes",g_kn.cbl[2]); nvl_kv("shader GPU address",g_kcode_va);
    char why[80]; why[0]=0; extern int nvd_selftest(char* why,int max,u32* us); u32 us=0; int r=nvd_selftest(why,80,&us);
    nvl_kv("compute launches",g_cp_ops); nvl_kv("compute batches",g_cp_subs); nvl_kv("waits between dependent launches",g_cp_waits); nvl_kv("self-test time us",us);
    if(!r){ nvl_s("  "); nvl_s(why); nvl_nl(); cp_why(why); for(int k=0;k<20;k++){ u32 fn=0,nn=0; if(msg_one(&fn,g_evbuf,sizeof(g_evbuf),&nn,1)<=0)break; ev_handle(fn,g_evbuf,nn); } return 0; }
    g_cp_ok=1; nvl_s("  every drawing operation on the GPU matched the CPU pixel for pixel - the desktop can be drawn by the GPU"); nvl_nl(); return 1; }

static const char* g_stname[]={"not started","booter ran","GSP-RM running","static info read, VRAM allocator ready","BAR1/BAR2 windows working","golden context ready","channels and engine objects created","copy engine verified"};
void nvgsp_status(void){
    nvl_s("NVIDIA GSP state: "); nvl_s(g_stname[G.state<8?G.state:0]); nvl_nl();
    if(g_nv_disabled){ nvl_s("NVIDIA drawing: OFF - "); nvl_s(g_off_why); nvl_s(" ("); nvl_d(nvgsp_off_age_s()); nvl_s(" s ago; Ctrl+Alt+N or nvgsp on turns it back on)"); nvl_nl(); }
    else if(G.state>=7){ nvl_s("NVIDIA drawing: on"); if(g_on_count){ nvl_s(" (turned back on "); nvl_d((u64)g_on_count); nvl_s(" time(s))"); } nvl_nl(); }
    if(g_off_count){ nvl_kv("times NVIDIA drawing was turned off",g_off_count); }
    if(g_auto_tries||g_auto_at){ nvl_kv("automatic re-checks after a timeout (max 1, only while idle)",g_auto_tries); }
    if(g_rc_other){ nvl_kv("channel errors on channels that are not ours (ignored, like nouveau)",g_rc_other); }
    if(g_nv_why[0]){ nvl_s("last failure: step "); nvl_d((u64)g_nv_step); nvl_s(" - "); nvl_s(g_nv_why); nvl_nl(); }
    if(G.state>=3){ nvl_kv("VRAM allocator total",nvv_total()); nvl_kv("VRAM allocator free",nvv_free_bytes()); nvl_kv("VRAM allocations",(u64)nvv_count()); nvl_kv("GSP errors seen",G.err_count); }
    if(G.state>=7){ nvl_s("screen through the copy engine: "); nvl_s(nvgsp_present_on()?"on":g_sc.ok?"off":"not set up"); nvl_nl();
        nvl_kv("frames presented",g_sc.frames); nvl_kv("frames sent as changed areas only",g_sc.rframes); nvl_kv("areas sent",g_sc.rects); nvl_kv("copies",g_ce_copies); nvl_kv("fills",g_ce_fills); nvl_kv("timeouts",g_ce_timeouts); nvl_kv("read-back mismatches",g_sc.bad);
        if(g_sc.why[0]){ nvl_s("last screen problem: "); nvl_s(g_sc.why); nvl_nl(); }
        nvl_s("2D engine: "); nvl_s(nv2d_ready()?"working":g_2d_obj?"created, not verified":"not created"); nvl_nl(); nvl_kv("2D operations",g_2d_ops); nvl_kv("2D push buffers",g_2d_subs); nvl_kv("2D timeouts",g_2d_timeouts); nvl_kv("frames put on the screen by the 2D engine",g_2d_frames);
        { extern int g_nvd_on; nvl_s("compute shaders: "); nvl_s(nvc_ready()?"working":g_cp_ready?"loaded, not verified":"not loaded"); if(g_cp_why[0]){ nvl_s(" - "); nvl_s(g_cp_why); } nvl_nl();
          nvl_s("desktop drawn by the NVIDIA GPU: "); nvl_s(g_nvd_on?"yes":"no"); nvl_nl(); nvl_kv("compute launches",g_cp_ops); nvl_kv("compute batches",g_cp_subs); nvl_kv("waits between dependent launches",g_cp_waits); nvl_kv("compute timeouts",g_cp_timeouts); nvl_kv("last batch us",g_cp_last_us); }
        if(g_2d_why[0]){ nvl_s("last 2D problem: "); nvl_s(g_2d_why); nvl_nl(); } } }

static char g_logout[65536+4096];
static char* lo_s(char* o,const char* s){ char* e=g_logout+sizeof(g_logout)-2; while(*s&&o<e)*o++=*s++; return o; }
static char* lo_n(char* o,const char* s,int n){ char* e=g_logout+sizeof(g_logout)-2; for(int k=0;k<n&&o<e;k++)*o++=s[k]; return o; }
static char* lo_wrap(char* o,const char* p){
    const int WRAP=72;
    while(*p){ const char* e=p; while(*e&&*e!='\n')e++; const char* q=p; int first=1;
        if(q==e)o=lo_s(o,"\n");
        while(q<e){ int room=first?WRAP:WRAP-6; int n=(int)(e-q);
            if(n>room){ int cut=0; for(int k=room;k>room/2;k--) if(q[k]==' '){ cut=k; break; } n=cut?cut:room; }
            if(!first)o=lo_s(o,"      ");
            o=lo_n(o,q,n); o=lo_s(o,"\n"); q+=n; while(q<e&&*q==' ')q++; first=0; }
        p=*e?e+1:e; }
    return o; }
static int g_run_on=0, g_run_step=0, g_run_dead=0; static u64 g_run_t0=0, g_step_t0=0; static const char* g_run_cmd="all"; static u32 g_run_boot0=0; static char g_run_name[72];
static int log_build(int r,const char* cmd){
    static char hd[512]; char* o=g_logout; char* h=hd; char* he=hd+sizeof(hd)-24;
    const char* parts[8]; int np=0; parts[np++]="nvgsp "; parts[np++]=cmd; parts[np++]=" - RESULT: ";
    for(int i=0;i<np;i++) for(const char* q=parts[i];*q&&h<he;q++)*h++=*q;
    if(r==2){ const char* t="STOPPED during step "; while(*t&&h<he)*h++=*t++; char d[12]; int n=0; u32 v=(u32)g_run_step; do{ d[n++]=(char)('0'+v%10); v/=10; }while(v); while(n&&h<he)*h++=d[--n]; *h++=':'; *h++=' '; t=g_run_name; while(*t&&h<he)*h++=*t++; t=" (the start-up was still running here - if the computer froze, this step is where)"; while(*t&&h<he)*h++=*t++; }
    else if(r>0){ const char* t="all steps passed"; while(*t&&h<he)*h++=*t++; if(G.state>=7){ t=nvgsp_present_on()?" (screen through the copy engine)":" (screen still copied by the CPU, see step 16)"; while(*t&&h<he)*h++=*t++;  { extern int g_nvd_on; t=g_nvd_on?", desktop drawn by the GPU":nvc_ready()?", compute shaders work":", no GPU drawing (see step 18)"; while(*t&&h<he)*h++=*t++; } } }
    else { const char* t="FAILED at step "; while(*t&&h<he)*h++=*t++; char d[12]; int n=0; u32 v=(u32)g_nv_step; do{ d[n++]=(char)('0'+v%10); v/=10; }while(v); while(n&&h<he)*h++=d[--n]; *h++=':'; *h++=' '; t=g_nv_why; while(*t&&h<he)*h++=*t++; }
    *h++='\n'; *h=0;
    o=lo_wrap(o,hd); o=lo_s(o,"driver: NoovexOS 64 v10.60\n"); o=lo_s(o,"state reached: "); o=lo_s(o,g_stname[G.state<8?G.state:0]); o=lo_s(o,"\n----------------------------------------------------------------\n");
    o=lo_wrap(o,g_nv_log);
    *o=0; return (int)(o-g_logout); }
void (*g_nv_progress)(int step,const char* what)=0;
static void save_quiet(int r){
    extern int fs_save_bin(const char*,const u8*,int,int); extern void nvx3_sync_all(void); extern int msd_ok, ufat_ok; extern int ufat_write_file(const char*,const char*,int);
    int n=log_build(r,g_run_cmd); fs_save_bin("nvgsp-log.txt",(const u8*)g_logout,n,0); nvx3_sync_all(); if(msd_ok&&ufat_ok)ufat_write_file("NVGSPLOG.TXT",g_logout,n); }
static void run_hook(const char* s){
    if(!g_run_on)return; int n=0; const char* p=s+1; while(*p>='0'&&*p<='9'){ n=n*10+(*p-'0'); p++; } if(*p!=']'||!n)return;
    u64 now=hr_ns();
    if(g_run_step){ nvl_s("  (step "); nvl_d((u64)g_run_step); nvl_s(" took "); nvl_d((now-g_step_t0)/1000000ULL); nvl_s(" ms)"); nvl_nl(); }
    if(g_run_boot0&&!g_run_dead){ u32 b=0; if(nv_gone(g_run_boot0,&b)){ g_run_dead=1; nvl_s("  the card stopped answering: BOOT0 read "); nvl_x(b); nvl_s(" twice in a row"); nvl_nl(); } }
    if(!g_run_dead&&now-g_run_t0>90000000000ULL){ g_run_dead=2; nvl_s("  the start-up has run for more than 90 s"); nvl_nl(); }
    g_run_step=n; g_step_t0=now;
    { const char* w=s+1; while(*w&&*w!=']')w++; if(*w)w++; while(*w==' ')w++; int i=0; while(w[i]&&i<70){ g_run_name[i]=w[i]; i++; } g_run_name[i]=0; }
    if(g_nv_progress)g_nv_progress(n,g_run_name);
    save_quiet(2); }
void nvgsp_all_off(const char* why){
    int any=g_sc.on||g_2d_ok||g_cp_ok||!g_nv_disabled; g_nv_disabled=G.state>=7; g_sc.on=0; g_2d_ok=0; g_cp_ok=0; g_cpn=0; g_cpi=0;
    { extern void nvd_halt(const char*); nvd_halt(why); }
    if(any){ char* q=g_off_why; q=scopy(q,why); *q=0; g_off_t=hr_ns(); g_off_count++; char b[140],*p=b; p=scopy(p,"nvgsp: all NVIDIA drawing paths off, the CPU draws - "); p=scopy(p,why); *p=0; klog(b); } }
static int run_check(void){ if(!g_run_dead)return 0; if(g_run_step<1)g_run_step=1; return nv_fail(g_run_step,g_run_dead==1?"the card stopped answering register reads":"the start-up took longer than 90 s"); }
static void save_log(int r,const char* cmd){
    extern int fs_save_bin(const char*,const u8*,int,int); extern void nvx3_sync_all(void);
    extern int msd_ok, ufat_ok; extern int msd_autoinit_safe(void); extern int ufat_write_file(const char*,const char*,int);
    int n=log_build(r,cmd); fs_save_bin("nvgsp-log.txt",(const u8*)g_logout,n,0); nvx3_sync_all();
    int usb=0; if(msd_ok||msd_autoinit_safe()){ if(ufat_ok&&ufat_write_file("NVGSPLOG.TXT",g_logout,n)==0)usb=1; }
    nvl_s(usb?"log saved: nvgsp-log.txt in Files and NVGSPLOG.TXT on the USB stick":"log saved: nvgsp-log.txt in Files (no writable USB stick found for NVGSPLOG.TXT)"); nvl_nl(); }
int nvgsp_cmd(const char* a){
    while(*a==' ')a++; int r=1;
    if(seq(a,"log")){ if(!g_nv_log_n){ nvl_s("no nvgsp log yet"); nvl_nl(); } return 1; }
    if(seq(a,"cardmem on")||seq(a,"cardmem off")){ g_nv_log_n=0; g_nv_log[0]=0; extern int g_nvd_vram, g_nvd_on; extern void gfx_stop(const char*); extern void nvd_request(void); int on=a[9]=='n'; int was=g_nvd_on; g_nvd_vram=on;
        if(was){ gfx_stop("switching where the work buffers live"); nvd_request(); }
        nvl_s(on?"GPU-only work buffers go into the card's memory":"all desktop images stay in normal RAM"); nvl_s(was?" - GPU drawing restarts":""); nvl_nl(); return 1; }
    if(seq(a,"monitors")){ nvl_reset(); nvl_s("nvgsp monitors"); nvl_nl(); int r3=nvgsp_monitors(); save_log(r3?1:0,a); return r3; }
    if(seq(a,"on")){ g_nv_log_n=0; g_nv_log[0]=0; char m[120]; int r2=nvgsp_on(m,120); nvl_s(m); nvl_nl(); return r2; }
    if(seq(a,"off")){ g_nv_log_n=0; g_nv_log[0]=0; if(G.state<7){ nvl_s("the NVIDIA card is not running"); nvl_nl(); return 0; } nvgsp_all_off("turned off with nvgsp off"); nvl_s("NVIDIA drawing off - the CPU draws (nvgsp on or Ctrl+Alt+N turns it back on)"); nvl_nl(); return 1; }
    if(seq(a,"present on")||seq(a,"present off")){ g_nv_log_n=0; g_nv_log[0]=0; int on=a[9]=='n';
        if(on&&!g_sc.ok){ nvl_s("the screen copy path is not set up - run nvgsp all first"); nvl_nl(); return 0; }
        g_sc.on=on; if(on){ g_sc.fails=0; g_sc.why[0]=0; g_sc_wrong=0; } nvl_s(on?"desktop frames go through the NVIDIA copy engine":"desktop frames are copied by the CPU"); nvl_nl(); return 1; }
    if(!a[0]||seq(a,"status")){ g_nv_log_n=0; g_nv_log[0]=0; nvgsp_status(); return 1; }
    nvl_reset(); nvl_s("nvgsp "); nvl_s(a); nvl_nl();
    { extern void (*g_nvl_hook)(const char*); g_nvl_hook=run_hook; g_run_on=!seq(a,"vram"); g_run_t0=g_step_t0=hr_ns(); g_run_step=0; g_run_dead=0; g_run_boot0=0; g_run_cmd=a; }
    if(G.state==0||seq(a,"vram")){ r=probe(); if(r<=0)goto out; }
    g_run_boot0=g_nv_boot0;
    if(seq(a,"all")&&G.state>=7){ g_run_on=0; nvl_s("  the NVIDIA card is already running - nothing was started again (nvgsp unload, then nvgsp all, to start it fresh)"); nvl_nl(); nvgsp_status(); return 1; }
    if(seq(a,"vram")){ u64 es=0x16A3350ULL; nv_fb_layout(es,0x1000); print_layout(); nvv_init(0,g_nvl.nonwpr_addr); nvl_kv("allocator span before GSP",nvv_total()); nvl_s("  (VRAM is not used before GSP-RM has scrubbed it; nvgsp boot hands the allocator the regions GSP reports)"); nvl_nl(); goto out; }
    if(seq(a,"boot")||seq(a,"all")){ r=boot_gsp(); if(r<=0)goto out; if(run_check()<0){ r=-g_run_step; goto out; } r=bars(); if(r<=0)goto out; if(run_check()<0){ r=-g_run_step; goto out; } }
    if(seq(a,"objects")||seq(a,"all")){ r=objects(); if(r<=0)goto out; if(run_check()<0){ r=-g_run_step; goto out; } scanout(); if(run_check()<0){ r=-g_run_step; goto out; } twod(); if(run_check()<0){ r=-g_run_step; goto out; } if(compute()){ extern void nvd_request(void); nvd_request(); } if(run_check()<0){ r=-g_run_step; goto out; } r=1; nvgsp_monitors(); }
    if(seq(a,"unload")){ r=unload(); goto out; }
    out: if(r<=0&&!seq(a,"unload"))nvgsp_all_off(g_nv_why[0]?g_nv_why:"the NVIDIA start-up did not finish");
    if(g_run_on&&g_run_step){ nvl_s("  (step "); nvl_d((u64)g_run_step); nvl_s(" took "); nvl_d((hr_ns()-g_step_t0)/1000000ULL); nvl_s(" ms, whole run "); nvl_d((hr_ns()-g_run_t0)/1000000ULL); nvl_s(" ms)"); nvl_nl(); }
    g_run_on=0; if(g_nv_progress)g_nv_progress(0,r>0?"done":"stopped"); nvgsp_status(); save_log(r,a); return r; }
