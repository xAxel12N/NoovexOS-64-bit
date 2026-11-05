extern int xapps_count(void); extern const char* app_name(int); extern int app_icon_draw(int,int,int,int); extern void app_launch(int);
#include "include/nvx/apps/internet/browser.h"
#include "include/nvx/apps/system/settings.h"
#include "include/nvx/apps/system/files_classic.h"
#include "include/nvx/apps/system/file_explorer.h"
#include "include/nvx/apps/accessories/small_apps.h"
#include "include/nvx/apps/system/logs.h"
#include "include/nvx/graphics/ui/desktop.h"
#include "include/nvx/graphics/ui/input.h"
#include "include/nvx/core/init/splash.h"
#include "include/nvx/core/init/setup.h"
#include "include/nvx/apps/media/videoplayer.h"
#include "include/nvx/drivers/video/fb/gfx.h"
#include "include/nvx/mem/heap.h"
#include "include/nvx/core/klog.h"
#include "include/nvx/core/focus.h"
#include "include/nvx/net/wifi/wifi_mgr.h"
/* =========================================================================
   Desktop icons / wallpaper / taskbar / compositor
   ========================================================================= */
/* ---- dock icons: consistent rounded tiles, flat glyphs ---- */
#define ICT 9
static void itile(int x,int y,u32 c){ fill_round(x+4,y+4,40,40,ICT,15,c); }
static void ic_folder(int x,int y){ itile(x,y,rgb(74,118,222));
    fill_round(x+11,y+15,10,4,2,1|2,rgb(255,214,120)); fill_round(x+10,y+18,24,14,3,15,rgb(255,203,92)); fill_round(x+10,y+18,24,4,2,1|2,rgb(255,224,150)); }
static void ic_term(int x,int y){ itile(x,y,rgb(26,30,42));
    dchar(x+13,y+15,'>',rgb(120,240,150),2); fill_rect(x+24,y+27,10,3,rgb(120,240,150)); }
static void ic_edit(int x,int y){ itile(x,y,rgb(96,110,232));
    fill_round(x+13,y+12,22,24,3,15,rgb(248,250,255)); for(int k=0;k<4;k++)fill_rect(x+17,y+17+k*5,14,2,rgb(150,170,220));
    line_bl(x+28,y+34,x+37,y+25,rgb(240,170,50),256,3); fill_rect(x+35,y+23,4,4,rgb(150,110,40)); }
static void ic_web(int x,int y){ itile(x,y,rgb(40,140,230)); int cx=x+24,cy=y+24;
    fill_circle(cx,cy,13,rgb(70,168,246)); for(int k=-1;k<=1;k++) line_bl(cx-12,cy+k*7,cx+12,cy+k*7,rgb(240,248,255),150,0);
    line_bl(cx,cy-13,cx,cy+13,rgb(240,248,255),160,0); fill_circle(cx,cy,2,rgb(255,255,255)); }
static void ic_calc(int x,int y){ itile(x,y,rgb(60,66,84));
    fill_round(x+12,y+11,24,8,2,15,rgb(150,210,180)); for(int r=0;r<3;r++)for(int c=0;c<3;c++) fill_round(x+13+c*8,y+23+r*6,5,4,1,15,rgb(150,158,178)); }
static void ic_clock(int x,int y){ itile(x,y,rgb(250,251,255)); int cx=x+24,cy=y+24;
    fill_circle(cx,cy,15,ACC); fill_circle(cx,cy,12,rgb(252,253,255));
    for(int t=0;t<12;t++){ int a=t*30; int px=cx+icos(a-90)*10/1000,py=cy+isin(a-90)*10/1000; fill_rect(px-1,py-1,2,2,rgb(120,128,146)); }
    line_bl(cx,cy,cx+icos(-30)*6/1000,cy+isin(-30)*6/1000,rgb(40,46,64),255,2);
    line_bl(cx,cy,cx+icos(60)*9/1000,cy+isin(60)*9/1000,rgb(40,46,64),255,1); fill_circle(cx,cy,2,rgb(220,70,90)); }
static void ic_calendar(int x,int y){ itile(x,y,rgb(250,251,255));
    fill_round(x+10,y+11,28,26,4,15,rgb(255,255,255)); fill_round(x+10,y+11,28,9,4,1|2,rgb(226,75,74));
    fill_rect(x+16,y+9,3,6,rgb(255,255,255)); fill_rect(x+29,y+9,3,6,rgb(255,255,255));
    dstr_ctr(x+24,y+23,"30",rgb(60,66,84),1); }
static void ic_sysmon(int x,int y){ itile(x,y,rgb(26,30,42));
    int bx=x+13; u32 g=rgb(90,222,150); int hs[5]={8,15,7,18,12}; for(int k=0;k<5;k++) fill_rect(bx+k*5,y+32-hs[k],3,hs[k],g);
    fill_rect(x+13,y+34,20,2,rgb(90,100,120)); }
static void ic_snake(int x,int y){ itile(x,y,rgb(46,152,72));
    u32 s=rgb(150,236,180); fill_round(x+12,y+26,7,7,2,15,s); fill_round(x+18,y+26,7,7,2,15,s); fill_round(x+24,y+26,7,7,2,15,s); fill_round(x+24,y+20,7,7,2,15,s); fill_round(x+30,y+20,8,8,2,15,rgb(190,250,210));
    fill_circle(x+34,y+23,1,rgb(20,40,26)); fill_circle(x+17,y+15,4,rgb(228,76,74)); }
static void ic_flappy(int x,int y){ itile(x,y,rgb(96,176,236));
    int cx=x+22,cy=y+25; fill_circle(cx,cy,9,rgb(250,208,60)); fill_round(x+11,y+25,7,5,2,15,rgb(236,182,44));
    fill_circle(cx+4,cy-3,2,rgb(30,30,34)); fill_rect(cx+8,cy-2,5,4,rgb(240,132,40)); }
