#include "include/nvx/drivers/video/nvidia/nv3d_state.h"
#include "include/nvx/drivers/video/nvidia/nv3d_shaders.h"
#define N3_PBW 0x6000u
#define N3_VBV 49152u
#define N3_FS_OFF 0x800u
static struct {
    u32 gen, boots; u32* pb; u64 pb_va; u32 n; int over, open, tested, good; volatile u32* sem; u64 sem_va; u32 seq;
    u64 sh_vram, sh_va, zero_vram, zero_va; float* vb; u64 vb_va; u32 nv;
    u64 ct_vram, ct_va, zt_vram, zt_va, s_len, ct_res, zt_res; u32 w, h, sw, sh;
    u32 frames, tris, subs, timeouts, last_us; char why[96];
} N3;
static int n3_fail(const char* w){ char* q=N3.why; q=scopy(q,w); *q=0; return 0; }
static u32 n3_fu(float f){ union { float f; u32 u; } x; x.f=f; return x.u; }
static void n3m(u32 m,u32 n){ if(N3.n+n+1>N3_PBW-64u){ N3.over=1; N3.n=0; } N3.pb[N3.n++]=(1u<<29)|(n<<16)|(m>>2); }
static void n3v(u32 v){ if(N3.n<N3_PBW)N3.pb[N3.n++]=v; }
static void n31(u32 m,u32 v){ n3m(m,1); n3v(v); }
static void n3_free_surf(void){
    if(N3.ct_va)nvm_unmap(&g_uv,N3.ct_va,N3.s_len); if(N3.zt_va)nvm_unmap(&g_uv,N3.zt_va,N3.s_len);
    if(N3.ct_va||N3.zt_va)nvm_tlb_flush(g_uv.pd3,0);
    if(N3.ct_vram)nvv_free(N3.ct_vram); if(N3.zt_vram)nvv_free(N3.zt_vram);
    N3.ct_va=0; N3.zt_va=0; N3.ct_vram=0; N3.zt_vram=0; N3.w=0; N3.h=0; }
static int n3_setup(void){
    if(!nvgsp_ce_ready())return n3_fail("the NVIDIA card is not running");
    if(N3.gen==g_ce_gen&&N3.pb_va)return 1;
    { u32 sm=nvgsp_cls_cur()->sm; if(sm!=75&&!((sm==86||sm==89)&&g_nv_sm8x_try))return n3_fail(sm==86||sm==89?"the 3D shaders are not checked on RTX 30/40 hardware - off until enabled":"the 3D shaders are built for Turing (sm_75) only"); }
    if(N3.boots==g_gsp_boots){ if(N3.ct_vram)nvv_free(N3.ct_vram); if(N3.zt_vram)nvv_free(N3.zt_vram); if(N3.sh_vram)nvv_free(N3.sh_vram); if(N3.zero_vram)nvv_free(N3.zero_vram); }
    N3.pb_va=0; N3.ct_res=0; N3.zt_res=0; N3.ct_va=0; N3.zt_va=0; N3.ct_vram=0; N3.zt_vram=0; N3.sh_vram=0; N3.zero_vram=0; N3.w=0; N3.h=0; N3.tested=0; N3.good=0; N3.open=0; N3.boots=g_gsp_boots;
    if(!N3.pb)N3.pb=(u32*)sa((u64)N3_PBW*4,0x1000); if(!N3.sem)N3.sem=(volatile u32*)sa(0x1000,0x1000); if(!N3.vb)N3.vb=(float*)sa((u64)N3_VBV*32,0x1000);
    if(!N3.pb||!N3.sem||!N3.vb)return n3_fail("no memory for the 3D command and vertex buffers");
    u64 pv=nvgsp_ce_map((u64)N3.pb,(u64)N3_PBW*4), sv=nvgsp_ce_map((u64)N3.sem,0x1000), vv=nvgsp_ce_map((u64)N3.vb,(u64)N3_VBV*32);
    u64 sh=nvv_alloc(0x1000,0x1000), ze=nvv_alloc(0x1000,0x1000); u64 sva=va_take(0x1000,0x1000), zva=va_take(0x1000,0x1000); int ok=pv&&sv&&vv&&sh&&ze;
    if(ok){ for(u32 i=0;i<sizeof(nv3d_vs)/4;i++)nvp_wr32(sh+i*4,nv3d_vs[i]); for(u32 i=0;i<sizeof(nv3d_fs)/4;i++)nvp_wr32(sh+N3_FS_OFF+i*4,nv3d_fs[i]);
        ok=nvm_map(&g_uv,sva,sh,0x1000,0,0,1)&&nvm_map(&g_uv,zva,ze,0x1000,0,0,1); if(!ok){ nvm_unmap(&g_uv,sva,0x1000); nvm_unmap(&g_uv,zva,0x1000); } }
    nvm_tlb_flush(g_uv.pd3,0);
    if(!ok){ if(pv)nvgsp_ce_unmap(pv,(u64)N3_PBW*4); if(sv)nvgsp_ce_unmap(sv,0x1000); if(vv)nvgsp_ce_unmap(vv,(u64)N3_VBV*32); if(sh)nvv_free(sh); if(ze)nvv_free(ze); return n3_fail("the 3D buffers and shaders could not be set up in the GPU's memory"); }
    N3.sh_vram=sh; N3.zero_vram=ze; N3.sh_va=sva; N3.zero_va=zva; N3.pb_va=pv; N3.sem_va=sv; N3.vb_va=vv; N3.gen=g_ce_gen; return 1; }
