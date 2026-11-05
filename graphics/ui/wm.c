#include "include/nvx/base.h"
#include "include/nvx/drivers/video/fb/framebuffer.h"
#include "include/nvx/net/http/http.h"
#include "include/nvx/graphics/theme/theme.h"
#include "include/nvx/graphics/ui/wm.h"
#include "include/nvx/apps/accessories/small_apps.h"
#include "include/nvx/apps/games/doom.h"
#include "include/nvx/core/focus.h"
#include "include/nvx/graphics/ui/desktop.h"
#include "include/nvx/drivers/video/fb/gfx.h"
/* =========================================================================
   Window manager
   ========================================================================= */

Win WN[NWIN];
int g_ws=0;                                   /* current workspace (0..3) */

/* ---- notifications: toast queue + history ---- */

Notif g_notif[NOTIF_MAX]; int g_notif_n=0; u64 g_toast_until=0; int g_toast_cur=-1;
int g_notifpanel=0;                            /* history panel open */
void notify_from(const char* app,const char* title,const char* body){
    if(!title)title=""; if(!app)app="System";
    int i=g_notif_n%NOTIF_MAX; int k=0; while(title[k]&&k<39){g_notif[i].title[k]=title[k];k++;} g_notif[i].title[k]=0;
    k=0; if(body){ while(body[k]&&k<71){g_notif[i].body[k]=body[k];k++;} } g_notif[i].body[k]=0;
    k=0; while(app[k]&&k<15){g_notif[i].app[k]=app[k];k++;} g_notif[i].app[k]=0;
    g_notif_n++;
    if(!focus_allows(app,title)){ g_notif[i].shown=1; return; }
    g_notif[i].shown=0; g_toast_cur=i; g_toast_until=0;
}
void notify(const char* title,const char* body){ notify_from("System",title,body); }
int focus=-1, ztop=1, drag=-1, dox=0, doy=0;
int g_ctxkind=0, g_ctxwin=-1;                 /* right-click menu: 0 desktop 1 window 2 files 4 tree */
int g_tctx_type=-1, g_tctx_val=-1;            /* tree ctx target: 0 folder,1 fs-file,2 curated */
int g_dctx_item=-1;                           /* desktop right-click target (app 0-4, or 100+slot) */
int g_prop=0, g_prop_app=-1, g_prop_slot=-1, g_prop_dir=-1; 
int g_submenu=0, g_sub_x=0, g_sub_y=0, g_subn=0;   /* hover submenu */
int g_fdrag=0, g_fdrag_slot=-1, g_fdrag_x=0, g_fdrag_y=0;   /* drag a tree file */
const char* g_subit[8]; char g_subparent[16]="";
int g_snap=0, g_resize=-1, r_sw0=0, r_sh0=0, r_mx0=0, r_my0=0, r_sx0=0, r_sy0=0, g_rflags=8|2, g_rz_hover=0;
int g_launcher=0, g_alttab=0, g_altsel=0, g_altlist[NWIN], g_altn=0, alt_prev=0;
int g_wallmode=2, wall_custom_slot=-1, g_clockpop=0, g_qs=0;
int g_rename=0, g_renslot=-1, g_renlen=0; char g_renbuf[64]="";
int g_clockx=0, g_qsx=0;
int g_marq=0, marq_x0=0, marq_y0=0;
char toast_msg[80]=""; u64 toast_t=0;
void toast(const char*m){ { extern int g_osd_kind; g_osd_kind=0; } int i=0; while(m[i]&&i<79){toast_msg[i]=m[i];i++;} toast_msg[i]=0; toast_t=g_ticks; }
const char *APPNAME[NWIN]={"Welcome","Terminal","Files","Editor","Calculator","Browser","Clock","Task Manager","Calendar","Snake","Flappy Bird","neofetch","Paint","DOOM","Settings","About","Registry Editor","Presentation","Logs","Terminal Finder","Video Player","X Display","Camera","Benchmark","Voice Recorder","Writer","Quake","Gears"};
   /* fwd - defined near compositor */
  
void open_win(int i){ int was=WN[i].open; WN[i].open=1; WN[i].min=0; WN[i].ws=g_ws; focus=i; WN[i].z=++ztop;
    if(!was){ if(i==W_SNAKE)snake_reset(); else if(i==W_FLAPPY)flappy_reset(); else if(i==W_CALENDAR)cal_open(); }
    if(!was)focus_app_started(i);
    if(!was && g_state==ST_DESKTOP){ win_anim(i,1); gfx_win_event(i,0); } }