static void ic_paint(int x,int y){ itile(x,y,rgb(250,251,255));
    fill_circle(x+14,y+15,3,rgb(228,76,74)); fill_circle(x+22,y+13,3,rgb(240,200,50)); fill_circle(x+30,y+15,3,rgb(90,200,110)); fill_circle(x+18,y+22,3,rgb(50,120,224)); fill_circle(x+27,y+22,3,rgb(150,90,220));
    line_bl(x+14,y+35,x+30,y+27,rgb(120,90,60),256,3); fill_round(x+28,y+24,7,7,2,15,rgb(228,76,74)); }
static void ic_gear(int x,int y){ itile(x,y,rgb(122,130,148)); int cx=x+24,cy=y+24;
    for(int a=0;a<8;a++){ int dx=0,dy=0; if(a==0)dy=-13;if(a==4)dy=13;if(a==2)dx=13;if(a==6)dx=-13; if(a==1){dx=9;dy=-9;}if(a==3){dx=9;dy=9;}if(a==5){dx=-9;dy=9;}if(a==7){dx=-9;dy=-9;} fill_rect(cx+dx-3,cy+dy-3,6,6,rgb(238,240,246)); }
    fill_circle(cx,cy,10,rgb(238,240,246)); fill_circle(cx,cy,4,rgb(122,130,148)); }

static int IC_X=28, IC_W=76, IC_H=76; static int IC_Y[6]={16,110,204,298,392,486};
int IC_APP[12]={W_FILES,W_TERM,W_EDITOR,W_BROWSER,W_CALC,W_CLOCK,W_CALENDAR,W_SYSMON,W_SNAKE,W_FLAPPY,W_PAINT,W_SETTINGS};
static const char *IC_LBL[12]={"Files","Terminal","Editor","Browser","Calc","Clock","Calendar","Monitor","Snake","Flappy","Paint","Settings"};
extern int g_dstyle;
static void cl_folder(int x,int y){ fill_rect(x+2,y+6,12,4,rgb(150,98,60)); fill_rect(x+3,y+7,10,3,rgb(214,160,116)); fill_rect(x+2,y+9,28,19,rgb(150,98,60)); fill_rect(x+3,y+10,26,17,rgb(222,176,128)); fill_rect(x+3,y+10,26,2,rgb(240,206,166)); }
static void cl_term(int x,int y){ fill_rect(x+2,y+4,28,22,rgb(128,128,128)); fill_rect(x+3,y+5,26,20,rgb(212,208,200)); fill_rect(x+5,y+7,22,15,rgb(0,0,0)); dstr(x+6,y+8,">_",rgb(0,220,0),1); fill_rect(x+10,y+26,12,3,rgb(128,128,128)); }
static void cl_edit(int x,int y){ fill_rect(x+6,y+2,20,28,rgb(64,64,64)); fill_rect(x+7,y+3,18,26,rgb(255,255,255)); for(int r=0;r<6;r++) fill_rect(x+9,y+7+r*4,14,1,rgb(80,110,200)); fill_rect(x+20,y+18,3,12,rgb(230,190,40)); fill_rect(x+20,y+29,3,2,rgb(60,60,60)); }
static void cl_web(int x,int y){ int cx=x+16,cy=y+16; fill_circle(cx,cy,13,rgb(30,90,200)); fill_circle(cx,cy,11,rgb(70,150,235)); for(int k=-6;k<=6;k+=6) fill_rect(cx-11,cy+k,23,1,rgb(200,230,255)); fill_rect(cx,cy-11,1,23,rgb(200,230,255)); fill_rect(cx-6,cy-6,5,9,rgb(60,170,70)); fill_rect(cx+3,cy+1,6,7,rgb(60,170,70)); }
static void cl_calc(int x,int y){ fill_rect(x+6,y+2,20,28,rgb(64,64,64)); fill_rect(x+7,y+3,18,26,rgb(212,208,200)); fill_rect(x+9,y+5,14,6,rgb(170,200,170)); for(int r=0;r<3;r++)for(int c=0;c<3;c++) fill_rect(x+9+c*5,y+14+r*5,4,4,rgb(80,80,80)); }
static void cl_gear(int x,int y){ int cx=x+16,cy=y+16; fill_circle(cx,cy,12,rgb(90,90,90)); fill_circle(cx,cy,10,rgb(170,170,170)); for(int k=0;k<8;k++){ int dx=(k==0||k==4)?0:(k<4?1:-1), dy=(k==2||k==6)?0:(k<2||k>6?-1:1); fill_rect(cx-2+dx*11,cy-2+dy*11,5,5,rgb(90,90,90)); } fill_circle(cx,cy,4,rgb(80,80,80)); fill_circle(cx,cy,2,rgb(212,208,200)); }
static void draw_icon(int k){ int x=IC_X,y=IC_Y[k],ix=x+14;
    if(g_dstyle==1){ int cx=x+38-16, cy=y+6; if(k==0)cl_folder(cx,cy);else if(k==1)cl_term(cx,cy);else if(k==2)cl_edit(cx,cy);else if(k==3)cl_web(cx,cy);else if(k==4)cl_calc(cx,cy);else cl_gear(cx,cy);
        static const char* CLB[6]={"Files","Terminal","Text Editor","Browser","Calculator","Settings"}; dstr_ctr(x+39,y+43,CLB[k],rgb(0,0,0),1); dstr_ctr(x+38,y+42,CLB[k],rgb(255,255,255),1); return; }
    if(k==0)ic_folder(ix,y);else if(k==1)ic_term(ix,y);else if(k==2)ic_edit(ix,y);else if(k==3)ic_web(ix,y);else if(k==4)ic_calc(ix,y);else ic_gear(ix,y);
    dstr_ctr(x+38,y+52,IC_LBL[k],rgb(238,242,250),1); }

