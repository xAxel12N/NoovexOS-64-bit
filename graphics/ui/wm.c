#include "include/nvx/base.h"
#include "include/nvx/drivers/video/fb/framebuffer.h"
#include "include/nvx/net/http/http.h"
#include "include/nvx/graphics/theme/theme.h"
#include "include/nvx/graphics/ui/wm.h"
#include "include/nvx/apps/accessories/small_apps.h"
#include "include/nvx/apps/games/doom.h"
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
void notify(const char* title,const char* body){
    int i=g_notif_n%NOTIF_MAX; int k=0; while(title[k]&&k<39){g_notif[i].title[k]=title[k];k++;} g_notif[i].title[k]=0;
    k=0; if(body){ while(body[k]&&k<71){g_notif[i].body[k]=body[k];k++;} } g_notif[i].body[k]=0;
    g_notif[i].shown=0; g_notif_n++; g_toast_cur=i; g_toast_until=0;   /* shown next frame */
}
int focus=-1, ztop=1, drag=-1, dox=0, doy=0;
int g_ctxkind=0, g_ctxwin=-1;                 /* right-click menu: 0 desktop 1 window 2 files 4 tree */
int g_tctx_type=-1, g_tctx_val=-1;            /* tree ctx target: 0 folder,1 fs-file,2 curated */
int g_dctx_item=-1;                           /* desktop right-click target (app 0-4, or 100+slot) */
int g_prop=0, g_prop_app=-1, g_prop_slot=-1; 
int g_submenu=0, g_sub_x=0, g_sub_y=0, g_subn=0;   /* hover submenu */
int g_fdrag=0, g_fdrag_slot=-1, g_fdrag_x=0, g_fdrag_y=0;   /* drag a tree file */
const char* g_subit[8]; char g_subparent[16]="";
int g_snap=0, g_resize=-1, r_sw0=0, r_sh0=0, r_mx0=0, r_my0=0, r_sx0=0, r_sy0=0, g_rflags=8|2, g_rz_hover=0;
int g_launcher=0, g_alttab=0, g_altsel=0, g_altlist[NWIN], g_altn=0, alt_prev=0;
int g_wallmode=0, wall_custom_slot=-1, g_clockpop=0, g_qs=0;
int g_rename=0, g_renslot=-1, g_renlen=0; char g_renbuf[64]="";
int g_clockx=0, g_qsx=0;
int g_marq=0, marq_x0=0, marq_y0=0;
char toast_msg[80]=""; u64 toast_t=0;
void toast(const char*m){ { extern int g_osd_kind; g_osd_kind=0; } int i=0; while(m[i]&&i<79){toast_msg[i]=m[i];i++;} toast_msg[i]=0; toast_t=g_ticks; }
const char *APPNAME[NWIN]={"Welcome","Terminal","Files","Editor","Calculator","Browser","Clock","Task Manager","Calendar","Snake","Flappy Bird","neofetch","Paint","DOOM","Settings","About","ChatGPT","Presentation","Logs","Terminal Finder","Video Player","X Display","Camera","Benchmark","Voice Recorder","Writer","Quake","Gears"};
   /* fwd - defined near compositor */
  
void open_win(int i){ int was=WN[i].open; WN[i].open=1; WN[i].min=0; WN[i].ws=g_ws; focus=i; WN[i].z=++ztop;
    if(!was){ if(i==W_SNAKE)snake_reset(); else if(i==W_FLAPPY)flappy_reset(); else if(i==W_CALENDAR)cal_open(); }
    if(!was && g_state==ST_DESKTOP){ win_anim(i,1); gfx_win_event(i,0); } }