static void focus_next(void){ int best=-1, bz=-1; for(int k=0;k<NWIN;k++) if(WN[k].open&&!WN[k].min&&WN[k].ws==g_ws&&WN[k].z>bz){ bz=WN[k].z; best=k; } focus=best; }
void close_win(int i){ if(WN[i].open&&!WN[i].min&&g_state==ST_DESKTOP){ win_anim(i,0); gfx_win_event(i,1); } { int wo=WN[i].open; WN[i].open=0; WN[i].min=0; if(focus==i)focus_next(); if(wo)focus_app_closed(i); } if(i==W_DOOM) g_doom_mcap=0; if(i==W_GPU3D){ extern void gpu3d_closed(void); gpu3d_closed(); } if(i==W_CAMERA){ extern void camera_closed(void); camera_closed(); } if(i==W_RECORDER){ extern void recorder_closed(void); recorder_closed(); } if(i==W_QUAKE){ extern void quake_closed(void); quake_closed(); } if(i==W_VIDEO){ extern void video_stop(void); video_stop(); } if(i==W_BROWSER){ extern void browser_media_pause(void); extern int web_busy(void); extern volatile int g_busy_esc; if(web_busy())g_busy_esc=1; browser_media_pause(); } }
void focus_win(int i){ if(!WN[i].open){ open_win(i); return; } if(WN[i].min)gfx_win_event(i,0); WN[i].min=0; focus=i; WN[i].z=++ztop; }
void win_min(int i){ if(!WN[i].min&&WN[i].open)gfx_win_event(i,2); WN[i].min=1; if(focus==i)focus_next(); }
void win_max(int i){ if(!WN[i].max){ WN[i].sx=WN[i].x;WN[i].sy=WN[i].y;WN[i].sw=WN[i].w;WN[i].sh=WN[i].h;
        WN[i].x=6; WN[i].y=32; WN[i].w=(int)W-12; WN[i].h=(int)H-32-86; WN[i].max=1; }
    else { WN[i].x=WN[i].sx; WN[i].y=WN[i].sy; WN[i].w=WN[i].sw; WN[i].h=WN[i].sh; WN[i].max=0; } focus=i; WN[i].z=++ztop; }
void apply_snap(int i,int s){ int wy=32, wh=(int)H-32-86, hw=((int)W-18)/2, hh=(wh-6)/2; WN[i].max=1;
    if(s==3){ WN[i].x=6;WN[i].y=wy;WN[i].w=(int)W-12;WN[i].h=wh; }
    else if(s==1){ WN[i].x=6;WN[i].y=wy;WN[i].w=hw;WN[i].h=wh; }
    else if(s==2){ WN[i].x=(int)W-6-hw;WN[i].y=wy;WN[i].w=hw;WN[i].h=wh; }
    else if(s==4){ WN[i].x=6;WN[i].y=wy;WN[i].w=hw;WN[i].h=hh; }
    else if(s==5){ WN[i].x=(int)W-6-hw;WN[i].y=wy;WN[i].w=hw;WN[i].h=hh; }
    else if(s==6){ WN[i].x=6;WN[i].y=wy+hh+6;WN[i].w=hw;WN[i].h=hh; }
    else { WN[i].x=(int)W-6-hw;WN[i].y=wy+hh+6;WN[i].w=hw;WN[i].h=hh; } focus=i; WN[i].z=++ztop; }

static void wtbtn_geom(int i,int b,int* bx,int* by);
int win_ctl_hit(int i,int px,int py){          /* 0=min 1=max 2=close, -1=none */
    if(py<WN[i].y||py>=WN[i].y+TITLEH) return -1;
    for(int b=0;b<3;b++){ int bx,by; wtbtn_geom(i,b,&bx,&by); if(px>=bx-1&&px<bx+17) return b; } return -1;
}
static void wbevel(int x,int y,int w,int h,int in){ u32 face=rgb(212,208,200), lt=in?rgb(128,128,128):rgb(255,255,255), dk=in?rgb(255,255,255):rgb(128,128,128), bk=in?rgb(255,255,255):rgb(64,64,64);
    fill_rect(x,y,w,h,face); fill_rect(x,y,w,1,in?rgb(64,64,64):rgb(212,208,200)); fill_rect(x,y,1,h,in?rgb(64,64,64):rgb(212,208,200)); fill_rect(x+1,y+1,w-2,1,lt); fill_rect(x+1,y+1,1,h-2,lt);
    fill_rect(x,y+h-1,w,1,bk); fill_rect(x+w-1,y,1,h,bk); fill_rect(x+1,y+h-2,w-2,1,dk); fill_rect(x+w-2,y+1,1,h-2,dk); }