static int n3_surf(u32 w,u32 h){
    if(w<16||h<16||w>2048||h>2048)return n3_fail("3D target size out of range");
    if(N3.ct_va&&N3.w==w&&N3.h==h)return 1;
    n3_free_surf();
    u32 sw=(w+15u)&~15u, sh=(h+7u)&~7u; u64 len=((u64)sw*4*sh+0xFFFFULL)&~0xFFFFULL;
    u64 c=nvv_alloc_raw(len,0x10000), z=nvv_alloc_raw(len,0x10000); if(!c||!z){ if(c)nvv_free(c); if(z)nvv_free(z); return n3_fail("no VRAM for the 3D colour and depth targets"); }
    if(!N3.ct_res){ N3.ct_res=va_take(0x1010000ULL,0x10000); N3.zt_res=va_take(0x1010000ULL,0x10000); }
    u64 cv=N3.ct_res, zv=N3.zt_res;
    if(!nvm_map_kind(&g_uv,cv,c,len,0,0,0,6)||!nvm_map_kind(&g_uv,zv,z,len,0,0,0,6)){ nvm_unmap(&g_uv,cv,len); nvm_unmap(&g_uv,zv,len); nvv_free(c); nvv_free(z); return n3_fail("the 3D targets could not be mapped"); }
    nvm_tlb_flush(g_uv.pd3,0);
    N3.ct_vram=c; N3.zt_vram=z; N3.ct_va=cv; N3.zt_va=zv; N3.s_len=len; N3.w=w; N3.h=h; N3.sw=sw; N3.sh=sh; return 1; }