static void wall_gradient(u32 t,u32 b){ (void)SCENE; for(u32 y=0;y<H;y++){ u32 c=mix(t,b,(int)((y*256)/H)); for(u32 x=0;x<W;x++) g_scene[(u64)y*SW+x]=c; } }
static void fill_tri(int x0,int y0,int x1,int y1,int x2,int y2,u32 c){
    int t;
    if(y1<y0){t=x0;x0=x1;x1=t;t=y0;y0=y1;y1=t;}
    if(y2<y0){t=x0;x0=x2;x2=t;t=y0;y0=y2;y2=t;}
    if(y2<y1){t=x1;x1=x2;x2=t;t=y1;y1=y2;y2=t;}
    if(y2==y0)return;
    for(int y=y0;y<=y2;y++){ if(y<0||y>=(int)H)continue;
        int xa=x0+(x2-x0)*(y-y0)/(y2-y0), xb;
        if(y<y1) xb = (y1==y0)? x0 : x0+(x1-x0)*(y-y0)/(y1-y0);
        else     xb = (y2==y1)? x1 : x1+(x2-x1)*(y-y1)/(y2-y1);
        int lx=xa<xb?xa:xb, rx=xa<xb?xb:xa; if(lx<0)lx=0; if(rx>=(int)W)rx=(int)W-1;
        u32* row=&SCENE[(u64)y*SW]; for(int x=lx;x<=rx;x++)row[x]=c; }
}
static u32 lp_rng;
static u32 lpr(void){ lp_rng^=lp_rng<<13; lp_rng^=lp_rng>>17; lp_rng^=lp_rng<<5; return lp_rng; }
static void wall_lowpoly(void){
    lp_rng=2246822519u;
    int COLS=14, ROWS=9; int cw=(int)W/COLS, ch=(int)H/ROWS;
    static int vx[16][11], vy[16][11];
    for(int r=0;r<=ROWS;r++) for(int c=0;c<=COLS;c++){
        int jx=(int)(lpr()%((u32)cw?(u32)cw/2+1:1))-cw/4, jy=(int)(lpr()%((u32)ch?(u32)ch/2+1:1))-ch/4;
        int px=c*cw+jx, py=r*ch+jy;
        if(c==0)px=0; if(c==COLS)px=(int)W; if(r==0)py=0; if(r==ROWS)py=(int)H;
        vx[c][r]=px; vy[c][r]=py; }
    for(int r=0;r<ROWS;r++) for(int c=0;c<COLS;c++){
        int td=(c*255/COLS + r*255/ROWS)/2;
        u32 base=mix(rgb(16,26,82), rgb(58,116,224), td);
        u32 c1=mix(base, rgb(96,156,246), (int)(lpr()%34));
        fill_tri(vx[c][r],vy[c][r], vx[c+1][r],vy[c+1][r], vx[c][r+1],vy[c][r+1], c1);
        u32 c2=mix(base, rgb(24,54,140), (int)(lpr()%34));
        fill_tri(vx[c+1][r],vy[c+1][r], vx[c+1][r+1],vy[c+1][r+1], vx[c][r+1],vy[c][r+1], c2); }
}
static u8 wallsrc[1920*1200*4]; static int wall_have=-1, wall_sw=0, wall_sh=0;
extern const unsigned char wall_hd_porsche_jpg[], wall_hd_crane_jpg[], wall_hd_wire_jpg[], wall_hd_peaks_jpg[], wall_hd_earth_jpg[], wall_hd_yosemite_jpg[];
extern const int wall_hd_porsche_jpg_len, wall_hd_crane_jpg_len, wall_hd_wire_jpg_len, wall_hd_peaks_jpg_len, wall_hd_earth_jpg_len, wall_hd_yosemite_jpg_len;
static void wall_src(int th,const unsigned char** jd,int* jl){
    switch(th){ case 0: *jd=wall_hd_porsche_jpg; *jl=wall_hd_porsche_jpg_len; break; case 1: *jd=wall_hd_crane_jpg; *jl=wall_hd_crane_jpg_len; break;
        case 2: *jd=wall_hd_wire_jpg; *jl=wall_hd_wire_jpg_len; break; case 3: *jd=wall_hd_peaks_jpg; *jl=wall_hd_peaks_jpg_len; break;
        case 4: *jd=wall_hd_earth_jpg; *jl=wall_hd_earth_jpg_len; break; default: *jd=wall_hd_yosemite_jpg; *jl=wall_hd_yosemite_jpg_len; break; } }
