#ifndef NVX_DRIVERS_VIDEO_FRAMEBUFFER_H
#define NVX_DRIVERS_VIDEO_FRAMEBUFFER_H

#include "include/nvx/base.h"

#define MAXW 1920
#define MAXH 1080
#define rgb(r,g,b) ( ((u32)(u8)(r)<<16) | ((u32)(u8)(g)<<8) | (u32)(u8)(b) )

extern int g_fb_rgb;
extern u32 g_uefi_ram_mb;
extern u32* g_scene;
extern u32 SCENE_STATIC[MAXW*MAXH];
extern u32* BG;
extern u32 BG_STATIC[MAXW*MAXH];
extern volatile int g_gq_inb, g_gfx_run;
void gfx_sync(void);
extern u32 g_scene_acc;
extern volatile int g_gq_capon; extern int g_scene_quiet; extern u32 g_scene_wr;
static inline u32* scene_ptr(void){ g_scene_acc++; if(!g_scene_quiet)g_scene_wr++; if(g_gq_inb|g_gfx_run|g_gq_capon)gfx_sync(); return g_scene; }
#define SCENE (scene_ptr())
extern u8 *FBB;
extern u32 PITCHB, W, H, SW;
extern u8 BPP;
extern u32 BYPP;
extern u8 *FBB2;
extern u32 PITCHB2, W2, H2;
extern u8 BYPP2;
extern int g_fb2_ok, g_fb2_rgb, g_mirror2;
void blit_mirror2(void);
extern int g_gl[95], g_gw[95], g_gm_ready;

void copy_swap32(u32* d,const u32* s,int n);
void fill_rect(int x,int y,int w,int h,u32 c);
u32 isqrt32(u32 v);
int cut(int i,int j,int w,int h,int r,int m);
void fill_round(int x,int y,int w,int h,int r,int m,u32 c);
void shadow_soft(int wx,int wy,int ww,int wh,int wr);
void fill_circle(int cx,int cy,int r,u32 c);
int slen(const char*s);
void dchar(int x,int y,char ch,u32 c,int s);
int dstr(int x,int y,const char*s,u32 c,int sc);
void dstr_ctr(int cx,int y,const char*s,u32 c,int sc);
void gmetrics(void);
int gadv(char ch,int s);
int pw_sub(const char*str,int a,int b,int s);
int pw_str(const char*str,int s);
int pdstr(int x,int y,const char*str,u32 c,int s);
int pdsub(int x,int y,const char*str,int a,int b,u32 c,int s);
void pdstr_ctr(int cx,int y,const char*s,u32 c,int sc);
void blit_all(void);

extern int g_gfx; void gfx_px(int x,int y,u32 c,int a);
static inline void putpx(int x,int y,u32 c){ if(g_gfx){ gfx_px(x,y,c,256); return; } if((u32)x<W&&(u32)y<H) g_scene[(u32)y*SW+x]=c; }
static inline u32 mix(u32 a,u32 b,int t){
    int ar=(a>>16)&255,ag=(a>>8)&255,ab=a&255,br=(b>>16)&255,bg=(b>>8)&255,bb=b&255;
    return rgb((u8)((ar*(256-t)+br*t)>>8),(u8)((ag*(256-t)+bg*t)>>8),(u8)((ab*(256-t)+bb*t)>>8));
}
static inline void pblend(int x,int y,u32 c,int a){ if(g_gfx){ gfx_px(x,y,c,a); return; } if((u32)x<W&&(u32)y<H){ u32*p=&g_scene[(u32)y*SW+x]; *p=mix(*p,c,a);} }

#endif