static void n3_state(void){
    n31(N3_SET_OBJECT,nvgsp_cls_cur()->threed);
    for(u32 i=0;i+1<sizeof(nv3d_init)/4;i+=2)n31(nv3d_init[i],nv3d_init[i+1]);
    n3m(N3_SET_VERTEX_STREAM_SUBSTITUTE_A,2); n3v((u32)(N3.zero_va>>32)); n3v((u32)N3.zero_va);
    n3m(N3_SET_SURFACE_CLIP_HORIZONTAL,2); n3v(N3.w<<N3V_SURFACE_CLIP_WIDTH_SHIFT); n3v(N3.h<<N3V_SURFACE_CLIP_HEIGHT_SHIFT);
    n3m(N3_SET_COLOR_TARGET_A,9); n3v((u32)(N3.ct_va>>32)); n3v((u32)N3.ct_va); n3v(N3.sw); n3v(N3.sh); n3v(N3V_CT_FORMAT_A8R8G8B8); n3v(N3V_CT_MEMORY_BL); n3v(1); n3v((N3.sw*4*N3.sh)>>2); n3v(0);
    n3m(N3_SET_ZT_A,5); n3v((u32)(N3.zt_va>>32)); n3v((u32)N3.zt_va); n3v(N3V_ZT_FORMAT_ZF32); n3v(0); n3v((N3.sw*4*N3.sh)>>2);
    n3m(N3_SET_ZT_SIZE_A,3); n3v(N3.sw); n3v(N3.sh); n3v(N3V_ZT_SIZE_C);
    n31(N3_SET_ZT_SELECT,N3V_ZT_SELECT_1);
    n3m(N3_SET_VIEWPORT_SCALE_X,3); n3v(n3_fu((float)N3.w*0.5f)); n3v(n3_fu(-(float)N3.h*0.5f)); n3v(n3_fu(1.0f));
    n3m(N3_SET_VIEWPORT_OFFSET_X,3); n3v(n3_fu((float)N3.w*0.5f)); n3v(n3_fu((float)N3.h*0.5f)); n3v(n3_fu(0.0f));
    n3m(N3_SET_VIEWPORT_CLIP_HORIZONTAL,4); n3v(N3.w<<N3V_VIEWPORT_CLIP_WIDTH_SHIFT); n3v(N3.h<<N3V_VIEWPORT_CLIP_HEIGHT_SHIFT); n3v(n3_fu(0.0f)); n3v(n3_fu(1.0f));
    n31(N3_SET_VERTEX_ATTRIBUTE_A,N3V_VA_F4); n31(N3_SET_VERTEX_ATTRIBUTE_A+4,N3V_VA_F4|(16u<<N3V_VA_OFFSET_SHIFT));
    n3m(N3_SET_VERTEX_STREAM_A_FORMAT,3); n3v(N3V_VS_FORMAT_ENABLE|32u); n3v((u32)(N3.vb_va>>32)); n3v((u32)N3.vb_va);
    n3m(N3_SET_VERTEX_STREAM_SIZE_A,2); n3v(0); n3v(N3_VBV*32u);
    u32 pv=N3_SET_PIPELINE_SHADER+N3V_IDX_VERTEX*N3V_PIPE_STRIDE, pp=N3_SET_PIPELINE_SHADER+N3V_IDX_PIXEL*N3V_PIPE_STRIDE, d=N3_SET_PIPELINE_REGISTER_COUNT-N3_SET_PIPELINE_SHADER, a=N3_SET_PIPELINE_PROGRAM_ADDRESS_A-N3_SET_PIPELINE_SHADER;
    n31(pv,N3V_SHADER_VERTEX); n3m(pv+a,2); n3v((u32)(N3.sh_va>>32)); n3v((u32)N3.sh_va); n3m(pv+d,2); n3v(NV3D_VS_GPRS); n3v(0);
    u64 fa=N3.sh_va+N3_FS_OFF;
    n31(pp,N3V_SHADER_PIXEL); n3m(pp+a,2); n3v((u32)(fa>>32)); n3v((u32)fa); n3m(pp+d,2); n3v(NV3D_FS_GPRS); n3v(4);
    n31(N3_SET_PRIMITIVE_TOPOLOGY,N3V_TOPOLOGY_TRIANGLELIST); }
static void n3_clear(float r,float g,float b){
    n31(N3_SET_CLEAR_SURFACE_CONTROL,N3V_CLEAR_CONTROL);
    n3m(N3_SET_CLEAR_RECT_HORIZONTAL,2); n3v(N3.w<<N3V_CLEAR_RECT_XMAX_SHIFT); n3v(N3.h<<N3V_CLEAR_RECT_YMAX_SHIFT);
    n3m(N3_SET_COLOR_CLEAR_VALUE,4); n3v(n3_fu(r)); n3v(n3_fu(g)); n3v(n3_fu(b)); n3v(n3_fu(1.0f));
    n31(N3_SET_Z_CLEAR_VALUE,n3_fu(1.0f));
    n31(N3_CLEAR_SURFACE,N3V_CLEAR_ALL); }