static void wtbtn_geom(int i,int b,int* bx,int* by){ int x=WN[i].x,y=WN[i].y,w=WN[i].w; int cx=x+w-6-16; *by=y+5; *bx=(b==2)?cx:(b==1)?cx-2-16:cx-16-16; }
void ui_bevel(int x,int y,int w,int h,int in){ wbevel(x,y,w,h,in); }
void ui_menubar(int x,int y,int w,const char* const* items,int n){ fill_rect(x,y,w,20,rgb(212,208,200)); int cx=x+6; for(int k=0;k<n;k++){ int l=0; while(items[k][l])l++; int iw=l*8+12; if(mx>=cx&&mx<cx+iw&&my>=y&&my<y+20) wbevel(cx,y+1,iw,18,0); dstr(cx+6,y+3,items[k],rgb(0,0,0),1); { int ux=cx+6; fill_rect(ux,y+16,7,1,rgb(0,0,0)); } cx+=iw; } fill_rect(x,y+20,w,1,rgb(128,128,128)); fill_rect(x,y+21,w,1,rgb(255,255,255)); }
void ui_scrollbar(int x,int y,int h,int total,int vis,int pos){ fill_rect(x,y,16,h,rgb(225,222,216)); for(int j=y;j<y+h;j+=2) for(int q=(j&1);q<16;q+=2) putpx(x+q,j,rgb(255,255,255));
    wbevel(x,y,16,16,0); wbevel(x,y+h-16,16,16,0); for(int k=0;k<4;k++){ fill_rect(x+8-k,y+6+k,1+2*k,1,rgb(0,0,0)); fill_rect(x+8-k,y+h-6-k,1+2*k,1,rgb(0,0,0)); }
    int tr=h-32; if(tr<8||total<=vis||total<=0) return; int th=tr*vis/total; if(th<12)th=12; if(th>tr)th=tr; int ty=y+16+(tr-th)*pos/(total-vis>0?total-vis:1); if(ty<y+16)ty=y+16; if(ty+th>y+h-16)ty=y+h-16-th; wbevel(x,ty,16,th,0); }
void ui_tabs(int x,int y,const char* const* items,int n,int sel){ int cx=x; for(int k=0;k<n;k++){ int l=0; while(items[k][l])l++; int iw=l*8+16; int on=(k==sel); int ty=on?y:y+2, th=on?22:20;
    fill_rect(cx,ty,iw,th,rgb(212,208,200)); fill_rect(cx,ty,iw,1,rgb(255,255,255)); fill_rect(cx,ty,1,th,rgb(255,255,255)); fill_rect(cx+iw-1,ty+1,1,th-1,rgb(64,64,64)); fill_rect(cx+iw-2,ty+1,1,th-1,rgb(128,128,128));
    dstr(cx+8,ty+(on?4:3),items[k],rgb(0,0,0),1); if(on) dstr(cx+9,ty+4,items[k],rgb(0,0,0),1); cx+=iw; } }
void win_frame(int i,const char*title){
    int x=WN[i].x,y=WN[i].y,w=WN[i].w,h=WN[i].h; int foc=(focus==i);
    wbevel(x,y,w,h,0);
    int tx=x+4,ty=y+4,tw=w-8,th=TITLEH-6;
    u32 c0=foc?rgb(110,34,9):rgb(128,128,128), c1=foc?rgb(244,202,158):rgb(192,192,192);
    if(g_gfx) gq_grad(g_rt,tx,ty,tw,th,c0,c1,0); else for(int ii=0;ii<tw;ii++){ u32 c=mix(c0,c1,ii*256/(tw>0?tw:1)); for(int j=0;j<th;j++) putpx(tx+ii,ty+j,c); }
    { char t[64]; int k=0; int maxc=(tw-110)/8; if(maxc>62)maxc=62; while(title[k]&&k<maxc){ t[k]=title[k]; k++; } t[k]=0; u32 tc=foc?rgb(255,255,255):rgb(213,208,199), sh=foc?rgb(66,20,5):rgb(76,76,76);
      extern void win_title_icon(int,int,int); win_title_icon(i,tx+2,ty+(th-16)/2); int lx=tx+22, lyy=ty+(th-14)/2;
      dstr(lx+1,lyy+1,t,sh,1); dstr(lx,lyy,t,tc,1); if(foc) dstr(lx+1,lyy,t,tc,1);
      int s0=lx+k*8+10, bx0,by0; wtbtn_geom(i,0,&bx0,&by0); int s1=bx0-6; u32 sc=foc?rgb(110,34,9):rgb(128,128,128);
      if(s1-s0>8) for(int yy=ty+3;yy<ty+th-3;yy+=2){ fill_rect(s0,yy,s1-s0,1,sc); } }
    for(int b=0;b<3;b++){ int bx,by; wtbtn_geom(i,b,&bx,&by); int pr=(focus==i&&mouse_btn&&mx>=bx&&mx<bx+16&&my>=by&&my<by+14); wbevel(bx,by,16,14,pr); int gx=bx+4+pr, gy=by+3+pr; u32 g=rgb(0,0,0);
        if(b==0){ fill_rect(gx,gy+6,6,2,g); }
        else if(b==1){ fill_rect(gx-1,gy-1,9,8,g); fill_rect(gx,gy+1,7,5,rgb(212,208,200)); }
        else { for(int t=0;t<7;t++){ putpx(gx+t,gy+t,g); putpx(gx+t+1,gy+t,g); putpx(gx+6-t,gy+t,g); putpx(gx+7-t,gy+t,g); } } }
    fill_rect(x+3,y+TITLEH-1,w-6,1,rgb(212,208,200));
    { int gx=x+w-5, gy=y+h-5; for(int k=0;k<3;k++){ int o=k*4+2; for(int t=0;t<=o;t++){ putpx(gx-o+t,gy-t,rgb(255,255,255)); putpx(gx-o+t+1,gy-t,rgb(128,128,128)); } } }
}
int in_rect(int px,int py,int x,int y,int w,int h){ return px>=x&&px<x+w&&py>=y&&py<y+h; }