static void wall_decode(int th){
    if(th==wall_have && wall_sw>0) return;
    const unsigned char* jd; int jl; wall_src(th,&jd,&jl);
    int w=0,h=0;
    if(jpeg_decode(jd,jl,wallsrc,(int)sizeof(wallsrc),&w,&h)==0 && w>0 && h>0){ wall_sw=w; wall_sh=h; wall_have=th; }
    else { wall_sw=0; wall_sh=0; wall_have=-1; }
}
static int ws_x0[MAXW]; static u8 ws_fx[MAXW]; static short ws_cw[MAXW][4];
static void cr_w(u32 f,short* w){ int t=(int)f; int t2=t*t>>8, t3=t2*t>>8; w[0]=(short)((-t3+2*t2-t)>>1); w[1]=(short)((3*t3-5*t2+512)>>1); w[2]=(short)((-3*t3+4*t2+t)>>1); w[3]=(short)((t3-t2)>>1); int s=w[0]+w[1]+w[2]+w[3]; w[1]+=(short)(256-s); }
static void wall_cubic(const u8* src,int sw,int sh,int ox,int oy,int dw,int dh){
    u32* sc=SCENE;
    for(u32 x=0;x<W;x++){ long long u=((long long)(2*((int)x-ox)+1)*sw*65536LL)/(2LL*dw)-32768; if(u<0)u=0; if(u>(long long)(sw-1)*65536)u=(long long)(sw-1)*65536; ws_x0[x]=(int)(u>>16); cr_w((u32)((u>>8)&255),ws_cw[x]); }
    for(u32 y=0;y<H;y++){ long long v=((long long)(2*((int)y-oy)+1)*sh*65536LL)/(2LL*dh)-32768; if(v<0)v=0; if(v>(long long)(sh-1)*65536)v=(long long)(sh-1)*65536;
        int y0=(int)(v>>16); short wy[4]; cr_w((u32)((v>>8)&255),wy); const u8* rr[4]; for(int j=0;j<4;j++){ int yy=y0-1+j; if(yy<0)yy=0; if(yy>=sh)yy=sh-1; rr[j]=src+(u64)yy*sw*4; }
        u32* d=sc+(u64)y*SW;
        for(u32 x=0;x<W;x++){ int x0=ws_x0[x]; int xi[4]; for(int i=0;i<4;i++){ int xx=x0-1+i; if(xx<0)xx=0; if(xx>=sw)xx=sw-1; xi[i]=xx*4; } short* wx=ws_cw[x]; int o[3];
            for(int k=0;k<3;k++){ int acc=0; for(int j=0;j<4;j++){ const u8* r=rr[j]; int h=r[xi[0]+k]*wx[0]+r[xi[1]+k]*wx[1]+r[xi[2]+k]*wx[2]+r[xi[3]+k]*wx[3]; acc+=h*wy[j]; } acc=(acc+32768)>>16; o[k]=acc<0?0:acc>255?255:acc; }
            d[x]=rgb(o[0],o[1],o[2]); } } }
static void wall_smooth(const u8* src,int sw,int sh,int ox,int oy,int dw,int dh){
    u32* sc=SCENE; if(sw<1||sh<1||dw<1||dh<1)return;
    if(dw>sw+sw/8&&sw>=4&&sh>=4){ wall_cubic(src,sw,sh,ox,oy,dw,dh); return; }
    for(u32 x=0;x<W;x++){ long long u=((long long)(2*((int)x-ox)+1)*sw*65536LL)/(2LL*dw)-32768; if(u<0)u=0; if(u>(long long)(sw-1)*65536)u=(long long)(sw-1)*65536; ws_x0[x]=(int)(u>>16); ws_fx[x]=(u8)((u>>8)&255); }
    for(u32 y=0;y<H;y++){ long long v=((long long)(2*((int)y-oy)+1)*sh*65536LL)/(2LL*dh)-32768; if(v<0)v=0; if(v>(long long)(sh-1)*65536)v=(long long)(sh-1)*65536;
        int y0=(int)(v>>16), y1=y0+1<sh?y0+1:sh-1; u32 fy=(u32)((v>>8)&255); const u8* r0=src+(u64)y0*sw*4; const u8* r1=src+(u64)y1*sw*4; u32* d=sc+(u64)y*SW;
        for(u32 x=0;x<W;x++){ int x0=ws_x0[x], x1=x0+1<sw?x0+1:sw-1; u32 fx=ws_fx[x]; const u8 *a=r0+x0*4, *b=r0+x1*4, *c=r1+x0*4, *e=r1+x1*4; u32 o[3];
            for(int k=0;k<3;k++){ u32 t=(a[k]*(256-fx)+b[k]*fx), bt=(c[k]*(256-fx)+e[k]*fx); o[k]=(t*(256-fy)+bt*fy+32768)>>16; }
            d[x]=rgb(o[0],o[1],o[2]); } } }
static void wall_blit_cover(void){
    if(view_w<=0||view_h<=0){ wall_gradient(rgb(28,34,72),rgb(58,66,120)); return; }
    int sw=view_w, sh=view_h; int s1=(int)W*1000/sw, s2=(int)H*1000/sh; int scn=s1>s2?s1:s2;
    int dw=sw*scn/1000, dh=sh*scn/1000; if(dw<1)dw=1; if(dh<1)dh=1; int ox=((int)W-dw)/2, oy=((int)H-dh)/2;
    wall_smooth(viewbuf,sw,sh,ox,oy,dw,dh);
}
void wall_thumbs_build(void){
    if(wall_thumb_ready)return;
    for(int k=0;k<NWALL;k++){
        const unsigned char* jd; int jl; wall_src(k,&jd,&jl);
        int w=0,h=0;
        if(jpeg_decode(jd,jl,wallsrc,(int)sizeof(wallsrc),&w,&h)==0 && w>0 && h>0){
            for(int y=0;y<40;y++) for(int x=0;x<64;x++){ int sx0=x*w/64, sx1=(x+1)*w/64, sy0=y*h/40, sy1=(y+1)*h/40; if(sx1<=sx0)sx1=sx0+1; if(sy1<=sy0)sy1=sy0+1; u32 r=0,g=0,b=0,n=0;
                for(int yy=sy0;yy<sy1;yy+=2) for(int xx=sx0;xx<sx1;xx+=2){ u8* p=wallsrc+((u64)yy*w+xx)*4; r+=p[0]; g+=p[1]; b+=p[2]; n++; } if(!n)n=1; wall_thumb[k][y*64+x]=rgb(r/n,g/n,b/n); }
        } else { for(int i=0;i<64*40;i++)wall_thumb[k][i]=THSW[k]; }
    }
    wall_thumb_ready=1; wall_have=-1;   /* wallsrc was reused; force wallpaper re-decode */
}
int g_dstyle=1;
static void draw_wallpaper(int th){
    if(g_safemode){ wall_gradient(rgb(28,34,72),rgb(58,66,120)); return; }
    if(g_wallmode==2){ wall_gradient(rgb(80,80,80),rgb(80,80,80)); return; }
    if(g_wallmode==1 && wall_custom_slot>=0 && FS[wall_custom_slot].used){ if(ensure_img(wall_custom_slot)&&view_w>0){ wall_blit_cover(); return; } }
    if(th<0){ wall_lowpoly(); return; }
    if(th<0)th=0; if(th>=NWALL)th=NWALL-1; wall_decode(th);
    if(wall_sw<=0){ wall_gradient(rgb(38,52,132),rgb(70,80,150)); return; }
    wall_smooth(wallsrc,wall_sw,wall_sh,0,0,(int)W,(int)H);
}
void build_bg(void){ draw_wallpaper(g_theme); memcpy(BG,SCENE,(u64)W*H*4); if(g_wallmode!=2){ extern int wall_enhance(u32*,int,int,int); wall_enhance(BG,(int)W,(int)H,(int)SW); } if(g_gfx) gq_bump(gs_bg); }