static int n3_submit(void){
    if(N3.nv){ n3m(N3_SET_DRAW_CONTROL_A,2); n3v(N3V_DRAW_CONTROL_A); n3v(1); n3m(N3_DRAW_VERTEX_ARRAY_BEGIN_END_A,2); n3v(0); n3v(N3.nv); N3.tris+=N3.nv/3; }
    if(N3.over){ N3.over=0; N3.n=0; N3.nv=0; return n3_fail("3D command buffer overflow"); }
    if(N3.gen!=g_ce_gen||!nvgsp_ce_ready()){ N3.n=0; N3.nv=0; return n3_fail("the NVIDIA card was restarted during a 3D frame"); }
    if(g_gr_busy||!ch_ready(0)){ N3.n=0; N3.nv=0; return n3_fail("the graphics channel is busy"); }
    g_gr_busy=1; u32 seq=++N3.seq; if(!seq)seq=N3.seq=1; N3.sem[0]=0; cpu_mb();
    n3m(0x005C,5); n3v((u32)N3.sem_va&0xFFFFFFFCu); n3v((u32)(N3.sem_va>>32)); n3v(seq); n3v(0); n3v(1u|(1u<<20));
    cflush(N3.vb,(u64)N3.nv*32); cflush(N3.pb,(u64)N3.n*4); ch_kick(0,N3.pb_va,(int)N3.n); N3.subs++;
    u32 n=N3.n; N3.n=0; N3.nv=0; (void)n;
    u64 t0=hr_ns(); while(N3.sem[0]!=seq){ if(hr_ns()-t0>500000000ULL){ N3.timeouts++; g_gr_busy=0; ch_hung(0,N3.sem,seq); N3.good=0; nv_auto_need("a 3D batch timed out"); return n3_fail("the 3D engine did not finish a batch within 500 ms"); } cpu_pause(); }
    N3.last_us=(u32)((hr_ns()-t0)/1000ULL); g_gr_busy=0; return 1; }
int nv3d_begin(u32 w,u32 h,float r,float g,float b){
    N3.why[0]=0; N3.open=0;
    if(!n3_setup()||!n3_surf(w,h))return 0;
    if(g_gr_busy||!ch_ready(0))return n3_fail("the graphics channel is busy");
    N3.n=0; N3.nv=0; N3.over=0; n3_state(); n3_clear(r,g,b); N3.open=1; return 1; }
void nv3d_tri(void* ctx,const float* v){
    (void)ctx; if(!N3.open)return;
    if(N3.nv+3>N3_VBV){ if(!n3_submit()){ N3.open=0; return; } n3_state(); }
    float* d=N3.vb+(u64)N3.nv*8; for(int i=0;i<24;i++)d[i]=v[i]; N3.nv+=3; }
int nv3d_end(void){ if(!N3.open)return 0; N3.open=0; if(!n3_submit())return 0; N3.frames++; return 1; }
static void n3_src(nv2d_surf* s){ s->va=N3.ct_va; s->pitch=N3.sw*4; s->w=N3.sw; s->h=N3.sh; s->fmt=0xCF; s->vram=1; s->bl=1; }
int nv3d_to_screen(int dx,int dy,int scale){
    nv2d_surf d, s; if(!N3.ct_va||N3.gen!=g_ce_gen||!nvgsp_ce_ready()||scale<1||!nv2d_screen(&d))return 0; n3_src(&s);
    return nv2d_blit(&d,dx,dy,(int)N3.w*scale,(int)N3.h*scale,&s,0,0,(int)N3.w,(int)N3.h,NV2D_COPY); }
int nv3d_read(u32* out,u32 pitch_px){
    if(!N3.ct_va||N3.gen!=g_ce_gen||!nvgsp_ce_ready()||pitch_px<N3.w)return 0; u64 len=(u64)pitch_px*4*N3.h; u64 va=nvgsp_ce_map((u64)out,len); if(!va)return 0;
    nv2d_surf d, s; d.va=va; d.pitch=pitch_px*4; d.w=pitch_px; d.h=N3.h; d.fmt=0xCF; d.vram=0; d.bl=0; n3_src(&s);
    int r=nv2d_blit(&d,0,0,(int)N3.w,(int)N3.h,&s,0,0,(int)N3.w,(int)N3.h,NV2D_COPY);
    nvgsp_ce_unmap(va,len); if(r)cflush(out,len); return r; }
