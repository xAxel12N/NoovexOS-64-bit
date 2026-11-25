#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "graphics/ui/outputs.c"
#include "graphics/ui/outputs_present.c"
static int fails, checks;
#define CK(c,msg) do{ checks++; if(!(c)){ fails++; printf("FAIL %s\n",msg); } }while(0)
volatile u64 g_ticks=1000; int g_gfx=0, g_ws=0, g_state=ST_DESKTOP, mx=10, my=10, focus=-1, g_rt=0, g_hwcur_drawing=0, drag=-1, g_snap=0, g_ctx=0, ctx_x=0, ctx_y=0;
static int hook_rt, hook_n; static void hook(int s,int x,int y,int w,int h){ (void)x; (void)y; (void)w; (void)h; hook_n++; if(s)hook_rt++; } void (*g_gq_mark_hook)(int,int,int,int,int);
static int cdx, cdy, cdW, cd2, cdn, ccx, ccy, ccw, ccn; void ctx_draw(void){ cdn++; cdx=ctx_x; cdy=ctx_y; cdW=(int)W; cd2=g_ctx2; fill_rect(ctx_x,ctx_y,10,10,0xC0C0C0); }
int ctx_click(int px,int py){ ccn++; ccx=px; ccy=py; ccw=ctx_x; int w=999,h=999; out_ctx_bounds(&w,&h); return w==256&&h==160?1:-1; }
static int allow=1; int nvset_allowed(int f){ (void)f; return allow; }
u32 W=320, H=200, SW=320, W2=256, H2=160, PITCHB2=256*4; u8 BYPP2=4; u8* FBB2; int g_fb2_ok=1, g_fb2_rgb=0, g_mirror2=1;
u32 S1[320*200], B1[320*200]; u32* g_scene=S1; u32* BG=B1; u32 g_scene_acc, g_scene_wr; int g_scene_quiet; volatile int g_gq_inb, g_gfx_run, g_gq_capon;
Win WN[NWIN]; const char* APPNAME[NWIN];
void* kmalloc(unsigned long n){ return malloc(n); } void kfree(void* p){ free(p); }
void gfx_sync(void){} void copy_swap32(u32* d,const u32* s,int n){ memcpy(d,s,(size_t)n*4); }
static int mirrors; void blit_mirror2(void){ mirrors++; }
void fill_rect(int x,int y,int w,int h,u32 c){ if(x<0){w+=x;x=0;} if(y<0){h+=y;y=0;} if(x+w>(int)W)w=(int)W-x; if(y+h>(int)H)h=(int)H-y; for(int j=0;j<h;j++)for(int i=0;i<w;i++)g_scene[(y+j)*SW+x+i]=c; }
static int frames[NWIN]; void win_frame(int i,const char* t){ (void)t; frames[i]++; fill_rect(WN[i].x,WN[i].y,WN[i].w,TITLEH,0x00FF00); }
static int drawn[NWIN], dx_[NWIN], dmx, dW; static u32* dsc;
static int dgfx, dwr; void draw_app_marked(int i){ drawn[i]++; dx_[i]=WN[i].x; dsc=g_scene; dW=(int)W; dmx=mx; dgfx=g_gfx; u32 w0=g_scene_wr; (void)SCENE; dwr+=g_scene_wr!=w0; fill_rect(WN[i].x,WN[i].y,WN[i].w,WN[i].h,0x112233+i); }
void draw_cursor(void){ fill_rect(mx,my,3,3,0xFFFFFF); }
static int tries, ends, lastok; static const char* lasterr;
int nvset_try_begin(int f){ (void)f; tries++; return 1; } void nvset_try_end(int f,int ok,const char* e){ (void)f; ends++; lastok=ok; lasterr=e; }
static int notes, notest; void nvset_note(int f,int st,const char* e){ (void)f; (void)e; notes++; notest=st; }
static char cfg[4]; static int cfgn;
int fs_find_name(const char* n){ (void)n; return cfgn?0:-1; } char* fsd(int k){ (void)k; return cfg; }
int fs_save_bin(const char* n,const u8* b,int l,int f){ (void)n; (void)f; memcpy(cfg,b,(size_t)l); cfgn=l; return 0; } void nvx3_sync_all(void){}
static u32 fb2(int x,int y){ return ((u32*)FBB2)[y*256+x]; }
int main(void){
    FBB2=calloc(256*160,4); for(int i=0;i<320*200;i++)B1[i]=0x0000AA;
    for(int i=0;i<NWIN;i++)APPNAME[i]="w";
    WN[1]=(Win){20,20,100,80,1,1,0,0,0,0,0,0,0}; WN[2]=(Win){400,30,120,90,1,2,0,0,0,0,0,0,0}; WN[3]=(Win){250,40,100,60,1,3,0,0,0,0,0,0,0};
    CK(display_mode_set(1)==0&&g_disp_extend&&!g_mirror2&&out_count()==2,"extend switches on with a second screen");
    CK(out1_skip_win(1)==0&&out1_skip_win(2)==1&&out1_skip_win(3)==0,"screen 1 draws its own windows and skips screen 2 windows");
    memset(frames,0,sizeof frames); out1_skip_win(2); CK(frames[2]==0,"a window fully on screen 2 is not drawn on screen 1");
    WN[4]=(Win){300,100,100,50,1,4,0,0,0,0,0,0,0}; memset(frames,0,sizeof frames); CK(out1_skip_win(4)==1&&frames[4]==1,"straddling screen 2 window: frame on screen 1");
    WN[4].open=0;
    mx=450; my=60; memset(drawn,0,sizeof drawn); memset(frames,0,sizeof frames);
    CK(blit_extend2()==0,"extended present works");
    CK(drawn[2]==1&&dx_[2]==80&&dsc!=S1&&dW==256&&dmx==130,"screen 2 window drawn with its contents into the second scene, local coordinates");
    CK(drawn[1]==0&&drawn[3]==0&&frames[3]==1,"screen 1 window crossing the edge: frame only on screen 2");
    CK(WN[2].x==400&&mx==450&&g_scene==S1&&W==320&&SW==320,"globals restored after the screen 2 pass");
    CK(fb2(85,60)==0x112235&&fb2(5,150)==0x0000AA,"second head shows the window contents and the wallpaper");
    CK(fb2(130,60)==0xFFFFFF,"cursor drawn on screen 2");
    mx=470; out2_cursor(); CK(fb2(130,60)==0x112235&&fb2(150,60)==0xFFFFFF,"cursor moves on screen 2 without a full redraw");
    mx=100; out2_cursor(); CK(fb2(150,60)==0x112235,"cursor leaves screen 2: old cursor removed");
    int px=450, py=60; mx=450; int k=out_enter(2,&px,&py); CK(k==1&&px==130&&WN[2].x==80&&mx==130,"click on screen 2 window reaches the app in local coordinates");
    out_leave(2,k,&px,&py); CK(px==450&&WN[2].x==400&&mx==450,"click translation undone");
    px=30; k=out_enter(1,&px,&py); CK(k==0&&px==30,"screen 1 windows untouched");
    g_ticks+=1; memset(drawn,0,sizeof drawn); blit_extend2(); CK(drawn[2]==0,"screen 2 redraw is rate limited");
    g_state=ST_LOGIN; mirrors=0; g_ticks+=10; blit_extend2(); CK(mirrors==1&&drawn[2]==0,"outside the desktop screen 2 mirrors");
    g_state=ST_DESKTOP;
    CK(display_arrange(OUT_LEFT)==0&&g_out[1].x==-256&&WN[2].x==400-320-256&&cfg[1]=='1',"arrangement left keeps screen 2 windows on screen 2 and is saved");
    CK(out1_skip_win(2)==1,"window still owned by screen 2 after rearranging");
    display_arrange(OUT_RIGHT); CK(WN[2].x==400,"back to the right");
    g_gfx=1; g_rt=5; g_gq_mark_hook=hook; hook_rt=0; hook_n=0; dwr=0; g_ticks+=10; memset(drawn,0,sizeof drawn); u32 wr0=g_scene_wr;
    blit_extend2(); CK(drawn[2]==1&&dgfx==0&&hook_n>0&&hook_rt==0&&dwr==0&&g_scene_wr==wr0,"GPU drawing on: screen 2 drawn in software without marking the GPU scene (render target) dirty");
    CK(g_rt==5&&g_gfx==1&&g_scene_quiet==0,"render target, GPU flag and quiet count restored");
    mx=450; my=60; hook_rt=0; out2_cursor(); CK(hook_rt==0&&g_hwcur_drawing==0,"cursor on screen 2 with GPU drawing on: no GPU scene marks, hardware cursor untouched");
    g_gfx=0; g_rt=0; g_gq_mark_hook=0;
    g_ctx=1; ctx_x=460; ctx_y=50; cdn=0; g_ticks+=10; blit_extend2(); CK(out_ctx_on2()&&cdn==1&&cdx==140&&cdy==50&&cdW==256&&cd2==1&&ctx_x==460,"right-click menu on screen 2 is drawn there in local coordinates");
    CK(out_ctx_click(470,60)==1&&ccn==1&&ccx==150&&ccy==60&&ccw==140&&ctx_x==460&&!g_ctx2,"click on a screen 2 menu reaches it in local coordinates, bounds of screen 2");
    ctx_x=100; CK(!out_ctx_on2(),"menu on screen 1 stays on screen 1"); g_ctx=0;
    { int o=-1; CK(out_snap_zone(575,80,320,200,&o)==2&&o==1,"snap: right edge of screen 2");
      CK(out_snap_zone(400,1,320,200,&o)==3&&o==1,"snap: top of screen 2 maximises there");
      CK(out_snap_zone(322,80,320,200,&o)==0,"snap: shared edge does not snap (screen 2 side)");
      CK(out_snap_zone(318,80,320,200,&o)==0&&o==0,"snap: shared edge does not snap (screen 1 side)");
      CK(out_snap_zone(1,20,320,200,&o)==4&&o==0,"snap: screen 1 left top corner unchanged");
      int bx,by,bw,bh; out_snap_rect(0,0,0,0,1,320,200,&bx,&by,&bw,&bh); CK(bx==326&&by==6&&bw==244&&bh==148,"snap area on screen 2 (6 px margin, no taskbar)");
      out_snap_rect(WN[2].x,WN[2].y,WN[2].w,WN[2].h,-1,320,200,&bx,&by,&bw,&bh); CK(bx==326,"hotkey snap uses the window's screen");
      out_snap_rect(WN[1].x,WN[1].y,WN[1].w,WN[1].h,-1,320,200,&bx,&by,&bw,&bh); CK(bx==6&&by==32&&bw==308&&bh==200-32-86,"screen 1 snap area unchanged");
      int dx=500, dy=50; out_desk_drop(&dx,&dy,320,200); CK(dx==319&&dy==50,"file dropped on screen 2 desktop lands on the screen 1 desktop"); }
    WN[2].max=1; WN[2].sx=400; WN[2].sy=30; WN[2].sw=120; WN[2].sh=90; WN[2].x=326; WN[2].y=6; WN[2].w=244; WN[2].h=148;
    g_fb2_ok=0; ends=0; mirrors=0; CK(blit_extend2()!=0&&!g_disp_extend&&g_mirror2&&ends==0,"screen 2 unplugged: mirrors, not counted as a failure");
    CK(WN[2].x+WN[2].w<=320&&out_of_rect(WN[2].x,WN[2].y,WN[2].w,WN[2].h)==0&&!WN[2].max,"windows of screen 2 come back to screen 1");
    CK(display_mode_set(1)!=0,"no second screen: extend refused");
    tries=0; CK(display_hotplug(1)==0&&tries==0&&!g_disp_extend,"still unplugged: nothing happens");
    g_fb2_ok=1; CK(display_hotplug(1)==1&&tries==1&&g_disp_extend&&WN[2].x==326&&WN[2].y==6&&WN[2].max&&WN[2].sx==400&&WN[2].sy==30&&WN[2].sw==120,"screen 2 plugged in again: extends and windows go back where they were on screen 2 (maximised again)");
    ends=0; g_ticks+=400; blit_extend2(); CK(ends==1&&lastok,"replug verified by the guard after 3 s");
    display_mode_set(0); CK(display_hotplug(1)==0&&!g_disp_extend,"after choosing mirror, a later replug does not extend");
    g_fb2_ok=0; display_mode_set(1);
    g_fb2_ok=1; cfgn=0; tries=0; g_ticks=600; display_cfg_tick(); CK(tries==1&&g_disp_extend,"boot: extend is on by default under the guard");
    g_ticks=1000; ends=0; mx=10; blit_extend2(); CK(ends==1&&lastok,"after 3 s of extended frames the guard reports success");
    display_mode_set(0); CK(!g_disp_extend&&g_mirror2&&out_count()==1,"mirror again");
    display_mode_set(1); g_fb2_ok=0; blit_extend2(); g_fb2_ok=1; tries=0; display_cfg_tick(); CK(tries==1&&g_disp_extend,"display tick re-extends after a replug");
    g_ticks=2000; ends=0; mx=10; blit_extend2(); CK(ends==1&&lastok&&!g_ext_try,"extend confirmed");
    ends=0; notes=0; u8* f2=FBB2; FBB2=0; out_fail("x"); FBB2=f2; CK(ends==0&&notes==1&&notest==NVSET_ST_FALLBACK&&!g_disp_extend,"runtime fallback after the guard confirmed extend is noted, not counted as a guard failure");
    display_mode_set(1); g_ext_try=1; ends=0; display_mode_set(0); CK(ends==1&&lastok&&!g_ext_try,"turning extend off during the 3 s check closes the guard try (no unfinished try at next boot)");
    g_ext_try=1; ends=0; notes=0; out_fail("y"); CK(ends==1&&!lastok&&notes==0,"failure during the 3 s check still counts as a guard failure");
    display_mode_set(1); g_ext_try=0; WN[2]=(Win){400,30,120,90,1,2,0,0,0,0,0,0,0}; ends=0; tries=0;
    CK(display_hpd(0)==1&&!g_fb2_ok&&!g_disp_extend&&g_mirror2&&ends==0&&out_of_rect(WN[2].x,WN[2].y,WN[2].w,WN[2].h)==0,"GSP-RM hot-plug: screen 2 unplugged, windows move to screen 1, no guard failure");
    CK(display_hpd(0)==0,"second unplug event ignored");
    CK(display_hpd(1)==2&&g_fb2_ok&&g_disp_extend&&tries==1&&WN[2].x==400&&WN[2].y==30,"GSP-RM hot-plug: screen 2 back, extends again and windows return to screen 2");
    CK(display_hpd(1)==0,"plug event without an earlier unplug does nothing");
    display_mode_set(0); g_ext_try=0; CK(display_hpd(0)==1&&!g_fb2_ok&&display_hpd(1)==2&&g_fb2_ok&&!g_disp_extend&&g_mirror2,"in mirror mode an unplug/replug only stops and restarts mirroring");
    printf("%d checks, %s\n",checks,fails?"FAIL":"ALL PASS"); return fails!=0; }
