#include <stdio.h>
#include <string.h>
#include "drivers/video/nvidia/nvcursor.c"
u64 g_nv_bar0; u32 g_nv_boot0; int g_nv_ampere_on, g_nv_ada_on;
static u32 regs[0x100000/4]; static int nw; static u32 wo[64], wv[64]; static int idle_after=1, reads;
u32 nvr32(u32 off){ reads++; if(off>=NVC57A_STAT_BASE&&off<NVC57A_STAT_BASE+0x200)return reads>=idle_after?0x00040000u:0x00010000u; return (off-0x600000u)<sizeof regs?regs[(off-0x600000u)/4]:0; }
void nvw32(u32 off,u32 v){ if(nw<64){ wo[nw]=off; wv[nw]=v; } nw++; if((off-0x600000u)<sizeof regs)regs[(off-0x600000u)/4]=v; }
void nv_udelay(u32 us){ (void)us; }
static int st_head=-1, st_buf=1, st_core=1, st_upd=1, st_nm, st_ups; static u32 st_mv[16]; static u32 st_vb[0x2000];
int nvflip_head(void){ return st_head; }
int nvflip_cursor_buf(u32 len,u64* v,volatile u32** c){ if(!st_buf||len>sizeof st_vb)return 0; *v=0x40000000ULL; *c=st_vb; return 1; }
int nvflip_core_methods(const u32* mv,int n){ if(!st_core||n>8)return 0; memcpy(st_mv,mv,(size_t)n*8); st_nm=n; return 1; }
int nvflip_core_update(u32 m){ (void)m; st_ups++; return st_upd; }
static u32 bk[128*128], wh[128*128], img[64*64];
static void clr(void){ for(int i=0;i<128*128;i++){ bk[i]=0; wh[i]=0xFFFFFFu; } }
static void px(int x,int y,u32 b,u32 w){ bk[y*128+x]=b; wh[y*128+x]=w; }
static int fails, checks;
#define CK(c,msg) do{ checks++; if(!(c)){ fails++; printf("FAIL %s\n",msg); } }while(0)
int main(void){
    nvcur_m m[8]; u32 pb[16];
    int n=nvcur_set_mthds(m,8,1,0xF0000001u,0x123400,64,3,5);
    CK(n==4,"set count");
    CK(m[0].mthd==0x249Cu&&m[0].data==(0x80000000u|0xCFu|(1u<<8)|(3u<<12)|(5u<<20)),"HEAD_SET_CONTROL_CURSOR head1");
    CK(m[1].mthd==0x24A0u&&m[1].data==0x000072FFu,"CURSOR_COMPOSITION blend K1/NEG_K1_TIMES_SRC");
    CK(m[2].mthd==0x2488u&&m[2].data==0xF0000001u,"CONTEXT_DMA_CURSOR");
    CK(m[3].mthd==0x2490u&&m[3].data==0x1234u,"OFFSET_CURSOR >>8");
    CK(nvcur_set_mthds(m,8,0,1,0x180,64,0,0)<0,"unaligned offset refused");
    CK(nvcur_set_mthds(m,8,0,1,0,48,0,0)<0,"bad size refused");
    CK(nvcur_set_mthds(m,8,4,1,0,64,0,0)<0,"bad head refused");
    CK(nvcur_set_mthds(m,8,0,1,0,32,32,0)<0,"hotspot outside refused");
    CK(nvcur_layout(32)==0&&nvcur_layout(64)==1&&nvcur_layout(128)==2&&nvcur_layout(256)==3,"layouts");
    n=nvcur_clr_mthds(m,8,0);
    CK(n==2&&m[0].mthd==0x209Cu&&m[0].data==0xCFu&&m[1].mthd==0x2088u&&m[1].data==0,"clear sequence");
    u32 o=nvcur_emit(pb,16,m,n);
    CK(o==4&&pb[0]==((1u<<18)|0x209Cu)&&pb[1]==0xCFu&&pb[2]==((1u<<18)|0x2088u),"push encoding");
    CK(nvcur_emit(pb,3,m,n)==0,"push overflow refused");
    CK(nvcur_user(0)==0x690000u+72u*0x1000u&&nvcur_user(1)==0x690000u+73u*0x1000u,"cursor channel user area");
    CK(nvcur_point_word(-1,2)==0x0002FFFFu,"point word signed x");
    CK(nvcur_chan_init(0)<0,"off by default");
    CK(nvcur_gen_ok(0x164000A1u)&&nvcur_gen_ok(0x168000A1u)&&!nvcur_gen_ok(0x174000A1u)&&!nvcur_gen_ok(0x194000A1u)&&!nvcur_gen_ok(0x140000A1u),"Turing only while gpu ampere/ada are off");
    g_nv_ampere_on=1; CK(nvcur_gen_ok(0x172000A1u)&&nvcur_gen_ok(0x177000A1u)&&!nvcur_gen_ok(0x170000A1u)&&!nvcur_gen_ok(0x194000A1u),"GA102-GA107 with gpu ampere on, not GA100");
    g_nv_ada_on=1; CK(nvcur_gen_ok(0x192000A1u)&&nvcur_gen_ok(0x197000A1u)&&!nvcur_gen_ok(0x195000A1u)&&!nvcur_gen_ok(0x1A2000A1u),"AD102-AD107 with gpu ada on");
    g_nv_ampere_on=0; g_nv_ada_on=0;
    g_nvcur_enable=1; CK(nvcur_chan_init(0)<0,"no BAR0 refused");
    g_nv_bar0=0xF0000000u; g_nv_boot0=0x16400000u; nw=0; reads=0; idle_after=3;
    CK(nvcur_chan_init(0)==0&&g_nvcur_on[0],"init ok on Turing");
    CK(wo[0]==NVC57A_CTRL_BASE+73u*4u&&wv[0]==1,"init writes channel control 73");
    nw=0; CK(nvcur_move(0,100,200)==0,"move");
    CK(nw==2&&wo[0]==nvcur_user(0)+0x208u&&wv[0]==(100u|(200u<<16))&&wo[1]==nvcur_user(0)+0x200u&&wv[1]==1,"point then update");
    nvcur_chan_fini(0); CK(!g_nvcur_on[0]&&nvcur_move(0,1,1)<0,"fini");
    g_nv_boot0=0x19400000u; CK(nvcur_chan_init(0)<0,"non-Turing refused");
    g_nv_boot0=0x16400000u; idle_after=1<<30; reads=0; CK(nvcur_chan_init(1)<0&&!g_nvcur_on[1],"channel that never idles: bounded wait, falls back");
    { int w=-1,hx=-1,hy=-1; clr(); CK(nvcur_img_build(bk,wh,128,128,64,64,img,&w,&hx,&hy)==0,"empty shape -> nothing to show");
      px(64,64,0xFFFFFFu,0xFFFFFFu); px(70,80,0,0x808080u); px(66,66,0x404040u,0xFFFFFFu);
      CK(nvcur_img_build(bk,wh,128,128,64,64,img,&w,&hx,&hy)==1&&w==32&&hx==0&&hy==0,"small arrow -> 32x32, hotspot at its tip");
      CK(img[0]==0xFFFFFFFFu&&img[16*32+6]==0x7F000000u&&img[2*32+2]==0x40404040u&&img[1]==0&&img[31*32+31]==0,"ARGB premultiplied: opaque, 50% shadow, transparent");
      px(64,64,0,0xFFFFFFu); px(50,50,0xFFFFFFu,0xFFFFFFu); px(78,78,0xFFFFFFu,0xFFFFFFu);
      CK(nvcur_img_build(bk,wh,128,128,64,64,img,&w,&hx,&hy)==1&&w==32&&hx==14&&hy==14&&img[0]==0xFFFFFFFFu&&img[28*32+28]==0xFFFFFFFFu,"centred shape (resize arrows) -> hotspot inside the image");
      px(100,64,0xFFFFFFu,0xFFFFFFu); CK(nvcur_img_build(bk,wh,128,128,64,64,img,&w,&hx,&hy)==1&&w==64&&hx==14,"wider shape -> 64x64");
      px(127,127,0xFFFFFFu,0xFFFFFFu); CK(nvcur_img_build(bk,wh,128,128,64,64,img,&w,&hx,&hy)<0,"shape larger than 64x64 refused (software cursor)");
      clr(); px(64,64,0x00FF0000u,0x00FF8080u); nvcur_img_build(bk,wh,128,128,64,64,img,&w,&hx,&hy); CK(img[0]==0x7F7F0000u,"colour clamped to alpha"); }
    { int x,y; nvcur_clip(-5,700,64,1024,600,&x,&y); CK(x==-5&&y==599,"clip: partly off left kept, bottom clamped");
      nvcur_clip(5000,-200,64,1024,600,&x,&y); CK(x==1023&&y==-63,"clip: right clamped, top keeps one row"); nvcur_clip(10,20,32,1024,600,&x,&y); CK(x==10&&y==20,"clip: inside unchanged"); }
    { int w,hx,hy; clr(); for(int y=0;y<40;y++)px(64,64+y,0xFFFFFFu,0xFFFFFFu); nvcur_img_build(bk,wh,128,128,64,64,img,&w,&hx,&hy);
      g_nvcur_enable=1; g_nv_boot0=0x16400000u; idle_after=1; reads=0; st_head=-1;
      CK(nvcur_hw_shape(img,w,hx,hy)<0&&!nvcur_hw_active(),"no flip head -> refused, software cursor");
      st_head=0; nvcur_chan_fini(0); st_ups=0; CK(nvcur_hw_shape(img,w,hx,hy)==0&&nvcur_hw_active()&&g_nvcur_on[0],"shape set (cursor channel started on the flip head)");
      CK(st_nm==4&&st_mv[0]==0x209Cu&&st_mv[1]==(0x80000000u|0xCFu|(1u<<8))&&st_mv[2]==0x20A0u&&st_mv[4]==0x2088u&&st_mv[5]==0xF0000001u&&st_mv[6]==0x2090u&&st_mv[7]==0x400000u&&st_ups==1,"core methods: control (64x64, hotspot 0 like nouveau), composition, VRAM ctxdma, offset; one update");
      CK(st_vb[0]==0xFFFFFFFFu&&st_vb[39*64]==0xFFFFFFFFu&&st_vb[1]==0&&st_vb[40*64]==0,"image in VRAM slot 0, pitch 64 pixels");
      CK(nvcur_hw_shape(img,w,hx,hy)==0&&st_mv[7]==0x400040u&&st_vb[0x1000]==0xFFFFFFFFu,"next shape goes to slot 1 (no tearing)");
      nw=0; CK(nvcur_hw_move(-10,599,1024,600)==0&&wv[0]==(0xFFF6u|(599u<<16))&&wv[1]==1,"move: point = position - hotspot, clipped, update");
      CK(nvcur_hw_shape(0,0,0,0)==0&&st_nm==2&&st_mv[1]==0xCFu&&nvcur_hw_active(),"hidden shape (DOOM has the mouse) disables the cursor");
      nw=0; CK(nvcur_hw_move(5,5,1024,600)==0&&nw==0,"hidden cursor is not moved");
      st_buf=0; CK(nvcur_hw_shape(img,w,hx,hy)<0,"no VRAM -> error (falls back)"); st_buf=1;
      st_core=0; CK(nvcur_hw_shape(img,w,hx,hy)<0,"core channel full -> error"); st_core=1;
      st_upd=0; CK(nvcur_hw_shape(img,w,hx,hy)<0,"core update timeout -> error"); st_upd=1;
      CK(nvcur_hw_shape(img,48,0,0)<0,"bad size refused");
      nvcur_hw_off(); CK(!nvcur_hw_active()&&st_mv[1]==0xCFu&&nvcur_hw_move(1,1,10,10)<0,"off clears the cursor");
      nvcur_hw_shape(img,w,hx,hy); st_head=-1; CK(!nvcur_hw_active()&&nvcur_hw_move(1,1,10,10)<0,"flip head gone -> inactive"); nvcur_hw_off(); }
    printf("%d checks, %s\n",checks,fails?"FAIL":"ALL PASS"); return fails!=0; }
