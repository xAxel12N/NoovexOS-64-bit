#include "include/nvx/base.h"
#include "include/nvx/drivers/video/fb/framebuffer.h"
#include "include/nvx/graphics/theme/theme.h"
#include "include/nvx/graphics/ui/hwcursor.h"
#include "include/nvx/graphics/ui/curshape.h"
#include "include/nvx/graphics/ui/outputs.h"
#include "include/nvx/drivers/video/nvidia/nvcursor.h"
#include "include/nvx/drivers/video/nvidia/nvflip.h"
#include "include/nvx/drivers/video/nvidia/nvsettings.h"

extern int mx, my, g_doom_mcap, g_quake_mcap, g_rz_hover;
int g_hwcur_drawing=0, g_hwcur_shapes=0, g_hwcur_sw=0;
static int hc_fail, hc_key_ok, hc_en, hc_hid; static u32 hc_key[5];
static u32 hc_bk[HWCUR_RB*HWCUR_RB], hc_wh[HWCUR_RB*HWCUR_RB], hc_img[64*64];
static void hc_render(u32* b,u32 bg){
    for(int i=0;i<HWCUR_RB*HWCUR_RB;i++)b[i]=bg;
    u32* s0=g_scene; u32 w0=W, h0=H, sw0=SW; int x0=mx, y0=my, gf=g_gfx;
    int rt=g_rt; g_scene=b; W=H=SW=HWCUR_RB; mx=my=HWCUR_RB/2; g_gfx=0; g_rt=0; g_hwcur_drawing=1; g_scene_quiet++;
    draw_cursor();
    g_scene_quiet--; g_hwcur_drawing=0; g_rt=rt; g_gfx=gf; mx=x0; my=y0; g_scene=s0; W=w0; H=h0; SW=sw0; }
static int hc_off(const char* why){
    nvcur_hw_off(); hc_key_ok=0;
    if(why){ hc_fail=1; if(nvset_try_begin(NVSET_CURSOR))nvset_try_end(NVSET_CURSOR,0,why); }
    return 0; }
int hwcur_step(void){
    if(g_hwcur_drawing)return 0;
    if(!g_nvcur_enable){ hc_en=0; hc_fail=0; return nvcur_hw_head()>=0?hc_off(0):0; }
    if(!hc_en){ hc_en=1; hc_fail=0; }
    if(hc_fail||nvflip_head()<0||!nvset_allowed(NVSET_CURSOR))return nvcur_hw_head()>=0?hc_off(0):0;
    if(out_on2(mx,my)){ if(!hc_hid&&nvcur_hw_head()>=0){ if(nvcur_hw_shape(0,0,0,0))return hc_off("the hardware cursor could not be hidden (core channel) - software cursor"); hc_hid=1; hc_key_ok=0; } return 0; }
    hc_hid=0;
    int sh=g_cur_shape; u32 k[5]={(u32)g_cursor,(u32)g_rz_hover,(u32)g_doom_mcap|((u32)g_quake_mcap<<1),ACC,(u32)sh|((sh==CUR_WAIT||sh==CUR_BUSY)?(u32)cur_busy_phase()<<8:0u)};
    if(!hc_key_ok||!nvcur_hw_active()||k[0]!=hc_key[0]||k[1]!=hc_key[1]||k[2]!=hc_key[2]||k[3]!=hc_key[3]||k[4]!=hc_key[4]){
        int w=0, hx=0, hy=0; hc_render(hc_bk,0); hc_render(hc_wh,0xFFFFFFu);
        int r=nvcur_img_build(hc_bk,hc_wh,HWCUR_RB,HWCUR_RB,HWCUR_RB/2,HWCUR_RB/2,hc_img,&w,&hx,&hy);
        if(nvcur_hw_shape(r>0?hc_img:0,w,hx,hy))return hc_off("the hardware cursor image could not be set (core channel) - software cursor");
        for(int i=0;i<5;i++)hc_key[i]=k[i]; hc_key_ok=1; g_hwcur_shapes++; g_hwcur_sw=r<0; }
    if(g_hwcur_sw)return 0;
    if(nvcur_hw_move(mx,my,(int)W,(int)H))return hc_off("the hardware cursor did not move (cursor channel) - software cursor");
    return 1; }