/* ---- macOS-style top menu bar + dock ---- */
static int g_applemenu=0;
int DOCK_x,DOCK_y,DOCK_w,DOCK_h;

static const char *AM_LBL[3]={"About This System","Log Out","Restart"};
#define AM_N 3
static int AM_X=8,AM_Y=27,AM_W=190,AM_IH=30;
int g_bellx=0,g_wsx=0;
/* ---- battery via the Embedded Controller (ports 0x62/0x66), ported from Noovex9 32-bit ---- */
#define EC_DATA 0x62
#define EC_CMD  0x66
int bat_present=0, bat_pct=100, bat_charging=1, bat_via_ec=0; static u64 bat_poll_t=0;
static int ec_wait_ibf(void){ for(int t=0;t<2000;t++){ if(!(inb(EC_CMD)&0x02))return 1; io_wait(); } return 0; }
static int ec_wait_obf(void){ for(int t=0;t<2000;t++){ if(inb(EC_CMD)&0x01)return 1; io_wait(); } return 0; }
int ec_read(u8 off,u8* out){ if(!ec_wait_ibf())return 0; outb(EC_CMD,0x80); if(!ec_wait_ibf())return 0; outb(EC_DATA,off); if(!ec_wait_obf())return 0; *out=inb(EC_DATA); return 1; }
int ec_detect(void){ u8 st=inb(EC_CMD); if(st==0xFF)return 0; return ec_wait_ibf(); }
static int ec_battery_pct(int* pct,int* charging){
    static const u8 cand[]={0x2A,0x32,0x42,0x90,0xB0,0x14,0x24};
    for(unsigned i=0;i<sizeof(cand);i++){ u8 v; if(ec_read(cand[i],&v)&&v>0&&v<=100){ *pct=v; u8 ac=0; if(ec_read(0x01,&ac))*charging=(ac&0x01)?1:0; else *charging=0; return 1; } }
    return 0;
}
static int battery_from_acpi(void){ if(!g_acb_n)return 0; if(g_acb_pct>=0){ bat_present=1; bat_pct=g_acb_pct; bat_charging=g_acb_charging; } else bat_present=0; bat_via_ec=2; return 1; }
void battery_init(void){ bat_present=0; bat_pct=100; bat_charging=1; bat_via_ec=0;
    if(battery_from_acpi())return;
    if(ec_detect()){ int p=0,ch=0; if(ec_battery_pct(&p,&ch)){ bat_present=1; bat_pct=p; bat_charging=ch; bat_via_ec=1; } } }
void battery_tick(void){
    if(battery_from_acpi())return;
    if(!bat_via_ec){ static int tries=0; if(tries>=3)return; if(g_ticks-bat_poll_t<100)return; bat_poll_t=g_ticks; tries++;   /* keep retrying ~1s until the EC answers */
        if(ec_detect()){ int p=0,ch=0; if(ec_battery_pct(&p,&ch)){ bat_present=1; bat_pct=p; bat_charging=ch; bat_via_ec=1; } } return; }
    if(g_ticks-bat_poll_t<300)return; bat_poll_t=g_ticks;                     /* poll ~3s once found */
    int p=0,ch=0; if(ec_battery_pct(&p,&ch)){ bat_present=1; bat_pct=p; bat_charging=ch; } }
static void draw_battery(int x,int y){   /* small battery glyph + charging bolt */
    u32 col = bat_charging?rgb(90,200,120) : (bat_pct<=15?rgb(236,80,80) : bat_pct<=30?rgb(230,180,70) : rgb(210,216,228));
    fill_round(x,y,22,11,2,15,rgb(60,64,78)); fill_round(x+1,y+1,20,9,2,15,rgb(24,26,34)); fill_rect(x+22,y+3,2,5,rgb(120,126,140));
    int w=(bat_pct*18)/100; if(w<1&&bat_pct>0)w=1; if(w>18)w=18; fill_round(x+2,y+2,w,7,1,15,col);
    if(bat_charging){ /* lightning bolt overlay */
        for(int i=0;i<6;i++){ putpx(x+12-i/2,y+2+i,rgb(255,255,255)); putpx(x+11-i/2,y+2+i,rgb(255,255,255)); }
        for(int i=0;i<5;i++){ putpx(x+9+i/2,y+9-i,rgb(255,255,255)); } }
}