void close_win(int i){ if(WN[i].open&&!WN[i].min&&g_state==ST_DESKTOP){ win_anim(i,0); gfx_win_event(i,1); } WN[i].open=0; WN[i].min=0; if(focus==i)focus=-1; if(i==W_DOOM) g_doom_mcap=0; if(i==W_GPU3D){ extern void gpu3d_closed(void); gpu3d_closed(); } if(i==W_CAMERA){ extern void camera_closed(void); camera_closed(); } if(i==W_RECORDER){ extern void recorder_closed(void); recorder_closed(); } if(i==W_QUAKE){ extern void quake_closed(void); quake_closed(); } if(i==W_VIDEO){ extern void video_stop(void); video_stop(); } if(i==W_BROWSER){ extern void browser_media_pause(void); browser_media_pause(); } }
void focus_win(int i){ if(!WN[i].open){ open_win(i); return; } if(WN[i].min)gfx_win_event(i,0); WN[i].min=0; focus=i; WN[i].z=++ztop; }
void win_min(int i){ if(!WN[i].min&&WN[i].open)gfx_win_event(i,2); WN[i].min=1; if(focus==i)focus=-1; }
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
    for(int b=0;b<3;b++){ int bx,by; wtbtn_geom(i,b,&bx,&by); if(px>=bx-1&&px<bx+19) return b; } return -1;
}
static void wbevel(int x,int y,int w,int h,int in){ u32 face=rgb(192,192,192), lt=in?rgb(128,128,128):rgb(255,255,255), dk=in?rgb(255,255,255):rgb(128,128,128), bk=in?rgb(255,255,255):rgb(0,0,0);
    fill_rect(x,y,w,h,face); fill_rect(x,y,w,1,in?rgb(0,0,0):rgb(223,223,223)); fill_rect(x,y,1,h,in?rgb(0,0,0):rgb(223,223,223)); fill_rect(x+1,y+1,w-2,1,lt); fill_rect(x+1,y+1,1,h-2,lt);
    fill_rect(x,y+h-1,w,1,bk); fill_rect(x+w-1,y,1,h,bk); fill_rect(x+1,y+h-2,w-2,1,dk); fill_rect(x+w-2,y+1,1,h-2,dk); }
static void wtbtn_geom(int i,int b,int* bx,int* by){ int x=WN[i].x,y=WN[i].y,w=WN[i].w; int cx=x+w-6-18; *by=y+6; *bx=(b==2)?cx:(b==1)?cx-2-18:cx-2-18-18; }
void win_frame(int i,const char*title){
    int x=WN[i].x,y=WN[i].y,w=WN[i].w,h=WN[i].h; int foc=(focus==i);
    wbevel(x,y,w,h,0);
    int tx=x+4,ty=y+4,tw=w-8,th=TITLEH-6;
    u32 c0=foc?rgb(0,0,128):rgb(128,128,128), c1=foc?rgb(16,132,208):rgb(192,192,192);
    if(g_gfx) gq_grad(g_rt,tx,ty,tw,th,c0,c1,0); else for(int ii=0;ii<tw;ii++){ u32 c=mix(c0,c1,ii*256/(tw>0?tw:1)); for(int j=0;j<th;j++) putpx(tx+ii,ty+j,c); }
    { char t[64]; int k=0; int maxc=(tw-90)/8; if(maxc>62)maxc=62; while(title[k]&&k<maxc){ t[k]=title[k]; k++; } t[k]=0; u32 tc=foc?rgb(255,255,255):rgb(224,224,224);
      dstr(tx+6,ty+(th-14)/2,t,tc,1); dstr(tx+7,ty+(th-14)/2,t,tc,1); }
    for(int b=0;b<3;b++){ int bx,by; wtbtn_geom(i,b,&bx,&by); int pr=(focus==i&&mouse_btn&&mx>=bx&&mx<bx+18&&my>=by&&my<by+16); wbevel(bx,by,18,16,pr); int gx=bx+5+pr, gy=by+4+pr; u32 g=rgb(0,0,0);
        if(b==0){ fill_rect(gx,gy+7,7,2,g); }
        else if(b==1){ fill_rect(gx-1,gy-1,10,9,g); fill_rect(gx,gy+1,8,6,rgb(192,192,192)); }
        else { for(int t=0;t<8;t++){ putpx(gx+t,gy+t,g); putpx(gx+t+1,gy+t,g); putpx(gx+7-t,gy+t,g); putpx(gx+8-t,gy+t,g); } } }
    fill_rect(x+3,y+TITLEH-1,w-6,1,rgb(192,192,192));
    { int gx=x+w-5, gy=y+h-5; for(int k=0;k<3;k++){ int o=k*4+2; for(int t=0;t<=o;t++){ putpx(gx-o+t,gy-t,rgb(255,255,255)); putpx(gx-o+t+1,gy-t,rgb(128,128,128)); } } }
}
int in_rect(int px,int py,int x,int y,int w,int h){ return px>=x&&px<x+w&&py>=y&&py<y+h; }