static void n3_v(float* d,float x,float y,float z,float r,float g,float b){ d[0]=x; d[1]=y; d[2]=z; d[3]=1.0f; d[4]=r; d[5]=g; d[6]=b; d[7]=1.0f; }
static int n3_near(u32 p,u32 want,int tol){ return px_near(p&0xFFFFFFu,want&0xFFFFFFu,tol); }
int nv3d_selftest(void){
    nvl_s("[21] 3D engine: block-linear colour and depth targets, vertex and pixel shader, depth test, read back through the 2D engine"); nvl_nl();
    N3.tested=1; N3.good=0;
    if(G.state<7){ nvl_s("  the NVIDIA card is not running - run nvgsp all first"); nvl_nl(); return n3_fail("the NVIDIA card is not running - run nvgsp all first"); }
    if(!nv2d_ready()){ nvl_s("  the 2D engine is not verified, so 3D pictures cannot be checked or shown"); nvl_nl(); return n3_fail("the 2D engine is not verified, so 3D pictures cannot be checked or shown"); }
    static u32 out[64*64]; float t[24];
    if(!nv3d_begin(64,64,0.25f,0.5f,0.75f)){ nvl_s("  "); nvl_s(N3.why); nvl_nl(); return 0; }
    n3_v(t,-1.0f,-1.0f,0.25f,0,1.0f,0); n3_v(t+8,0.0f,-1.0f,0.25f,0,1.0f,0); n3_v(t+16,-1.0f,1.0f,0.25f,0,1.0f,0); nv3d_tri(0,t);
    n3_v(t,-1.0f,-1.0f,0.5f,1.0f,0,0); n3_v(t+8,1.0f,-1.0f,0.5f,1.0f,0,0); n3_v(t+16,-1.0f,0.0f,0.5f,1.0f,0,0); nv3d_tri(0,t);
    n3_v(t,0.0f,0.0f,0.75f,0,0,0); n3_v(t+8,1.0f,0.0f,0.75f,1.0f,1.0f,1.0f); n3_v(t+16,1.0f,1.0f,0.75f,1.0f,1.0f,1.0f); nv3d_tri(0,t);
    if(!nv3d_end()){ nvl_s("  "); nvl_s(N3.why); nvl_nl(); return 0; }
    for(int i=0;i<64*64;i++)out[i]=0x12345678u;
    if(!nv3d_read(out,64)){ nvl_s("  the 2D engine could not copy the 3D picture back"); nvl_nl(); return n3_fail("the 3D picture could not be read back"); }
    u32 bg=out[4*64+40], gr=out[58*64+6], rd=out[58*64+40], ov=out[50*64+10], g1p=out[24*64+56], g0p=out[30*64+36];
    int ok=n3_near(bg,0x4080BFu,3)&&n3_near(gr,0x00FF00u,3)&&n3_near(rd,0xFF0000u,3)&&n3_near(ov,0x00FF00u,3);
    int grad=((g1p>>16)&255u)>((g0p>>16)&255u)+60u&&((g1p>>16)&255u)==((g1p>>8)&255u)&&((g0p>>8)&255u)==(g0p&255u)&&((g0p>>16)&255u)<120u;
    nvl_s("  background "); nvl_x(bg); nvl_s(" green "); nvl_x(gr); nvl_s(" red "); nvl_x(rd); nvl_s(" overlap "); nvl_x(ov); nvl_s(" gradient "); nvl_x(g0p); nvl_s(" -> "); nvl_x(g1p); nvl_nl();
    if(!ok||!grad){ nvl_s("  the picture read back does not match (expected 4080BF, 00FF00, FF0000, 00FF00 and a rising grey ramp) - 3D stays off"); nvl_nl(); return n3_fail("the 3D test picture did not match"); }
    N3.good=1; nvl_s("  3D test picture correct: clear, two triangles with depth order, interpolated colours"); nvl_nl(); return 1; }
int nv3d_ready(void){ return N3.good&&N3.gen==g_ce_gen&&nv2d_ready()&&nvgsp_ce_ready(); }
void nv3d_stats(u32* frames,u32* tris,u32* subs,u32* tmo,u32* us){ if(frames)*frames=N3.frames; if(tris)*tris=N3.tris; if(subs)*subs=N3.subs; if(tmo)*tmo=N3.timeouts; if(us)*us=N3.last_us; }
const char* nv3d_why(void){ return N3.why; }
int nv3d_tested(void){ return N3.tested&&N3.gen==g_ce_gen; }