char g_wifi_user[64]; int g_wifi_userlen=0; char g_wifi_hid[33]; int g_wifi_hidlen=0; static u8 g_wifi_selb[6]; static char g_wifi_sels[33]; static char g_wifi_failed[33];
void wifi_entry_cancel(void){ for(int i=0;i<64;i++){ g_wifi_pw[i]=0; g_wifi_user[i]=0; } for(int i=0;i<33;i++)g_wifi_hid[i]=0; g_wifi_pwlen=0; g_wifi_userlen=0; g_wifi_hidlen=0; g_wifi_entry=0; g_wifi_sel=-1; }
static int wifi_sel_resolve(void){ for(int i=0;i<g_apn;i++){ int eq=1; for(int b=0;b<6;b++) if(g_aps[i].bssid[b]!=g_wifi_selb[b])eq=0; if(eq)return i; } return -1; }
static int ap_eap(int idx){ return idx>=0&&idx<g_apn&&(g_aps[idx].akm&AKM_8021X)&&!(g_aps[idx].akm&(AKM_PSK|AKM_SAE)); }
void wifi_gui_connect(int idx){
    (void)idx; g_wifi_pw[g_wifi_pwlen<63?g_wifi_pwlen:63]=0; g_wifi_user[g_wifi_userlen<63?g_wifi_userlen:63]=0;
    compose_present(); int k=wifi_sel_resolve(); int r=k>=0?wifi_connect_ap(k,g_wifi_pw,g_wifi_user,1):wifi_connect_ssid(g_wifi_sels,g_wifi_pw,g_wifi_user,0);
    if(r==6||r==7){ int i=0; for(;g_wifi_sels[i];i++)g_wifi_failed[i]=g_wifi_sels[i]; g_wifi_failed[i]=0; } else g_wifi_failed[0]=0;
    wifi_entry_cancel(); compose_present(); }
void wifi_gui_pick(int idx){
    if(idx<0||idx>=g_apn)return; g_wifi_sel=idx; for(int b=0;b<6;b++)g_wifi_selb[b]=g_aps[idx].bssid[b]; { int i=0; for(;g_aps[idx].ssid[i]&&i<32;i++)g_wifi_sels[i]=g_aps[idx].ssid[i]; g_wifi_sels[i]=0; }
    int k=wifi_saved_find(g_aps[idx].ssid); if(k>=0&&seq(g_wifi_failed,g_aps[idx].ssid))k=-1;
    if(g_aps[idx].sec==0){ g_wifi_entry=0; g_wifi_pwlen=0; g_wifi_pw[0]=0; wifi_gui_connect(idx); return; }
    if(k>=0){ g_wifi_entry=0; int i=0; for(;g_wnets[k].pass[i]&&i<63;i++)g_wifi_pw[i]=g_wnets[k].pass[i]; g_wifi_pw[i]=0; g_wifi_pwlen=i; i=0; for(;g_wnets[k].user[i]&&i<63;i++)g_wifi_user[i]=g_wnets[k].user[i]; g_wifi_user[i]=0; g_wifi_userlen=i; wifi_gui_connect(idx); return; }
    g_wifi_pwlen=0; g_wifi_pw[0]=0; g_wifi_userlen=0; g_wifi_user[0]=0; g_wifi_entry=ap_eap(idx)?2:1;
    if(!g_aps[idx].ssid[0]){ g_wifi_hidlen=0; g_wifi_hid[0]=0; g_wifi_sel=-2; g_wifi_entry=3; if(g_wifipop){ g_wifipop=0; { extern int g_stpg; g_stpg=40; } { extern void settings_clear_search(void); settings_clear_search(); } open_win(W_SETTINGS); } } }
int wifi_entry_key(int c){
    if(!g_wifi_entry)return 0;
    if(c==27){ wifi_entry_cancel(); return 1; }
    if(g_wifi_entry==3){ if(c=='\n'){ g_wifi_hid[g_wifi_hidlen]=0; if(g_wifi_hidlen){ g_wifi_entry=1; g_wifi_sel=-2; g_wifi_pwlen=0; } } else if(c=='\b'){ if(g_wifi_hidlen>0)g_wifi_hidlen--; } else if(c>=32&&c<127&&g_wifi_hidlen<32)g_wifi_hid[g_wifi_hidlen++]=(char)c; g_wifi_hid[g_wifi_hidlen]=0; return 1; }
    if(g_wifi_entry==2){ if(c=='\n'||c=='\t'){ g_wifi_user[g_wifi_userlen]=0; if(g_wifi_userlen)g_wifi_entry=1; } else if(c=='\b'){ if(g_wifi_userlen>0)g_wifi_userlen--; } else if(c>=32&&c<127&&g_wifi_userlen<62)g_wifi_user[g_wifi_userlen++]=(char)c; g_wifi_user[g_wifi_userlen]=0; return 1; }
    if(c=='\n'){ g_wifi_entry=0; if(g_wifi_sel==-2){ g_wifi_pw[g_wifi_pwlen]=0; compose_present(); wifi_connect_ssid(g_wifi_hid,g_wifi_pw,"",1); for(int i=0;i<64;i++)g_wifi_pw[i]=0; g_wifi_pwlen=0; g_wifi_sel=-1; } else wifi_gui_connect(g_wifi_sel); return 1; }
    if(c=='\b'){ if(g_wifi_pwlen>0)g_wifi_pwlen--; return 1; }
    if(c>=32&&c<127&&g_wifi_pwlen<63)g_wifi_pw[g_wifi_pwlen++]=(char)c; return 1; }
const char* wifi_sec_name(const scan_ap* a){ if(a->sec==0)return "Open"; if((a->akm&AKM_8021X)&&!(a->akm&(AKM_PSK|AKM_SAE)))return "Enterprise"; if((a->akm&AKM_SAE)&&!(a->akm&AKM_PSK))return "WPA3"; if(a->akm&AKM_SAE)return "WPA2/WPA3"; if(a->sec==1)return "WEP"; return "WPA2"; }
void wifi_bars(int x,int y,int bars,u32 on,u32 off){ for(int b=0;b<4;b++)fill_rect(x+b*5,y+12-b*3-3,3,3+b*3,b<bars?on:off); }
void wifi_lock(int x,int y,u32 c){ fill_rect(x,y+4,8,6,c); fill_rect(x+1,y,1,4,c); fill_rect(x+6,y,1,4,c); fill_rect(x+2,y-1,4,1,c); }
int nic_index_wifi(void){ if(!pci_n)pci_scan(); for(int k=0;k<pci_n;k++) if(pci_cls[k]==0x02&&pci_sub[k]==0x80)return k; return -1; }
u32 rtw_find_mmio(void){
    if(g_wifi_mmio)return g_wifi_mmio;
    int wi=nic_index_wifi(); if(wi<0)return 0;
    if(pci_ven[wi]!=0x10EC||(pci_dev[wi]!=0xC821&&pci_dev[wi]!=0xB821&&pci_dev[wi]!=0xC82A&&pci_dev[wi]!=0xC82B))return 0;
    int bus=pci_bus[wi],sl=pci_slot[wi];
    u32 c4=pci_cfg(bus,sl,0,0x04); pci_cfgw(bus,sl,0,0x04,c4|0x06);
    for(int bi=0;bi<6;bi++){ u32 bar=pci_cfg(bus,sl,0,0x10+bi*4); if(!bar)continue;
        if(!(bar&1)){ u32 a=bar&0xFFFFFFF0u; if(a>0x100000u)g_wifi_mmio=a; } }
    return g_wifi_mmio;
}
void wifi_gui_scan(int full){ if(!rtw_find_mmio()){ wm_status_set("No Wi-Fi adapter found"); return; } if(!full&&!g_wm_up){ wm_status_set("Wi-Fi is off - press Scan"); return; } wifi_scan_start(full); }
static void wifi_icon_glyph(int cx,int cy,u32 col){
    /* three stacked arcs + dot, simple Wi-Fi symbol */
    for(int r=2;r<=8;r+=3){ for(int a=-r;a<=r;a++){ int y=(int)((r*r-a*a)); if(y<0)continue;
        /* approximate arc top: y-offset ~ sqrt */ int yo=0,tt=r*r-a*a; while((yo+1)*(yo+1)<=tt)yo++;
        int px=cx+a, py=cy-yo; if(px>=0&&py>=0&&px<(int)W&&py<27) putpx(px,py,col); } }
    fill_circle(cx,cy,1,col);
}
int g_tby=0, g_tbh=56, g_lbx_end=0, g_tray_left=0;
static void glass(int x,int y,int w,int h,int r,u32 c,int a){ if(g_gfx){ gfx_round(x,y,w,h,r,15,c,a); return; } for(int j=0;j<h;j++){ int yy=y+j; if(yy<0||(u32)yy>=H)continue; for(int i=0;i<w;i++){ if(cut(i,j,w,h,r,15))continue; int xx=x+i; if(xx<0||(u32)xx>=W)continue; u32*p=&SCENE[(u64)yy*SW+xx]; *p=mix(*p,c,a); } } }
static void tb_btn(int x,int y,int w,int h,int on){ int hov=(mx>=x&&mx<x+w&&my>=y&&my<y+h); if(on||hov) glass(x,y,w,h,8,rgb(255,255,255),on?60:34); }
static void ic_launcher(int cx,int cy){ static const u32 C[6]={0xF0506E,0xF5A623,0x7ED321,0x2FB8F0,0x5B6CF0,0xC86DD7};
    fill_circle(cx,cy,14,rgb(250,250,252)); for(int k=0;k<6;k++){ int dx[6]={0,7,7,0,-7,-7}, dy[6]={-8,-4,4,8,4,-4}; fill_circle(cx+dx[k],cy+dy[k],4,C[k]); } fill_circle(cx,cy,3,rgb(250,250,252)); }
static void draw_dock(void);
static void draw_topbar(void){ draw_dock(); }
#define TB_SEP rgb(92,90,122)
static void tb_sep(int x){ fill_rect(x,g_tby+18,1,g_tbh-36,TB_SEP); }
static void draw_tray(void){ if(!g_gm_ready)gmetrics();
    int y=g_tby, h=g_tbh, cy=y+h/2, slot=38; u32 tc=rgb(232,236,246);
    int x=(int)W-22;
    g_bellx=x-slot+6; { int sx=g_bellx-6; tb_btn(sx,cy-17,slot-2,34,g_notifpanel); int bx=sx+(slot-2)/2-6,by=cy-8; fill_round(bx+2,by,8,10,3,15,tc); fill_rect(bx,by+10,12,2,tc); fill_rect(bx+4,by+12,4,2,tc); { focus_mode* f=focus_get(g_focus_cur); if(f&&f->show_tb){ fill_circle(bx+6,by+6,11,f->color); focus_glyph(bx+6,by+6,f->icon,rgb(255,255,255)); } else if(g_notif_n>0) fill_circle(bx+12,by+1,4,rgb(236,72,72)); } }
    x=g_bellx-6-10; tb_sep(x);
    int hh,mm,ss,yy2,mo,dd; read_clock(&hh,&mm,&ss,&yy2,&mo,&dd); (void)ss;
    char t[8],*p=t; p=u2(p,hh);*p++=':';p=u2(p,mm); *p=0;
    char dt[16],*q=dt; q=udec(q,(u32)yy2); *q++='/'; q=u2(q,mo); *q++='/'; q=u2(q,dd); *q=0;
    int cw=pw_str(t,2), dw=pw_str(dt,1); int bw=(cw>dw?cw:dw)+20; g_clockx=x-10-bw;
    tb_btn(g_clockx,cy-26,bw,52,g_clockpop); pdstr_ctr(g_clockx+bw/2,cy-21,t,tc,2); pdstr_ctr(g_clockx+bw/2,cy+11,dt,rgb(186,190,212),1);
    x=g_clockx-10; tb_sep(x);
    int sx=x-10-slot;
    g_qsx=sx+12; tb_btn(sx,cy-17,slot-2,34,g_qs); for(int r=0;r<3;r++){ int yy=cy-6+r*5; fill_rect(g_qsx,yy,14,2,tc); int kx=g_qsx+((r*5)%9); fill_rect(kx,yy-1,3,4,ACC); }
    sx-=slot; g_wifix=sx+5; tb_btn(sx,cy-17,slot-2,34,g_wifipop); wifi_icon_glyph(sx+(slot-2)/2,cy+6, g_apn>0?tc:rgb(160,164,190));
    if(bat_present){ sx-=slot; tb_btn(sx,cy-17,slot-2,34,0); draw_battery(sx+6,cy-6); }
    sx-=slot; { tb_btn(sx,cy-17,slot-2,34,0); int vx=sx+9; fill_rect(vx,cy-3,4,6,tc); for(int k=0;k<5;k++)fill_rect(vx+4+k,cy-3-k,1,6+2*k,tc); for(int k=0;k<3;k++) fill_rect(vx+12+k*3,cy-2-k*2,1,4+k*4,tc); }
    g_wsx=-1000; g_tray_left=sx-10; tb_sep(g_tray_left);
    { extern int dl_active(void); extern int dl_progress(char*,int); if(dl_active()){ char nm[48]; int pc=dl_progress(nm,sizeof nm); int pw2=200, px=g_tray_left-pw2-10, py=cy-15;
        fill_round(px,py,pw2,30,10,15,rgb(40,44,60)); pdstr(px+10,py+3,nm,tc,1);
        int bw=pw2-20; fill_round(px+10,py+21,bw,5,2,15,rgb(70,76,96)); if(pc>=0){ fill_round(px+10,py+21,pc*bw/100,5,2,15,ACC); } else { int t=(int)(g_ticks/2%(u64)(bw-40)); fill_round(px+10+t,py+21,40,5,2,15,ACC); }
        g_tray_left=px-6; } }
}
static void dock_icon(int k,int x,int y){ switch(k){ case 0:ic_folder(x,y);break; case 1:ic_term(x,y);break; case 2:ic_edit(x,y);break; case 3:ic_web(x,y);break; case 4:ic_calc(x,y);break; case 5:ic_clock(x,y);break; case 6:ic_calendar(x,y);break; case 7:ic_sysmon(x,y);break; case 8:ic_snake(x,y);break; case 9:ic_flappy(x,y);break; case 10:ic_paint(x,y);break; default:ic_gear(x,y);break; } }
#define C_FACE rgb(212,208,200)
static int g_bevel_glass=0;
static void bevel(int x,int y,int w,int h,int in){ u32 lt=in?rgb(128,128,128):rgb(255,255,255), dk=in?rgb(255,255,255):rgb(128,128,128), bk=in?rgb(255,255,255):rgb(64,64,64);
    if(g_bevel_glass&&g_gfx&&g_gfx_fx){ gfx_shadow(x,y,w,h,0); gfx_glass(x,y,w,h,0,C_FACE,218,2); } else fill_rect(x,y,w,h,C_FACE); fill_rect(x,y,w,1,lt); fill_rect(x,y,1,h,lt); fill_rect(x,y+h-1,w,1,bk); fill_rect(x+w-1,y,1,h,bk); fill_rect(x+1,y+h-2,w-2,1,dk); fill_rect(x+w-2,y+1,1,h-2,dk);
    if(in){ fill_rect(x+1,y+1,w-2,1,rgb(64,64,64)); fill_rect(x+1,y+1,1,h-2,rgb(64,64,64)); } }
static const u32 APPCOL[NWIN]={0x3C6EDC,0x1C202C,0x3C6EDC,0x6070F0,0x50586A,0x2890E6,0xF0F0F4,0x1C202C,0xE04848,0x2E9848,0x60B0EC,0x3C6EDC,0xF0F0F4,0xA02020,0x7A8294,0x3C6EDC,0x10A37F,0xD04A2A,0x1C202C,0x1C202C,0x202838,0x202838,0x2A3A5A,0x2E9858,0xD04040,0x2B579A,0x6B4A2A,0x76B900};
extern u8 g_aicon[]; extern void icon_choice_draw(int x,int y,int s,int ic);
static void mini_icon(int app,int x,int y){ if(app>=0&&app<NWIN&&g_aicon[app]){ icon_choice_draw(x,y,16,g_aicon[app]-1); return; } u32 c=(app>=0&&app<NWIN)?APPCOL[app]:rgb(120,120,140); fill_rect(x,y,16,16,c); fill_rect(x,y,16,1,mix(c,rgb(255,255,255),90)); fill_rect(x+15,y,1,16,mix(c,rgb(0,0,0),90)); fill_rect(x,y+15,16,1,mix(c,rgb(0,0,0),90));
    const char* nm=(app>=0&&app<NWIN)?APPNAME[app]:"?"; char t[2]={nm[0],0}; int light=((c>>16&255)+(c>>8&255)+(c&255))>600; dstr(x+4,y+1,t,light?rgb(30,30,40):rgb(255,255,255),1); }
