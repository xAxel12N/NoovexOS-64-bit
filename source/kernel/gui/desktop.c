#include "include/nvx/core/sound.h"
#include "include/nvx/base.h"
#include "include/nvx/lib/crypto/nvxwpa.h"
#include "include/nvx/lib/mem.h"
#include "include/nvx/lib/fmt.h"
#include "include/nvx/arch/x86_64/simd.h"
#include "include/nvx/drivers/video/framebuffer.h"
#include "include/nvx/drivers/rtc/cmos.h"
#include "include/nvx/drivers/bus/pci.h"
#include "include/nvx/drivers/usb/xhci.h"
#include "include/nvx/net/http.h"
#include "include/nvx/gui/theme.h"
#include "include/nvx/gui/wm.h"
#include "include/nvx/fs/ramfs.h"
#include "include/nvx/fs/nvxfs.h"
#include "include/nvx/drivers/wifi/rtw_efuse.h"
#include "include/nvx/drivers/wifi/rtw8821ce.h"
#include "include/nvx/drivers/wifi/rtw_sec.h"
#include "include/nvx/core/power.h"
#include "include/nvx/drivers/acpi/acpi_dev.h"
#include "include/nvx/apps/editor.h"
extern int xapps_count(void); extern const char* app_name(int); extern int app_icon_draw(int,int,int,int); extern void app_launch(int);
#include "include/nvx/apps/browser.h"
#include "include/nvx/apps/settings.h"
#include "include/nvx/apps/files_classic.h"
#include "include/nvx/apps/file_explorer.h"
#include "include/nvx/apps/small_apps.h"
#include "include/nvx/apps/logs.h"
#include "include/nvx/gui/desktop.h"
#include "include/nvx/gui/input.h"
#include "include/nvx/init/splash.h"
#include "include/nvx/init/setup.h"
#include "include/nvx/apps/videoplayer.h"
#include "include/nvx/drivers/video/gfx.h"
#include "include/nvx/mm/heap.h"
#include "include/nvx/core/klog.h"
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
static void draw_icon(int k){ int x=IC_X,y=IC_Y[k],ix=x+14;
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
    if(g_wallmode==1 && wall_custom_slot>=0 && FS[wall_custom_slot].used){ if(ensure_img(wall_custom_slot)&&view_w>0){ wall_blit_cover(); return; } }
    if(th<0){ wall_lowpoly(); return; }
    if(th<0)th=0; if(th>=NWALL)th=NWALL-1; wall_decode(th);
    if(wall_sw<=0){ wall_gradient(rgb(38,52,132),rgb(70,80,150)); return; }
    wall_smooth(wallsrc,wall_sw,wall_sh,0,0,(int)W,(int)H);
}
void build_bg(void){ draw_wallpaper(g_theme); memcpy(BG,SCENE,(u64)W*H*4); { extern int wall_enhance(u32*,int,int,int); wall_enhance(BG,(int)W,(int)H,(int)SW); } if(g_gfx) gq_bump(gs_bg); }

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
static int ec_wait_ibf(void){ for(int t=0;t<300000;t++){ if(!(inb(EC_CMD)&0x02))return 1; io_wait(); } return 0; }
static int ec_wait_obf(void){ for(int t=0;t<300000;t++){ if(inb(EC_CMD)&0x01)return 1; io_wait(); } return 0; }
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
    if(!bat_via_ec){ if(g_ticks-bat_poll_t<100)return; bat_poll_t=g_ticks;   /* keep retrying ~1s until the EC answers */
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

void wifi_gui_connect(int idx){
    if(idx<0||idx>=g_apn)return;
    u32 m=rtw_find_mmio(); if(!m||!g_rxnum){ scopy(g_wifi_status,"init Wi-Fi in Terminal first (init/allbuilt/macinit/fw/rings)"); return; }
    /* show progress immediately so the UI isn't a silent freeze */
    { char* p=g_wifi_status; p=scopy(p,"connecting to "); int j=0; while(g_aps[idx].ssid[j]&&j<24){*p++=g_aps[idx].ssid[j];j++;} p=scopy(p,"..."); *p=0; }
    compose_present();
    g_chquiet=1;
    int r;
    if(g_aps[idx].sec==0) r=rtw_assoc(m,&g_aps[idx],g_aps[idx].ssid);
    else r=rtw_wpa2_join(m,&g_aps[idx],g_aps[idx].ssid,g_wifi_pw);
    g_chquiet=0;
    if(r==0){ char* p=g_wifi_status; p=scopy(p,g_aps[idx].sec==0?"CONNECTED (open) to ":"CONNECTED to "); int j=0; while(g_aps[idx].ssid[j]&&j<24){*p++=g_aps[idx].ssid[j];j++;} *p=0;
        int k=0; while(g_aps[idx].ssid[k]&&k<32){g_wifi_conn[k]=g_aps[idx].ssid[k];k++;} g_wifi_conn[k]=0;
        notify("Wi-Fi", g_wifi_conn); }
    else if(r==1) scopy(g_wifi_status,"FAILED: no auth response (AP not answering)");
    else if(r==2) scopy(g_wifi_status,"FAILED: auth rejected by AP");
    else if(r==3) scopy(g_wifi_status,"FAILED: no assoc response");
    else if(r==4) scopy(g_wifi_status,"FAILED: assoc rejected (security mismatch)");
    else if(r==5) scopy(g_wifi_status,"FAILED: associated but AP didn't start handshake");
    else if(r==6) scopy(g_wifi_status,"FAILED: wrong password (handshake MIC rejected)");
    else scopy(g_wifi_status,"FAILED: handshake error");
    compose_present();
}
int nic_index_wifi(void){ if(!pci_n)pci_scan(); for(int k=0;k<pci_n;k++) if(pci_cls[k]==0x02&&pci_sub[k]==0x80)return k; return -1; }
u32 rtw_find_mmio(void){
    if(g_wifi_mmio)return g_wifi_mmio;
    int wi=nic_index_wifi(); if(wi<0)return 0;
    int bus=pci_bus[wi],sl=pci_slot[wi];
    u32 c4=pci_cfg(bus,sl,0,0x04); pci_cfgw(bus,sl,0,0x04,c4|0x06);
    for(int bi=0;bi<6;bi++){ u32 bar=pci_cfg(bus,sl,0,0x10+bi*4); if(!bar)continue;
        if(!(bar&1)){ u32 a=bar&0xFFFFFFF0u; if(a>0x100000u)g_wifi_mmio=a; } }
    return g_wifi_mmio;
}
void wifi_gui_scan(int full){
    u32 m=rtw_find_mmio(); if(!m){ scopy(g_wifi_status,"no Wi-Fi adapter"); return; }
    if(!g_rxnum){ scopy(g_wifi_status,"init Wi-Fi in Terminal first"); return; }
    g_wifi_scanning=1; g_chquiet=1;
    g_apn=0; rtw_rx_ring_up(m);
    static const u8 qc[]={1,6,11,2,3,4,5,7,8,9,10,12,13};
    int nch=full?13:3;
    for(int ci=0;ci<nch;ci++){ rtw_set_channel_2g(m,qc[ci]); ui_pump(); for(int w=0;w<4;w++){ rtw_delay_us(62000); ui_pump(); rtw_scan_drain(m,qc[ci]); } }
    g_wifi_scanning=0; g_chquiet=0;
    char* p=g_wifi_status; p=scopy(p,"found "); p=udec(p,(u32)g_apn); p=scopy(p," networks"); *p=0;
}
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
    g_bellx=x-slot+6; { int sx=g_bellx-6; tb_btn(sx,cy-17,slot-2,34,g_notifpanel); int bx=sx+(slot-2)/2-6,by=cy-8; fill_round(bx+2,by,8,10,3,15,tc); fill_rect(bx,by+10,12,2,tc); fill_rect(bx+4,by+12,4,2,tc); if(g_notif_n>0) fill_circle(bx+12,by+1,4,rgb(236,72,72)); }
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
}
static void dock_icon(int k,int x,int y){ switch(k){ case 0:ic_folder(x,y);break; case 1:ic_term(x,y);break; case 2:ic_edit(x,y);break; case 3:ic_web(x,y);break; case 4:ic_calc(x,y);break; case 5:ic_clock(x,y);break; case 6:ic_calendar(x,y);break; case 7:ic_sysmon(x,y);break; case 8:ic_snake(x,y);break; case 9:ic_flappy(x,y);break; case 10:ic_paint(x,y);break; default:ic_gear(x,y);break; } }
#define C_FACE rgb(192,192,192)
static int g_bevel_glass=0;
static void bevel(int x,int y,int w,int h,int in){ u32 lt=in?rgb(128,128,128):rgb(255,255,255), dk=in?rgb(255,255,255):rgb(128,128,128), bk=in?rgb(255,255,255):rgb(0,0,0);
    if(g_bevel_glass&&g_gfx&&g_gfx_fx){ gfx_shadow(x,y,w,h,0); gfx_glass(x,y,w,h,0,C_FACE,218,2); } else fill_rect(x,y,w,h,C_FACE); fill_rect(x,y,w,1,lt); fill_rect(x,y,1,h,lt); fill_rect(x,y+h-1,w,1,bk); fill_rect(x+w-1,y,1,h,bk); fill_rect(x+1,y+h-2,w-2,1,dk); fill_rect(x+w-2,y+1,1,h-2,dk);
    if(in){ fill_rect(x+1,y+1,w-2,1,rgb(0,0,0)); fill_rect(x+1,y+1,1,h-2,rgb(0,0,0)); } }
static const u32 APPCOL[NWIN]={0x3C6EDC,0x1C202C,0x3C6EDC,0x6070F0,0x50586A,0x2890E6,0xF0F0F4,0x1C202C,0xE04848,0x2E9848,0x60B0EC,0x3C6EDC,0xF0F0F4,0xA02020,0x7A8294,0x3C6EDC,0x10A37F,0xD04A2A,0x1C202C,0x1C202C,0x202838,0x202838,0x2A3A5A,0x2E9858,0xD04040,0x2B579A,0x6B4A2A,0x76B900};
static void mini_icon(int app,int x,int y){ u32 c=(app>=0&&app<NWIN)?APPCOL[app]:rgb(120,120,140); fill_rect(x,y,16,16,c); fill_rect(x,y,16,1,mix(c,rgb(255,255,255),90)); fill_rect(x+15,y,1,16,mix(c,rgb(0,0,0),90)); fill_rect(x,y+15,16,1,mix(c,rgb(0,0,0),90));
    const char* nm=(app>=0&&app<NWIN)?APPNAME[app]:"?"; char t[2]={nm[0],0}; int light=((c>>16&255)+(c>>8&255)+(c&255))>600; dstr(x+4,y+1,t,light?rgb(30,30,40):rgb(255,255,255),1); }
static void nv_logo_small(int x,int y){ fill_round(x,y,16,16,3,15,rgb(40,60,150)); fill_round(x+2,y+2,12,12,2,15,rgb(90,130,230)); dstr(x+4,y+1,"N",rgb(255,255,255),1); }
int g_tbtn_n=0, g_tbtn_x[16], g_tbtn_w[16], g_tbtn_app[16]; int g_tray_x0=0, g_airplane=0, g_nightlight=0, g_battsave=0, g_dnd=0;
static void draw_classic_bar(void){
    g_tbh=30; g_tby=(int)H-g_tbh; DOCK_x=-20000; DOCK_w=0; DOCK_y=g_tby; DOCK_h=g_tbh;
    if(g_gfx&&g_gfx_fx) gfx_glass(0,g_tby,(int)W,g_tbh,0,C_FACE,212,2); else fill_rect(0,g_tby,(int)W,g_tbh,C_FACE); fill_rect(0,g_tby,(int)W,1,rgb(223,223,223)); fill_rect(0,g_tby+1,(int)W,1,rgb(255,255,255));
    int sb_w=96; bevel(3,g_tby+4,sb_w,g_tbh-7,g_launcher); nv_logo_small(9+(g_launcher?1:0),g_tby+7+(g_launcher?1:0));
    dstr(30+(g_launcher?1:0),g_tby+8+(g_launcher?1:0),"NoovexOS",rgb(0,0,0),1); dstr(31+(g_launcher?1:0),g_tby+8+(g_launcher?1:0),"NoovexOS",rgb(0,0,0),1);
    g_lbx_end=3+sb_w;
    int hh,mm,ss,yy2,mo,dd; read_clock(&hh,&mm,&ss,&yy2,&mo,&dd); (void)ss;(void)yy2;(void)mo;(void)dd; char t[8],*p=t; p=u2(p,hh);*p++=':';p=u2(p,mm);*p=0;
    int cw=56, tw2=5*22+6, bxw=tw2+cw; int bx0=(int)W-bxw-4; bevel(bx0,g_tby+4,bxw,g_tbh-7,1); fill_rect(bx0+2,g_tby+6,bxw-4,g_tbh-11,C_FACE);
    g_clockx=bx0+tw2; dstr_ctr(g_clockx+cw/2-2,g_tby+8,t,rgb(0,0,0),1);
    g_tray_x0=bx0+4; for(int k=0;k<5;k++){ int ix=g_tray_x0+k*22, cy=g_tby+15; u32 ic=rgb(0,0,0);
        if((k==1||k==2||k==3)&&g_qs) fill_rect(ix,g_tby+6,20,g_tbh-11,rgb(168,168,176));
        if(k==4&&g_notifpanel) fill_rect(ix,g_tby+6,20,g_tbh-11,rgb(168,168,176));
        if(k==0){ for(int r=0;r<4;r++) fill_rect(ix+10-r,cy-1+r,1+2*r,1,ic); }
        else if(k==1){ if(g_airplane){ fill_rect(ix+9,cy-6,2,12,ic); fill_rect(ix+4,cy-1,12,2,ic); fill_rect(ix+7,cy+4,6,2,ic); }
            else { for(int a=0;a<3;a++){ int rr=3+a*3; for(int dx=-rr;dx<=rr;dx++){ int dy2=rr*rr-dx*dx; int d=0; while((d+1)*(d+1)<=dy2)d++; if(dx>=-d&&dx<=d) putpx(ix+10+dx,cy+4-d,(a==2&&g_apn==0)?rgb(128,128,128):ic); } } fill_rect(ix+9,cy+3,3,3,ic); } }
        else if(k==2){ fill_rect(ix+4,cy-2,4,5,ic); for(int q=0;q<5;q++) fill_rect(ix+8+q,cy-2-q,1,5+2*q,ic); if(g_snd_vol==0){ for(int q=0;q<5;q++){ putpx(ix+14+q,cy-2+q,ic); putpx(ix+18-q,cy-2+q,ic); } } else { fill_rect(ix+15,cy-1,1,3,ic); if(g_snd_vol>50) fill_rect(ix+17,cy-3,1,7,ic); } }
        else if(k==3){ if(bat_present){ fill_rect(ix+3,cy-4,14,9,ic); fill_rect(ix+4,cy-3,12,7,C_FACE); fill_rect(ix+17,cy-2,2,5,ic); int fw=bat_pct*10/100; fill_rect(ix+5,cy-2,fw,5,bat_pct<=15?rgb(200,0,0):rgb(0,128,0)); }
            else { fill_rect(ix+6,cy-6,2,4,ic); fill_rect(ix+12,cy-6,2,4,ic); fill_rect(ix+4,cy-2,12,5,ic); fill_rect(ix+9,cy+3,2,4,ic); } }
        else { fill_round(ix+6,cy-6,8,10,3,15,ic); fill_rect(ix+4,cy+3,12,2,ic); fill_rect(ix+8,cy+5,4,2,ic); if(g_notif_n>0&&!g_dnd) fill_circle(ix+15,cy-5,3,rgb(220,0,0)); if(g_dnd){ fill_circle(ix+15,cy-5,3,rgb(0,0,128)); fill_rect(ix+13,cy-5,5,1,rgb(255,255,255)); } } }
    g_bellx=(int)W+100; g_qsx=-1000; g_wifix=-1000; g_wsx=-1000; g_tray_left=bx0;
    g_tbtn_n=0; int bx=g_lbx_end+6, avail=g_clockx-6-bx; int n=0; for(int i=0;i<NWIN;i++) if(WN[i].open&&WN[i].ws==g_ws)n++;
    int bw=n?avail/n:160; if(bw>160)bw=160; if(bw<40)bw=40;
    for(int i=0;i<NWIN&&g_tbtn_n<16;i++){ if(!WN[i].open||WN[i].ws!=g_ws)continue; if(bx+bw>g_clockx-4)break; int act=(focus==i&&!WN[i].min);
        bevel(bx,g_tby+4,bw-3,g_tbh-7,act); if(act){ for(int yy=g_tby+6;yy<g_tby+g_tbh-5;yy++)for(int xx=bx+2;xx<bx+bw-5;xx++) if(((xx+yy)&1)==0)putpx(xx,yy,rgb(255,255,255)); }
        mini_icon(i,bx+5+act,g_tby+7+act); char tt[24]; int k=0; const char* nm=APPNAME[i]; int maxc=(bw-34)/8; if(maxc>22)maxc=22; while(nm[k]&&k<maxc){ tt[k]=nm[k]; k++; } tt[k]=0;
        dstr(bx+26+act,g_tby+8+act,tt,rgb(0,0,0),1); g_tbtn_x[g_tbtn_n]=bx; g_tbtn_w[g_tbtn_n]=bw-3; g_tbtn_app[g_tbtn_n]=i; g_tbtn_n++; bx+=bw; }
}
static void cqs_geom(int*x,int*y,int*w,int*h){ *w=360; *h=290; *x=(int)W-*w-4; *y=g_tby-*h-2; }
static void cqs_tile(int k,int*x,int*y,int*w,int*h){ int px,py,pw,ph; cqs_geom(&px,&py,&pw,&ph); *w=(pw-16-16)/3; *h=60; *x=px+8+(k%3)*(*w+8); *y=py+10+(k/3)*(*h+8); }
static void cqs_slider_geom(int k,int*x,int*y,int*w){ int px,py,pw,ph; cqs_geom(&px,&py,&pw,&ph); *x=px+44; *y=py+164+k*38; *w=pw-44-18; }
static const char* CQS_N[6]={"Wi-Fi","Airplane","Night light","Battery saver","Do not disturb","All settings"};
static int cqs_on(int k){ return k==0?(g_apn>0&&!g_airplane):k==1?g_airplane:k==2?g_nightlight:k==3?g_battsave:k==4?g_dnd:0; }
static void cqs_draw(void){ int px,py,pw,ph; cqs_geom(&px,&py,&pw,&ph); g_bevel_glass=1; bevel(px,py,pw,ph,0); g_bevel_glass=0;
    for(int k=0;k<6;k++){ int x,y,w,h; cqs_tile(k,&x,&y,&w,&h); int on=cqs_on(k); bevel(x,y,w,h,on); if(on) fill_rect(x+2,y+2,w-4,h-4,rgb(0,0,128));
        u32 fc=on?rgb(255,255,255):rgb(0,0,0); int cx=x+w/2, cy=y+18;
        if(k==0){ for(int a=0;a<3;a++){ int rr=3+a*3; for(int dx=-rr;dx<=rr;dx++){ int dy2=rr*rr-dx*dx; int d=0; while((d+1)*(d+1)<=dy2)d++; putpx(cx+dx,cy+5-d,fc); } } fill_rect(cx-1,cy+4,3,3,fc); }
        else if(k==1){ fill_rect(cx-1,cy-7,2,14,fc); fill_rect(cx-7,cy-1,14,2,fc); fill_rect(cx-4,cy+5,8,2,fc); }
        else if(k==2){ fill_circle(cx,cy,7,fc); fill_circle(cx+3,cy-3,6,on?rgb(0,0,128):C_FACE); }
        else if(k==3){ fill_rect(cx-8,cy-5,16,10,fc); fill_rect(cx-7,cy-4,14,8,on?rgb(0,0,128):C_FACE); fill_rect(cx+8,cy-2,2,4,fc); fill_rect(cx-5,cy-2,5,4,rgb(0,160,0)); }
        else if(k==4){ fill_circle(cx,cy,7,fc); fill_rect(cx-4,cy-1,8,2,on?rgb(0,0,128):C_FACE); }
        else { fill_circle(cx,cy,6,fc); fill_circle(cx,cy,3,on?rgb(0,0,128):C_FACE); for(int q=0;q<8;q++){ int dx[8]={0,5,7,5,0,-5,-7,-5},dy[8]={-7,-5,0,5,7,5,0,-5}; fill_rect(cx+dx[q]-1,cy+dy[q]-1,2,2,fc); } }
        if(!g_gm_ready)gmetrics(); pdstr_ctr(cx,y+38,CQS_N[k],fc,1); }
    for(int k=0;k<2;k++){ int x,y,w; cqs_slider_geom(k,&x,&y,&w); int v=k==0?(g_bl.ok?(g_bl.cur<0?0:g_bl.cur):-1):g_snd_vol; u32 ic=v<0?rgb(128,128,128):rgb(0,0,0);
        if(k==0){ fill_circle(x-22,y+6,4,ic); for(int q=0;q<8;q++){ int dx[8]={0,6,8,6,0,-6,-8,-6},dy[8]={-8,-6,0,6,8,6,0,-6}; putpx(x-22+dx[q],y+6+dy[q],ic); } }
        else { fill_rect(x-28,y+3,4,6,ic); for(int q=0;q<5;q++) fill_rect(x-24+q,y+3-q,1,6+2*q,ic); }
        if(v>=0){ bevel(x,y+3,w,6,1); int kx=x+v*(w-11)/100; bevel(kx,y-4,11,20,0); } else dstr(x,y-1,k==0?"Brightness: laptop screens only":"",rgb(128,128,128),1); }
    fill_rect(px+6,py+ph-38,pw-12,1,rgb(128,128,128)); fill_rect(px+6,py+ph-37,pw-12,1,rgb(255,255,255));
    { char b[32],*p=b; if(bat_present){ p=udec(p,(u32)bat_pct); p=scopy(p,bat_charging?"%  charging":"%"); } else p=scopy(p,"Plugged in"); *p=0; dstr(px+12,py+ph-28,b,rgb(0,0,0),1); }
    { int gx=px+pw-34, gy=py+ph-32; bevel(gx,gy,26,24,0); fill_circle(gx+13,gy+12,5,rgb(0,0,0)); fill_circle(gx+13,gy+12,2,C_FACE); for(int q=0;q<8;q++){ int dx[8]={0,5,7,5,0,-5,-7,-5},dy[8]={-7,-5,0,5,7,5,0,-5}; fill_rect(gx+13+dx[q]-1,gy+12+dy[q]-1,2,2,rgb(0,0,0)); } }
}
int classic_qs_click(int mx0,int my0){ int px,py,pw,ph; cqs_geom(&px,&py,&pw,&ph); if(mx0<px||mx0>=px+pw||my0<py||my0>=py+ph) return 0;
    for(int k=0;k<6;k++){ int x,y,w,h; cqs_tile(k,&x,&y,&w,&h); if(mx0>=x&&mx0<x+w&&my0>=y&&my0<y+h){
        if(k==0){ g_qs=0; g_wifipop=1; if(g_apn==0)wifi_gui_scan(0); }
        else if(k==1){ g_airplane^=1; }
        else if(k==2){ g_nightlight^=1; }
        else if(k==3){ g_battsave^=1; g_refresh_hz=g_battsave?30:60; }
        else if(k==4){ g_dnd^=1; }
        else { g_qs=0; open_win(W_SETTINGS); }
        return 1; } }
    for(int k=0;k<2;k++){ int x,y,w; cqs_slider_geom(k,&x,&y,&w); if(my0>=y-6&&my0<y+18&&mx0>=x-4&&mx0<x+w+4){ int v=(mx0-x)*100/(w>0?w:1); if(v<0)v=0; if(v>100)v=100;
        if(k==0){ if(g_bl.ok)bl_set(v); } else { g_snd_vol=v; if(g_snd_state)g_snd.volume(v); } return 1; } }
    { int gx=px+pw-34, gy=py+ph-32; if(mx0>=gx&&mx0<gx+26&&my0>=gy&&my0<gy+24){ g_qs=0; open_win(W_SETTINGS); } }
    return 1; }
int classic_tray_click(int px){ if(px<g_tray_x0||px>=g_tray_x0+5*22) return 0; int k=(px-g_tray_x0)/22;
    if(k==4){ g_notifpanel^=1; g_qs=0; } else { g_qs^=1; g_notifpanel=0; g_clockpop=0; } return 1; }
static void night_tint(int y0,int y1){ if(!g_nightlight)return; if(g_gfx){ gq_tint(g_rt,0,y0,(int)W,y1-y0); return; } for(int y=y0;y<y1;y++){ u32* r=&SCENE[(u64)y*SW]; for(u32 x=0;x<W;x++){ u32 c=r[x]; u32 g=(c>>8)&255, b=c&255; r[x]=(c&0xFF0000)|((g*15/16)<<8)|(b*5/8); } } }
void classic_taskbtn_click(int px){ for(int k=0;k<g_tbtn_n;k++) if(px>=g_tbtn_x[k]&&px<g_tbtn_x[k]+g_tbtn_w[k]){ int i=g_tbtn_app[k]; if(focus==i&&!WN[i].min)win_min(i); else focus_win(i); return; } }
static void draw_dock(void){
    if(g_dstyle==1){ draw_classic_bar(); return; }
    int n=12; g_tbh=64; g_tby=(int)H-g_tbh-12; DOCK_h=g_tbh; DOCK_y=g_tby; DOCK_w=n*DOCK_IC+(n-1)*DOCK_GAP+2*DOCK_PAD;
    int bx0=12, bw0=(int)W-24, cy=g_tby+g_tbh/2;
    shadow_soft(bx0,g_tby,bw0,g_tbh,18); glass(bx0,g_tby,bw0,g_tbh,18,rgb(28,26,46),205);
    for(int i=18;i<bw0-18;i++){ u32*p=&SCENE[(u64)g_tby*SW+bx0+i]; *p=mix(*p,rgb(255,255,255),46); }
    { int lx=bx0+12;
      tb_btn(lx,cy-22,44,44,g_launcher); ic_launcher(lx+22,cy);
      int wx=lx+50; tb_btn(wx,cy-22,44,44,0); fill_round(wx+9,cy-12,26,24,6,15,rgb(46,190,150)); fill_rect(wx+14,cy-5,16,2,rgb(255,255,255)); fill_rect(wx+14,cy+1,11,2,rgb(255,255,255));
      int m2=wx+50; tb_btn(m2,cy-22,44,44,0); fill_round(m2+9,cy-12,26,24,6,15,rgb(244,120,90)); for(int k=0;k<4;k++){ int qx=m2+13+(k&1)*10, qy=cy-8+(k>>1)*9; fill_round(qx,qy,8,7,2,15,rgb(255,255,255)); }
      g_lbx_end=m2+44; tb_sep(g_lbx_end+12); }
    draw_tray();
    { int l=g_lbx_end+24, r=g_tray_left-12; DOCK_x=(l+r)/2-DOCK_w/2; if(DOCK_x<l)DOCK_x=l; }
    int hovk=-1, iy=cy-DOCK_IC/2;
    for(int k=0;k<n;k++){ int ix=DOCK_x+DOCK_PAD+k*(DOCK_IC+DOCK_GAP), cxk=ix+DOCK_IC/2; int app=IC_APP[k];
        int hov=(my>=g_tby&&my<g_tby+g_tbh&&mx>=ix-5&&mx<ix+DOCK_IC+5);
        if(hov){ glass(ix-3,cy-26,DOCK_IC+6,52,12,rgb(255,255,255),34); hovk=k; }
        dock_icon(k,ix,iy-1);
        if(WN[app].open) fill_round(cxk-(focus==app?9:4),g_tby+g_tbh-7,focus==app?18:8,3,1,15,(focus==app)?ACC:rgb(200,204,222)); }
    if(hovk>=0){ const char*nm=APPNAME[IC_APP[hovk]]; int cxk=DOCK_x+DOCK_PAD+hovk*(DOCK_IC+DOCK_GAP)+DOCK_IC/2;
        int tw=pw_str(nm,1)+28, tx=cxk-tw/2, ty=g_tby-38; if(tx<6)tx=6; if(tx+tw>(int)W-6)tx=(int)W-6-tw;
        shadow_soft(tx,ty,tw,28,8); glass(tx,ty,tw,28,8,rgb(28,26,46),225); pdstr_ctr(tx+tw/2,ty+8,nm,rgb(240,244,252),1); }
}
static void applemenu_draw(void){ int ph=AM_N*AM_IH+8; shadow_soft(AM_X,AM_Y,AM_W,ph,10); fill_round(AM_X,AM_Y,AM_W,ph,10,15,rgb(250,251,253));
    for(int k=0;k<AM_N;k++){ int iy=AM_Y+4+k*AM_IH; int hov=(mx>=AM_X&&mx<AM_X+AM_W&&my>=iy&&my<iy+AM_IH);
        if(hov) fill_round(AM_X+4,iy,AM_W-8,AM_IH,6,15,ACC); dstr(AM_X+14,iy+8,AM_LBL[k], hov?rgb(255,255,255):INK,1); } }
/* ---- notifications: toast + history panel ---- */
static void toast_frame(void){                        /* advance the toast timer */
    if(g_toast_cur<0) return;
    if(g_toast_until==0){ g_toast_until=g_ticks+400; g_notif[g_toast_cur].shown=1; }   /* ~4 s */
    if(g_ticks>g_toast_until){ g_toast_cur=-1; }
}
static void notif_toast_draw(void){
    if(g_toast_cur<0||g_dnd) return; Notif* nt=&g_notif[g_toast_cur];
    int tw=300, th=nt->body[0]?64:44, tx=(int)W-tw-16, ty=34;
    shadow_soft(tx,ty,tw,th,12); fill_round(tx,ty,tw,th,12,15,g_theme?rgb(34,37,48):rgb(252,253,255));
    fill_rect(tx,ty+10,3,th-20,ACC);
    dstr(tx+16,ty+10,nt->title,g_theme?rgb(236,239,246):rgb(24,28,40),1);
    if(nt->body[0]) dstr(tx+16,ty+32,nt->body,g_theme?rgb(150,158,176):rgb(110,118,138),1);
}
static void wifi_panel_draw(void){
    int pw=320, px=g_wifix-pw+20; if(px<6)px=6;
    int rows=g_apn>7?7:g_apn; int ph=70+rows*30+(g_wifi_entry?70:0)+26; int py=g_tby-ph-10;
    shadow_soft(px,py,pw,ph,12); fill_round(px,py,pw,ph,12,15,g_theme?rgb(28,31,42):rgb(250,251,254));
    dstr(px+16,py+14,"Wi-Fi",g_theme?rgb(236,239,246):rgb(24,28,40),2);
    int bx=px+pw-80, by=py+12; fill_round(bx,by,66,22,6,15, g_wifi_scanning?rgb(150,158,176):ACC); dstr_ctr(bx+33,by+7,g_wifi_scanning?"...":"Scan",rgb(255,255,255),1);
    dstr(px+16,py+38,g_wifi_status,g_theme?rgb(150,158,176):rgb(110,118,138),1);
    int cy=py+66;
    for(int i=0;i<rows;i++){
        int hov=(mx>=px+8&&mx<px+pw-8&&my>=cy&&my<cy+28);
        int open=(g_aps[i].sec==0);
        int isconn=(g_wifi_conn[0] && g_aps[i].ssid[0]); if(isconn){ int k=0; while(g_aps[i].ssid[k]&&g_wifi_conn[k]&&g_aps[i].ssid[k]==g_wifi_conn[k])k++; isconn=(!g_aps[i].ssid[k]&&!g_wifi_conn[k]); }
        if(i==g_wifi_sel) fill_round(px+8,cy,pw-16,28,6,15,mix(ACC,g_theme?rgb(28,31,42):rgb(255,255,255),180));
        else if(hov) fill_round(px+8,cy,pw-16,28,6,15,g_theme?rgb(40,44,58):rgb(234,238,246));
        u32 nmc=g_theme?rgb(224,228,236):rgb(30,34,46);
        dstr(px+16,cy+8,g_aps[i].ssid[0]?g_aps[i].ssid:"<hidden>",nmc,1);
        /* signal dots by SEEN count */
        int sig=g_aps[i].seen>30?4:g_aps[i].seen>15?3:g_aps[i].seen>5?2:1;
        for(int b=0;b<4;b++){ u32 dc=b<sig?(g_theme?rgb(140,200,255):ACC):(g_theme?rgb(60,66,82):rgb(214,220,230)); fill_rect(px+pw-58+b*7,cy+18-b*3,4,3+b*3,dc); }
        if(isconn) dstr(px+pw-92,cy+8,"",rgb(90,200,120),1);
        else { const char* sec=open?"open":"lock"; dstr(px+pw-92,cy+8,sec, open?rgb(90,170,110):rgb(160,140,90),1); }
        cy+=30;
    }
    if(g_apn==0) dstr(px+16,py+66,"(press Scan to find networks)",g_theme?rgb(120,128,146):rgb(150,158,176),1);
    if(g_wifi_entry && g_wifi_sel>=0){
        cy+=6; dstr(px+16,cy,"Password:",g_theme?rgb(200,206,220):rgb(70,78,98),1); cy+=20;
        fill_round(px+16,cy,pw-100,26,5,15,g_theme?rgb(44,48,62):rgb(238,241,247));
        char mask[40]; int n=g_wifi_pwlen>36?36:g_wifi_pwlen; for(int k=0;k<n;k++)mask[k]='*'; mask[n]=0;
        dstr(px+22,cy+7,mask,g_theme?rgb(236,239,246):rgb(24,28,40),1);
        int jx=px+pw-76; fill_round(jx,cy,60,26,5,15,ACC); dstr_ctr(jx+30,cy+7,"Join",rgb(255,255,255),1);
    }
}
static void notif_panel_draw(void){
    int pw=340, px=(int)W-pw-10, py=10, ph=g_tby-20;
    shadow_soft(px,py,pw,ph,0); fill_rect(px,py,pw,ph,g_theme?rgb(26,28,38):rgb(248,249,252));
    fill_rect(px,py,2,ph,g_theme?rgb(48,52,66):rgb(226,229,238));
    dstr(px+18,py+16,"Notifications",g_theme?rgb(236,239,246):rgb(24,28,40),2);
    int cy=py+52; int shown=0;
    for(int q=g_notif_n-1;q>=0 && shown<12;q--){ Notif* nt=&g_notif[q%NOTIF_MAX];
        int ch=nt->body[0]?60:40; fill_round(px+12,cy,pw-24,ch-6,8,15,g_theme?rgb(34,37,48):rgb(255,255,255));
        fill_rect(px+12,cy+8,3,ch-22,ACC);
        dstr(px+26,cy+8,nt->title,g_theme?rgb(232,236,244):rgb(28,32,44),1);
        if(nt->body[0]) dstr(px+26,cy+28,nt->body,g_theme?rgb(150,158,176):rgb(110,118,138),1);
        cy+=ch; shown++; }
    if(g_notif_n==0) dstr(px+18,py+60,"No new notifications",g_theme?rgb(120,128,146):rgb(150,158,176),1);
}
/* ---- workspaces: switcher overlay (shown briefly on change) ---- */
static u64 g_ws_show=0;
void ws_switch(int w){ if(w<0)w=NWS-1; if(w>=NWS)w=0; if(w==g_ws)return; g_ws=w; if(focus>=0&&WN[focus].ws!=g_ws)focus=-1; g_ws_show=g_ticks+120; }
static void ws_overlay_draw(void){
    if(g_ticks>g_ws_show) return;
    int bw=NWS*66+16, bh=54, bx=(int)W/2-bw/2, by=(int)H-140;
    shadow_soft(bx,by,bw,bh,12); fill_round(bx,by,bw,bh,12,15,g_theme?rgb(30,33,44):rgb(250,251,255));
    for(int w=0;w<NWS;w++){ int cx=bx+8+w*66, cyy=by+8; int cnt=0; for(int i=0;i<NWIN;i++) if(WN[i].open&&WN[i].ws==w)cnt++;
        fill_round(cx,cyy,58,38,7,15,w==g_ws?ACC:(g_theme?rgb(44,48,62):rgb(228,232,242)));
        char b[8],*p=b;*p++=(char)('1'+w);*p=0; dstr_ctr(cx+29,cyy+6,b,w==g_ws?rgb(255,255,255):(g_theme?rgb(200,206,220):rgb(90,98,118)),2);
        if(cnt){ char d[8],*q=d;q=udec(q,(u32)cnt);*q=0; dstr_ctr(cx+29,cyy+26,d,w==g_ws?rgb(230,236,255):rgb(120,128,146),1); } }
}
static int anim_hide=-1;
   /* fwd - defined in boot splash section */
/* ---- desktop shortcuts on the wallpaper ---- */
static const int DESK[5]={W_FILES,W_BROWSER,W_TERM,W_PAINT,W_SETTINGS};
static const char* DESK_L[5]={"Files","Browser","Terminal","Paint","Settings"};
/* per-format desktop file icons */
static void file_icon(const char* name,int x,int y){
    if(fext_eq(name,"pptx")){ itile(x,y,rgb(190,74,52)); fill_round(x+11,y+12,26,22,3,15,rgb(255,255,255)); fill_rect(x+15,y+17,18,3,rgb(198,80,58)); fill_rect(x+15,y+23,13,3,rgb(230,150,130)); }
    else if(fext_eq(name,"docx")){ itile(x,y,rgb(40,86,170)); fill_round(x+11,y+11,26,26,3,15,rgb(255,255,255)); dstr(x+17,y+16,"W",rgb(40,86,170),2); }
    else if(fext_eq(name,"png")||fext_eq(name,"jpg")||fext_eq(name,"jpeg")||fext_eq(name,"webp")){ itile(x,y,rgb(36,120,92)); fill_round(x+10,y+12,28,22,3,15,rgb(214,232,224)); fill_round(x+27,y+16,6,6,3,15,rgb(245,205,90)); for(int i=0;i<20;i++){int yy=y+32-(i%7); putpx(x+13+i,yy,rgb(70,150,110)); putpx(x+13+i,yy+1,rgb(70,150,110));} }
    else if(fext_eq(name,"nvx")||fext_eq(name,"c")||fext_eq(name,"h")||fext_eq(name,"rn")){ itile(x,y,rgb(28,36,54)); dstr(x+8,y+16,"</>",rgb(120,220,150),2); }
    else if(fext_eq(name,"zip")||fext_eq(name,"tar")||fext_eq(name,"gz")){ itile(x,y,rgb(120,98,46)); fill_round(x+15,y+10,18,28,3,15,rgb(224,204,140)); for(int j=0;j<26;j+=4)fill_rect(x+22,y+11+j,4,2,rgb(120,98,46)); }
    else if(fext_eq(name,"md")||fext_eq(name,"txt")||fext_eq(name,"cfg")||fext_eq(name,"log")){ itile(x,y,rgb(58,68,94)); fill_round(x+12,y+9,24,30,3,15,rgb(246,248,251)); fill_rect(x+16,y+16,16,2,rgb(150,160,180)); fill_rect(x+16,y+21,16,2,rgb(180,188,204)); fill_rect(x+16,y+26,11,2,rgb(180,188,204)); }
    else { itile(x,y,rgb(60,66,86)); fill_round(x+12,y+9,24,30,3,15,rgb(232,236,244)); fill_rect(x+28,y+9,8,8,rgb(200,206,220)); }
}
/* desktop item positions (grid-snapped pixel coords) */

int g_apx[5], g_apy[5], g_apinit=0;
int g_fpx[FS_N], g_fpy[FS_N]; u8 g_fpl[FS_N];
int g_ddrag=-1, g_ddx=0, g_ddy=0, g_ddsx=0, g_ddsy=0;   /* dragging: -1 none, 0-4 app, 100+slot file */
 
static void desk_init_pos(void){ if(g_apinit)return; g_apinit=1; for(int k=0;k<5;k++){ g_apx[k]=DGX0; g_apy[k]=DGY0+k*DGH; } for(int k=0;k<FS_N;k++)g_fpl[k]=0; }
void desk_place_file(int slot){   /* assign the next free grid cell to a desktop file */
    if(g_fpl[slot])return; desk_init_pos();
    for(int c=0;c<12;c++)for(int r=0;r<8;r++){ int px=DGX0+c*DGW, py=DGY0+r*DGH; int used=0;
        for(int k=0;k<5;k++) if(g_apx[k]==px&&g_apy[k]==py)used=1;
        for(int k=0;k<FS_N;k++) if(g_fpl[k]&&g_fpx[k]==px&&g_fpy[k]==py)used=1;
        if(!used){ g_fpx[slot]=px; g_fpy[slot]=py; g_fpl[slot]=1; return; } }
    g_fpx[slot]=DGX0; g_fpy[slot]=DGY0; g_fpl[slot]=1;
}
void launch_icon(int w,int x,int y){ switch(w){
    case W_FILES:ic_folder(x,y);break; case W_TERM:ic_term(x,y);break; case W_EDITOR:ic_edit(x,y);break; case W_BROWSER:ic_web(x,y);break;
    case W_CALC:ic_calc(x,y);break; case W_CLOCK:ic_clock(x,y);break; case W_CALENDAR:ic_calendar(x,y);break; case W_SYSMON:ic_sysmon(x,y);break;
    case W_LOGS:{ itile(x,y,rgb(12,14,20)); for(int r=0;r<4;r++)fill_rect(x+10,y+11+r*7,28-(r*3),3,rgb(120,255,150)); break; }
    case W_TFIND:{ itile(x,y,rgb(18,20,28)); dstr(x+9,y+13,">_",rgb(120,255,180),2); break; }
    case W_VIDEO:{ itile(x,y,rgb(32,40,56)); fill_round(x+9,y+12,30,22,4,15,rgb(60,140,240)); for(int r=0;r<12;r++){ int l=r<6?r:11-r; fill_rect(x+21,y+17+r,l+1,1,rgb(255,255,255)); } break; }
    case W_SNAKE:ic_snake(x,y);break; case W_FLAPPY:ic_flappy(x,y);break; case W_PAINT:ic_paint(x,y);break; case W_SETTINGS:ic_gear(x,y);break;
    case W_CAMERA:{ itile(x,y,rgb(34,44,70)); fill_round(x+10,y+15,28,20,4,15,rgb(90,100,130)); fill_circle(x+24,y+25,7,rgb(20,24,34)); fill_circle(x+24,y+25,4,rgb(110,170,250)); fill_round(x+18,y+11,12,5,2,15,rgb(90,100,130)); break; }
    case W_QUAKE:{ itile(x,y,rgb(58,38,22)); fill_circle(x+24,y+24,13,rgb(196,140,70)); fill_circle(x+24,y+24,9,rgb(58,38,22)); fill_rect(x+22,y+8,4,30,rgb(196,140,70)); fill_rect(x+16,y+30,16,3,rgb(196,140,70)); break; }
    case W_WRITER:{ itile(x,y,rgb(43,87,154)); fill_round(x+13,y+9,22,30,3,15,rgb(255,255,255)); for(int r=0;r<5;r++)fill_rect(x+17,y+15+r*4,r==4?8:14,2,rgb(43,87,154)); dstr(x+9,y+25,"W",rgb(255,255,255),1); break; }
    case W_RECORDER:{ itile(x,y,rgb(150,30,40)); fill_round(x+19,y+10,10,18,5,15,rgb(255,255,255)); fill_rect(x+16,y+24,2,6,rgb(255,255,255)); fill_rect(x+30,y+24,2,6,rgb(255,255,255)); fill_rect(x+16,y+30,16,2,rgb(255,255,255)); fill_rect(x+23,y+31,2,6,rgb(255,255,255)); fill_rect(x+18,y+37,12,2,rgb(255,255,255)); break; }
    case W_GPU3D:{ itile(x,y,rgb(18,26,20)); fill_round(x+10,y+12,28,22,3,15,rgb(118,185,0)); fill_round(x+13,y+15,22,16,2,15,rgb(18,26,20)); for(int k=0;k<5;k++)fill_rect(x+14+k*4,y+34,2,5,rgb(118,185,0)); fill_circle(x+24,y+23,5,rgb(240,140,60)); fill_circle(x+22,y+21,2,rgb(255,220,180)); break; }
    case W_BENCH:{ itile(x,y,rgb(24,70,52)); for(int k=0;k<4;k++){ int bh=6+k*6; fill_round(x+11+k*7,y+36-bh,5,bh,2,15,k==3?rgb(250,210,90):rgb(120,220,160)); } break; }
    case W_DOOM:{ itile(x,y,rgb(78,18,14)); fill_round(x+11,y+11,26,26,5,15,rgb(196,52,30)); dstr_ctr(x+24,y+17,"D",rgb(255,224,180),2); }break;
    default:{ itile(x,y,rgb(46,52,72)); fill_round(x+13,y+13,22,22,4,15,ACC); dstr_ctr(x+24,y+18,"N",rgb(255,255,255),1); } } }
static void desk_hi(int x,int y){ if(mx>=x-6&&mx<x+54&&my>=y-6&&my<y+74){ if(g_gfx){ gfx_round(x-6,y-6,60,80,12,15,rgb(255,255,255),42); return; } for(int j=-6;j<74;j++)for(int i=-6;i<54;i++){ if(cut(i+6,j+6,60,80,12,15))continue; int dx=x+i,dy=y+j; if((u32)dx<W&&(u32)dy<H){ u32*p=&SCENE[(u64)dy*SW+dx]; *p=mix(*p,rgb(255,255,255),42); } } } }
static void desk_draw(void){ return; desk_init_pos();
    for(int k=0;k<5;k++){ int x=g_apx[k],y=g_apy[k]; if(g_ddrag==k){ x=mx-g_ddx; y=my-g_ddy; } else desk_hi(x,y);
        launch_icon(DESK[k],x,y); dstr_ctr(x+24,y+52,DESK_L[k],rgb(238,242,250),1); }
    for(int s=0;s<FS_N;s++) if(FS[s].used && FS[s].folder==2){ if(!g_fpl[s])desk_place_file(s); int x=g_fpx[s],y=g_fpy[s]; if(g_ddrag==100+s){ x=mx-g_ddx; y=my-g_ddy; } else desk_hi(x,y);
        file_icon(FS[s].name,x,y); char nm[13]; int i=0; for(;FS[s].name[i]&&i<11;i++)nm[i]=FS[s].name[i]; if(FS[s].name[i]){nm[i-1]='.';nm[i]=0;}else nm[i]=0; dstr_ctr(x+24,y+52,nm,rgb(238,242,250),1); } }
static void marq_draw(void){ if(!g_marq)return; int x0=marq_x0,y0=marq_y0,x1=mx,y1=my; if(x1<x0){int t=x0;x0=x1;x1=t;} if(y1<y0){int t=y0;y0=y1;y1=t;}
    int w=x1-x0, h=y1-y0; if(w<2||h<2)return;
    if(g_gfx){ gq_blend(g_rt,x0,y0,w,h,rgb(96,152,240),68); fill_rect(x0,y0,w,1,rgb(60,120,230)); fill_rect(x0,y1-1,w,1,rgb(60,120,230)); fill_rect(x0,y0,1,h,rgb(60,120,230)); fill_rect(x1-1,y0,1,h,rgb(60,120,230)); return; }
    for(int j=0;j<h;j++)for(int i=0;i<w;i++){ int dx=x0+i,dy=y0+j; if((u32)dx<W&&(u32)dy<H){ u32*p=&SCENE[(u64)dy*SW+dx]; *p=mix(*p,rgb(96,152,240),68); } }
    for(int i=0;i<w;i++){ putpx(x0+i,y0,rgb(60,120,230)); putpx(x0+i,y1-1,rgb(60,120,230)); } for(int j=0;j<h;j++){ putpx(x0,y0+j,rgb(60,120,230)); putpx(x1-1,y0+j,rgb(60,120,230)); } }
void prop_draw(void){
    if(!g_prop)return;
    int pw=340, ph=210, x=(int)W/2-pw/2, y=(int)H/2-ph/2;
    shadow_soft(x,y,pw,ph,12); fill_round(x,y,pw,ph,12,15,rgb(244,246,250));
    fill_round(x,y,pw,40,12,15,rgb(52,110,200)); dstr(x+16,y+13,"Properties",rgb(255,255,255),2);
    int lx=x+22, ly=y+58; char b[96],*p;
    if(g_prop_slot>=0 && g_prop_slot<FS_N && FS[g_prop_slot].used){
        file_icon(FS[g_prop_slot].name, x+pw-72, y+56);
        dstr(lx,ly,"Name:",rgb(120,128,146),1); dstr(lx+80,ly,FS[g_prop_slot].name,rgb(30,34,48),1); ly+=28;
        const char* ty="Document"; const char* nm=FS[g_prop_slot].name;
        if(fext_eq(nm,"pptx"))ty="PowerPoint"; else if(fext_eq(nm,"docx"))ty="Word document"; else if(fext_eq(nm,"png")||fext_eq(nm,"jpg")||fext_eq(nm,"jpeg")||fext_eq(nm,"webp"))ty="Image";
        else if(fext_eq(nm,"nvx")||fext_eq(nm,"c")||fext_eq(nm,"h")||fext_eq(nm,"rn"))ty="Source code"; else if(fext_eq(nm,"zip"))ty="Archive"; else if(fext_eq(nm,"md"))ty="Markdown"; else if(fext_eq(nm,"html"))ty="HTML page"; else if(fext_eq(nm,"txt"))ty="Text file"; else if(fext_eq(nm,"cfg"))ty="Config";
        dstr(lx,ly,"Type:",rgb(120,128,146),1); dstr(lx+80,ly,(char*)ty,rgb(30,34,48),1); ly+=28;
        p=b;p=scopy(p,"");u32 L=(u32)FS[g_prop_slot].len; if(L>=1024){p=udec(p,L/1024);*p++='.';p=udec(p,(L%1024)*10/1024);p=scopy(p," KB");}else{p=udec(p,L);p=scopy(p," bytes");}*p=0;
        dstr(lx,ly,"Size:",rgb(120,128,146),1); dstr(lx+80,ly,b,rgb(30,34,48),1); ly+=28;
        const char* loc=FS[g_prop_slot].folder==2?"Desktop":FS[g_prop_slot].folder==1?"Downloads":"Documents";
        dstr(lx,ly,"Location:",rgb(120,128,146),1); dstr(lx+80,ly,(char*)loc,rgb(30,34,48),1); ly+=28;
        dstr(lx,ly,"Storage:",rgb(120,128,146),1); dstr(lx+80,ly,disk_ok?"NVXFS (disk)":"RAM",rgb(30,34,48),1);
    } else if(g_prop_app>=0 && g_prop_app<5){
        launch_icon(DESK[g_prop_app], x+pw-70, y+54);
        dstr(lx,ly,"Name:",rgb(120,128,146),1); dstr(lx+80,ly,(char*)DESK_L[g_prop_app],rgb(30,34,48),1); ly+=28;
        dstr(lx,ly,"Type:",rgb(120,128,146),1); dstr(lx+80,ly,"Application",rgb(30,34,48),1); ly+=28;
        dstr(lx,ly,"Status:",rgb(120,128,146),1); dstr(lx+80,ly,WN[DESK[g_prop_app]].open?"Open":"Closed",rgb(30,34,48),1); ly+=28;
        dstr(lx,ly,"System:",rgb(120,128,146),1); dstr(lx+80,ly,"NoovexOS 64",rgb(30,34,48),1);
    }
    int bw=90,bx=x+pw-bw-16,by=y+ph-40; int over=(mx>=bx&&mx<bx+bw&&my>=by&&my<by+28);
    fill_round(bx,by,bw,28,7,15,over?rgb(40,90,180):rgb(52,110,200)); dstr_ctr(bx+bw/2,by+7,"Close",rgb(255,255,255),1);
}
int desk_hit(int px,int py){ (void)px;(void)py; return -1; for(int k=0;k<5;k++){ if(px>=g_apx[k]&&px<g_apx[k]+48&&py>=g_apy[k]&&py<g_apy[k]+72) return k; }
    for(int s=0;s<FS_N;s++) if(FS[s].used&&FS[s].folder==2&&g_fpl[s]){ if(px>=g_fpx[s]&&px<g_fpx[s]+48&&py>=g_fpy[s]&&py<g_fpy[s]+72) return 100+s; } return -1; }
void desk_open(int it){ if(it<0)return; if(it<100){ focus_win(DESK[it]); return; } int s=it-100; if(s<0||s>=FS_N||!FS[s].used)return; int kd=fkind(s); g_fsel=s; { extern int py_open_slot(int); if(py_open_slot(s))return; } if(fext_eq(FS[s].name,"pptx")){ pptx_load(s); open_win(W_PPTX); } else if(video_is_media_name(FS[s].name)) video_open_slot(s); else if(kd==1)g_imgview=s; else if(kd==2)browser_show_html(fsd(s),FS[s].len,FS[s].name); else editor_open(s); }
static int desk_click(int px,int py){ int it=desk_hit(px,py); if(it>=0){ desk_open(it); return 1; } return 0; }
/* ---- app launcher ---- */
static const int LAUNCH[23]={W_FILES,W_TERM,W_EDITOR,W_BROWSER,W_VIDEO,W_CALC,W_CLOCK,W_CALENDAR,W_SYSMON,W_SNAKE,W_FLAPPY,W_PAINT,W_DOOM,W_NEOFETCH,W_LOGS,W_TFIND,W_SETTINGS,W_ABOUT,W_CAMERA,W_BENCH,W_RECORDER,W_WRITER,W_QUAKE};
char g_lq[40]=""; int g_lqn=0;           /* launcher search query */
/* result kinds: 0=app(open_win), 1=settings page(open settings@cat), 2=file(open in editor/viewer) */
static int g_lres_kind[24], g_lres_arg[24], g_lres_n=0;
static const char* SET_PAGES[5]={"Settings: System","Settings: Personalization","Settings: Network","Settings: Display","Settings: About"};
static int lc_ci(char a){ if(a>='A'&&a<='Z')a+=32; return a; }
static int lc_has(const char* hay,const char* needle){ if(!needle[0])return 1;
    for(int i=0;hay[i];i++){ int j=0; while(needle[j]&&hay[i+j]&&lc_ci(hay[i+j])==lc_ci(needle[j]))j++; if(!needle[j])return 1; } return 0; }
void launcher_search(void){ g_lres_n=0;
    for(int k=0;k<23 && g_lres_n<24;k++) if(lc_has(APPNAME[LAUNCH[k]],g_lq)){ g_lres_kind[g_lres_n]=0; g_lres_arg[g_lres_n]=LAUNCH[k]; g_lres_n++; }
    if(pyide_installed()&&lc_has("Python IDE",g_lq)&&g_lres_n<24){ g_lres_kind[g_lres_n]=3; g_lres_arg[g_lres_n]=0; g_lres_n++; }
    if((lc_has("IDLE",g_lq)||lc_has("python",g_lq)||lc_has("tkinter",g_lq))&&g_lres_n<24){ g_lres_kind[g_lres_n]=4; g_lres_arg[g_lres_n]=0; g_lres_n++; }
    { int c=xapps_count(); for(int i=0;i<c&&g_lres_n<24;i++) if(lc_has(app_name(1100+i),g_lq)){ g_lres_kind[g_lres_n]=5; g_lres_arg[g_lres_n]=1100+i; g_lres_n++; } }
    if(g_lq[0]) for(int k=0;k<5 && g_lres_n<24;k++) if(lc_has(SET_PAGES[k],g_lq)){ g_lres_kind[g_lres_n]=1; g_lres_arg[g_lres_n]=k; g_lres_n++; }
    if(g_lq[0]) for(int f=0;f<FS_N && g_lres_n<24;f++) if(FS[f].used && lc_has(FS[f].name,g_lq)){ g_lres_kind[g_lres_n]=2; g_lres_arg[g_lres_n]=f; g_lres_n++; }
}
static void launcher_open_result(int idx){ if(idx<0||idx>=g_lres_n)return; g_launcher=0; int kind=g_lres_kind[idx],arg=g_lres_arg[idx];
    if(kind==0) open_win(arg);
    else if(kind==1){ g_setcat=arg; open_win(W_SETTINGS); }
    else if(kind==2){ extern int py_open_slot(int); if(py_open_slot(arg))return; ed_py=0; editor_open(arg); open_win(W_EDITOR); }
    else if(kind==3){ pyide_open(); }
    else if(kind==4){ app_launch(1001); }
    else if(kind==5){ app_launch(arg); }        /* open file in editor */
}
static const char* LPWR[4]={"Log Out","Sleep","Restart","Shut Down"};
static void launcher_geom(int*px,int*py,int*pw,int*ph);
static void launcher_pwr(int i,int* x,int* y,int* w,int* h){ int px,py,pw,ph; launcher_geom(&px,&py,&pw,&ph); int bw[4]={88,72,84,104}; int xx=px+pw-20; for(int k=3;k>=i;k--)xx-=bw[k]+8; *x=xx+8; *y=py+ph-46; *w=bw[i]; *h=30; }
static void launcher_power(int i){ if(i==0){ g_state=ST_LOGIN; login_pass[0]=0; login_pl=0; } else if(i==1){ power_sleep(); } else if(i==2){ power_reboot(); } else power_off(); }
#define LB_SIDE 64
#define LB_TOP 76
#define LB_ROW 46
#define LB_FOOT 56
#define LB_COLS 4
#define LB_CELLH 82
#define LB_HDR 32
static const char* LGRP_N[5]={"Productivity","Internet & Media","Games","System","Drivers & Hardware"};
#define APP_PYIDE 1000
static int LGRP[5][16]={{W_FILES,W_WRITER,W_EDITOR,W_CALC,W_CALENDAR,W_CLOCK,1001,-1,-1},{W_BROWSER,W_VIDEO,W_RECORDER,W_PAINT,-1},{W_SNAKE,W_FLAPPY,W_DOOM,W_QUAKE,-1},{W_TERM,W_SYSMON,W_LOGS,W_TFIND,W_XDISP,W_NEOFETCH,W_SETTINGS,-1},{W_GPU3D,W_CAMERA,W_RECORDER,W_BENCH,W_SYSMON,W_SETTINGS,-1}};
static void lgrp_refresh(void){ int n=7; if(pyide_installed())LGRP[0][n++]=APP_PYIDE; int c=xapps_count(); for(int i=0;i<c&&n<15;i++)LGRP[0][n++]=1100+i; LGRP[0][n]=-1; }
static int lgrid_h(void){ lgrp_refresh(); int h=0; for(int g=0;g<5;g++){ int n=0; while(n<16&&LGRP[g][n]>=0)n++; h+=LB_HDR+((n+LB_COLS-1)/LB_COLS)*LB_CELLH+8; } return h; }
static void launcher_geom(int*px,int*py,int*pw,int*ph){ *pw=476; *ph=LB_TOP+lgrid_h()+12; if(*ph>g_tby-24)*ph=g_tby-24; *px=12; *py=g_tby-*ph-12; }
static const int LSIDE[6]={W_FILES,W_EDITOR,W_PAINT,W_CLOCK,W_SYSMON,W_BROWSER};
static void lside_icon(int k,int cx,int cy,u32 c){
    if(k==0){ fill_round(cx-8,cy-6,16,11,2,15,c); fill_rect(cx-3,cy+6,6,2,c); }
    else if(k==1){ fill_round(cx-6,cy-8,12,16,2,15,c); for(int r=0;r<3;r++)fill_rect(cx-3,cy-4+r*4,6,1,rgb(40,32,70)); }
    else if(k==2){ fill_round(cx-8,cy-6,16,12,2,15,c); fill_circle(cx-3,cy-2,2,rgb(40,32,70)); }
    else if(k==3){ fill_circle(cx-3,cy+4,3,c); fill_rect(cx-1,cy-7,2,11,c); fill_rect(cx-1,cy-7,7,2,c); }
    else if(k==4){ fill_round(cx-7,cy-7,14,14,2,15,c); for(int r=0;r<3;r++)fill_rect(cx-7+r*5,cy-7,1,14,rgb(40,32,70)); }
    else { fill_circle(cx,cy,7,c); fill_rect(cx-1,cy-4,2,6,rgb(40,32,70)); fill_rect(cx-3,cy,6,2,rgb(40,32,70)); } }
static int lb_hit(int x,int y,int w,int h){ return mx>=x&&mx<x+w&&my>=y&&my<y+h; }
static const char* CM_CAT[5]={"Development","Games","Graphics","Utilities","Drivers & Hardware"};
static int CM_APPS[5][16]={{W_TERM,W_EDITOR,W_TFIND,W_LOGS,W_XDISP,1001,-1,-1,-1},{W_SNAKE,W_FLAPPY,W_DOOM,W_QUAKE,-1},{W_PAINT,W_VIDEO,-1},{W_FILES,W_WRITER,W_BROWSER,W_CALC,W_CALENDAR,W_CLOCK,W_SYSMON,W_NEOFETCH,W_SETTINGS,-1},{W_GPU3D,W_CAMERA,W_RECORDER,W_BENCH,W_SYSMON,-1}};
static const u32 CM_CC[5]={0xC8A040,0x8090A0,0x40A060,0x5070C0,0x2E9858};
#define CM_IH 24
static int g_csub=-1;
static void cm_geom(int*x,int*y,int*w,int*h){ *w=180; *h=6+CM_IH+8+5*CM_IH+8+CM_IH+6; *x=2; *y=g_tby-*h+1; }
static int cm_item_y(int it,int y0){ int y=y0+3; if(it==0)return y; y+=CM_IH+8; if(it<=5)return y+(it-1)*CM_IH; return y+5*CM_IH+8; }
static void cm_sub_geom(int c,int*x,int*y,int*w,int*h){ { int n=6; if(pyide_installed())CM_APPS[0][n++]=1000; int c=xapps_count(); for(int i=0;i<c&&n<15;i++)CM_APPS[0][n++]=1100+i; CM_APPS[0][n]=-1; } int mx0,my0,mw,mh; cm_geom(&mx0,&my0,&mw,&mh); int n=0; while(n<16&&CM_APPS[c][n]>=0)n++; *w=200; *h=n*CM_IH+6; *x=mx0+mw-3; *y=cm_item_y(1+c,my0)-3; if(*y+*h>g_tby)*y=g_tby-*h; }
static void classic_menu_draw(void){ int x,y,w,h; cm_geom(&x,&y,&w,&h);
    g_bevel_glass=1; bevel(x,y,w,h,0); g_bevel_glass=0; if(g_gfx&&g_gfx_fx){ gq_blend(g_rt,x+3,y+3,24,h-6,rgb(160,160,168),200); gq_blend(g_rt,x+27,y+3,w-30,h-6,rgb(255,255,255),222); } else { fill_rect(x+3,y+3,24,h-6,rgb(160,160,168)); fill_rect(x+27,y+3,w-30,h-6,rgb(255,255,255)); } fill_rect(x+27,y+3,1,h-6,rgb(128,128,128));
    static const char* LB[7]={"About NoovexOS","Development","Games","Graphics","Utilities","Drivers & Hardware","Exit..."};
    int hovit=-1;
    for(int it=0;it<7;it++){ int iy=cm_item_y(it,y); int hov=(mx>=x+3&&mx<x+w-3&&my>=iy&&my<iy+CM_IH); if(hov)hovit=it;
        int sel=hov||(it>=1&&it<=5&&g_csub==it-1);
        if(sel) fill_rect(x+28,iy,w-31,CM_IH,rgb(0,0,128));
        if(it==0) nv_logo_small(x+7,iy+4); else if(it<=5){ u32 c=CM_CC[it-1]; fill_rect(x+7,iy+4,16,16,c); fill_rect(x+7,iy+4,16,1,mix(c,rgb(255,255,255),110)); char t[2]={LB[it][0],0}; dstr(x+11,iy+5,t,rgb(255,255,255),1); }
        else { fill_rect(x+7,iy+5,16,12,rgb(0,0,128)); fill_rect(x+9,iy+7,12,8,rgb(80,140,220)); fill_rect(x+12,iy+17,6,2,rgb(80,80,80)); }
        dstr(x+36,iy+5,LB[it],sel?rgb(255,255,255):rgb(0,0,0),1);
        if(it>=1&&it<=5){ int ax=x+w-14, ay=iy+8; u32 ac=sel?rgb(255,255,255):rgb(0,0,0); for(int k=0;k<4;k++) fill_rect(ax+k,ay+k,1,8-2*k,ac); }
        if(it==0||it==5){ int sy=iy+CM_IH+3; fill_rect(x+29,sy,w-34,1,rgb(128,128,128)); fill_rect(x+29,sy+1,w-34,1,rgb(255,255,255)); } }
    if(hovit>=1&&hovit<=5)g_csub=hovit-1;
    else if(hovit>=0)g_csub=-1;
    else if(g_csub>=0){ int sx,sy,sw,sh; cm_sub_geom(g_csub,&sx,&sy,&sw,&sh); if(!(mx>=sx&&mx<sx+sw&&my>=sy&&my<sy+sh)&&!(mx>=x&&mx<x+w&&my>=y&&my<y+h))g_csub=-1; }
    if(g_csub>=0){ int sx,sy,sw,sh; cm_sub_geom(g_csub,&sx,&sy,&sw,&sh); bevel(sx,sy,sw,sh,0); fill_rect(sx+3,sy+3,sw-6,sh-6,rgb(255,255,255));
        for(int k=0;k<16&&CM_APPS[g_csub][k]>=0;k++){ int app=CM_APPS[g_csub][k], iy=sy+3+k*CM_IH; int hov=(mx>=sx+3&&mx<sx+sw-3&&my>=iy&&my<iy+CM_IH);
            if(hov)fill_rect(sx+3,iy,sw-6,CM_IH,rgb(0,0,128)); if(app>=1000){ if(!app_icon_draw(app,sx+8,iy+4,16)){ fill_rect(sx+8,iy+4,16,16,rgb(55,118,171)); fill_rect(sx+12,iy+8,8,8,rgb(255,214,70)); } dstr(sx+32,iy+5,app_name(app),hov?rgb(255,255,255):rgb(0,0,0),1); continue; } mini_icon(app,sx+8,iy+4); dstr(sx+32,iy+5,APPNAME[app],hov?rgb(255,255,255):rgb(0,0,0),1); } }
}
static int classic_menu_click(int px,int py){ int x,y,w,h; cm_geom(&x,&y,&w,&h);
    if(g_csub>=0){ int sx,sy,sw,sh; cm_sub_geom(g_csub,&sx,&sy,&sw,&sh); if(px>=sx&&px<sx+sw&&py>=sy&&py<sy+sh){ int k=(py-sy-3)/CM_IH; int n=0; while(n<16&&CM_APPS[g_csub][n]>=0)n++; if(k>=0&&k<n){ int app=CM_APPS[g_csub][k]; g_launcher=0; g_csub=-1; if(app==1000)pyide_open(); else if(app>1000){ app_launch(app); } else { if(app==W_EDITOR)ed_py=0; open_win(app); } } return 1; } }
    if(px>=x&&px<x+w&&py>=y&&py<y+h){ for(int it=0;it<7;it++){ int iy=cm_item_y(it,y); if(py>=iy&&py<iy+CM_IH){
            if(it==0){ g_launcher=0; open_win(W_ABOUT); } else if(it<=5){ g_csub=it-1; } else { g_launcher=0; launcher_power(0); } return 1; } } return 1; }
    g_launcher=0; g_csub=-1; return 1; }
static void launcher_draw(void){ if(g_dstyle==1){ classic_menu_draw(); return; } if(!g_gm_ready)gmetrics(); int px,py,pw,ph; launcher_geom(&px,&py,&pw,&ph);
    u32 tx=rgb(240,242,250), sub=rgb(178,182,206), ic=rgb(206,210,230), dark=rgb(44,34,78);
    shadow_soft(px,py,pw,ph,18); glass(px,py,pw,ph,18,rgb(46,34,84),252);
    glass(px,py,LB_SIDE,ph,18,rgb(22,16,44),200); glass(px+LB_SIDE-18,py,18,ph,0,rgb(22,16,44),200);
    fill_rect(px+LB_SIDE,py+14,1,ph-28,rgb(84,72,124));
    int cx=px+LB_SIDE/2;
    fill_circle(cx,py+36,19,mix(ACC,rgb(255,255,255),70)); fill_circle(cx,py+36,17,ACC); pdstr_ctr(cx,py+28,"N",rgb(255,255,255),2);
    for(int k=0;k<6;k++){ int cyk=py+96+k*52; if(lb_hit(px+10,cyk-20,LB_SIDE-20,40)) glass(px+10,cyk-20,LB_SIDE-20,40,10,rgb(255,255,255),36); lside_icon(k,cx,cyk,ic); }
    { int cyk=py+ph-96; if(lb_hit(px+10,cyk-20,LB_SIDE-20,40)) glass(px+10,cyk-20,LB_SIDE-20,40,10,rgb(255,255,255),36);
      fill_circle(cx,cyk,8,ic); fill_circle(cx,cyk,4,dark); for(int k=0;k<8;k++){ int dx[8]={0,6,9,6,0,-6,-9,-6},dy[8]={-9,-6,0,6,9,6,0,-6}; fill_circle(cx+dx[k],cyk+dy[k],2,ic); } }
    { int cyk=py+ph-44; if(lb_hit(px+10,cyk-20,LB_SIDE-20,40)) glass(px+10,cyk-20,LB_SIDE-20,40,10,rgb(255,255,255),36);
      fill_circle(cx,cyk,9,ic); fill_circle(cx,cyk,7,rgb(50,38,88)); fill_rect(cx-4,cyk-11,8,7,rgb(50,38,88)); fill_rect(cx-1,cyk-11,2,9,ic); }
    int lx=px+LB_SIDE+16, lw=pw-LB_SIDE-32;
    { int sby=py+18, sbh=38, sbw=lw-48; glass(lx,sby,sbw,sbh,10,rgb(255,255,255),26);
      int scx=g_lq[0]?lx+14:lx+sbw/2-34;
      fill_circle(scx+6,sby+18,6,ic); fill_circle(scx+6,sby+18,4,rgb(66,50,110)); fill_rect(scx+10,sby+22,5,2,ic);
      if(g_lq[0]) pdstr(scx+24,sby+12,g_lq,tx,1); else pdstr(scx+24,sby+12,"Search",sub,1);
      if(g_lq[0]&&((g_ticks/30)&1)){ int cxp=scx+24+g_lqn*8; fill_rect(cxp,sby+10,2,18,tx); }
      int ebx=lx+lw-38; if(lb_hit(ebx,sby,38,sbh)) glass(ebx,sby,38,sbh,10,rgb(255,255,255),30);
      int ex=ebx+12, ey=sby+12; fill_rect(ex+9,ey,6,2,ic); fill_rect(ex+13,ey,2,6,ic); fill_rect(ex,ey+12,6,2,ic); fill_rect(ex,ey+8,2,6,ic); for(int k=0;k<5;k++){ putpx(ex+12-k,ey+2+k,ic); putpx(ex+2+k,ey+12-k,ic); } }
    int ry=py+LB_TOP;
    if(!g_lq[0]){
        int cw=lw/LB_COLS, gy=ry; lgrp_refresh();
        for(int g=0;g<5;g++){ if(gy+LB_HDR>py+ph-8)break;
            pdstr(lx+2,gy+8,LGRP_N[g],sub,1); fill_rect(lx+pw_str(LGRP_N[g],1)+12,gy+15,lw-pw_str(LGRP_N[g],1)-14,1,rgb(84,72,124)); gy+=LB_HDR;
            int n=0; while(n<16&&LGRP[g][n]>=0)n++;
            for(int k=0;k<n;k++){ int cxk=lx+(k%LB_COLS)*cw, cyk=gy+(k/LB_COLS)*LB_CELLH; if(cyk+LB_CELLH>py+ph-4)break; int app=LGRP[g][k];
                if(lb_hit(cxk+2,cyk,cw-4,LB_CELLH-6)) glass(cxk+2,cyk,cw-4,LB_CELLH-6,12,rgb(255,255,255),34);
                if(app>1000){ if(!app_icon_draw(app,cxk+cw/2-24,cyk+2,48)) launch_icon(W_EDITOR,cxk+cw/2-24,cyk+2); } else launch_icon(app==APP_PYIDE?W_EDITOR:app,cxk+cw/2-24,cyk+2);
                if(app==APP_PYIDE){ fill_round(cxk+cw/2+6,cyk+30,22,16,4,15,rgb(55,118,171)); pdstr(cxk+cw/2+9,cyk+31,"py",rgb(255,214,70),1); }
                const char* nm=app>=1000?(app==1001?"IDLE":app_name(app)):APPNAME[app]; char t[20]; int tn=0; while(nm[tn]&&tn<19){ t[tn]=nm[tn]; tn++; } t[tn]=0; while(tn>3&&pw_str(t,1)>cw-8){ t[--tn]=0; t[tn-1]='.'; }
                pdstr_ctr(cxk+cw/2,cyk+56,t,tx,1); }
            gy+=((n+LB_COLS-1)/LB_COLS)*LB_CELLH+8; }
    } else {
        for(int k=0;k<g_lres_n && ry+LB_ROW<py+ph-12;k++){ int hov=lb_hit(lx-8,ry,lw+8,LB_ROW-4);
            if(hov) glass(lx-8,ry,lw+8,LB_ROW-4,10,ACC,200);
            int kind=g_lres_kind[k],arg=g_lres_arg[k];
            if(kind==0){ launch_icon(arg,lx-6,ry-3); pdstr(lx+50,ry+7,APPNAME[arg],tx,1); pdstr(lx+50,ry+24,"Application",sub,1); }
            else if(kind==1){ ic_gear(lx-6,ry-3); pdstr(lx+50,ry+7,SET_PAGES[arg]+10,tx,1); pdstr(lx+50,ry+24,"Settings",sub,1); }
            else if(kind==3){ launch_icon(W_EDITOR,lx-6,ry-3); pdstr(lx+50,ry+7,"Python IDE",tx,1); pdstr(lx+50,ry+24,"Application (python3-ide)",sub,1); }
            else if(kind==4){ if(!app_icon_draw(1001,lx-6,ry-3,40)) launch_icon(W_EDITOR,lx-6,ry-3); pdstr(lx+50,ry+7,"IDLE (Python 3.13)",tx,1); pdstr(lx+50,ry+24,"Application (X11, Tkinter)",sub,1); }
            else if(kind==5){ if(!app_icon_draw(arg,lx-6,ry-3,40)) launch_icon(W_EDITOR,lx-6,ry-3); pdstr(lx+50,ry+7,app_name(arg),tx,1); pdstr(lx+50,ry+24,"Application (installed package)",sub,1); }
            else { ic_edit(lx-6,ry-3); pdstr(lx+50,ry+7,FS[arg].name,tx,1); pdstr(lx+50,ry+24,"File",sub,1); }
            ry+=LB_ROW; }
        if(g_lres_n==0) pdstr(lx,py+LB_TOP+12,"No results",sub,1);
    }
}
int launcher_click(int px,int py){ if(g_dstyle==1)return classic_menu_click(px,py); int lx0,ly,lw0,lh; launcher_geom(&lx0,&ly,&lw0,&lh);
    if(px<lx0||px>=lx0+lw0||py<ly||py>=ly+lh){ g_launcher=0; return 1; }
    if(px<lx0+LB_SIDE){ for(int k=0;k<6;k++){ int cyk=ly+96+k*52; if(py>=cyk-20&&py<cyk+20){ g_launcher=0; open_win(LSIDE[k]); return 1; } }
        { int cyk=ly+lh-96; if(py>=cyk-20&&py<cyk+20){ g_launcher=0; open_win(W_SETTINGS); return 1; } }
        { int cyk=ly+lh-44; if(py>=cyk-20&&py<cyk+20){ g_launcher=0; launcher_power(0); return 1; } }
        return 1; }
    int lx=lx0+LB_SIDE+16, lw=lw0-LB_SIDE-32, ry=ly+LB_TOP;
    if(g_lq[0]){ for(int k=0;k<g_lres_n;k++){ if(py>=ry&&py<ry+LB_ROW-4&&px>=lx-8&&px<lx+lw){ launcher_open_result(k); return 1; } ry+=LB_ROW; } return 1; }
    { int cw=lw/LB_COLS, gy=ry;
      for(int g=0;g<5;g++){ gy+=LB_HDR; int n=0; while(n<16&&LGRP[g][n]>=0)n++;
        for(int k=0;k<n;k++){ int cxk=lx+(k%LB_COLS)*cw, cyk=gy+(k/LB_COLS)*LB_CELLH; if(cyk+LB_CELLH>ly+lh-4)break; if(px>=cxk+2&&px<cxk+cw-2&&py>=cyk&&py<cyk+LB_CELLH-6){ g_launcher=0; if(LGRP[g][k]==APP_PYIDE)pyide_open(); else if(LGRP[g][k]>1000){ app_launch(LGRP[g][k]); } else { if(LGRP[g][k]==W_EDITOR)ed_py=0; open_win(LGRP[g][k]); } return 1; } }
        gy+=((n+LB_COLS-1)/LB_COLS)*LB_CELLH+8; } }
    return 1;
}
/* keystrokes while the launcher is open (search box) */
int launcher_key(char c){ if(!g_launcher)return 0;
    if(c=='\n'){ if(g_lq[0]&&g_lres_n>0) launcher_open_result(0); else g_launcher=0; return 1; }
    if(c=='\b'){ if(g_lqn>0){ g_lq[--g_lqn]=0; launcher_search(); } return 1; }
    if(c=='\x1b'){ g_launcher=0; g_lq[0]=0; g_lqn=0; return 1; }
    if(c>=32 && c<127 && g_lqn<38){ g_lq[g_lqn++]=c; g_lq[g_lqn]=0; launcher_search(); return 1; }
    return 1;
}
/* ---- alt-tab switcher ---- */
static void alttab_draw(void){ if(g_altn<=0)return; int cw=96, pw=g_altn*cw+24; if(pw>(int)W-40)pw=(int)W-40; int ph=120; int px=((int)W-pw)/2, py=((int)H-ph)/2;
    shadow_soft(px,py,pw,ph,14); fill_round(px,py,pw,ph,14,15,rgb(30,34,44));
    for(int k=0;k<g_altn;k++){ int cx=px+12+k*cw, cy=py+14; int sel=(k==g_altsel); if(sel) fill_round(cx,cy,cw-8,ph-28,10,15,mix(rgb(30,34,44),rgb(255,255,255),40));
        launch_icon(g_altlist[k], cx+(cw-8)/2-24, cy+8); const char*nm=APPNAME[g_altlist[k]]; char t[14]; int tn=0; for(int q=0;nm[q]&&tn<12;q++)t[tn++]=nm[q]; t[tn]=0; dstr_ctr(cx+(cw-8)/2, cy+62, t, sel?rgb(255,255,255):rgb(180,186,200),1); } }
/* ---- toast ---- */
static void toast_draw(void){ { extern int osd_draw(void); if(osd_draw())return; } if(!toast_t)return; u64 age=(u64)(g_ticks-toast_t); if(age>=300){toast_t=0;return;}
    int a=255; if(age>240) a=255-(int)(age-240)*255/60; int tw=slen(toast_msg)*8+40; int tx=((int)W-tw)/2, ty=(int)H-140;
    if(g_gfx) gfx_glass(tx,ty,tw,34,10,rgb(28,32,44),a*232/255,g_gfx_fx?1:0); else for(int j=0;j<34;j++)for(int i=0;i<tw;i++){ if(cut(i,j,tw,34,10,15))continue; int dx=tx+i,dy=ty+j; if((u32)dx<W&&(u32)dy<H){ u32*p=&SCENE[(u64)dy*SW+dx]; *p=mix(*p,rgb(28,32,44),a); } }
    dstr_ctr(tx+tw/2, ty+9, toast_msg, mix(rgb(28,32,44),rgb(240,244,252),a), 1); }
/* ---- top-bar popovers ---- */
 
static void clockpop_draw(void){ int pw=210, ph=210, px=(int)W-pw-12, py=g_tby-ph-10; shadow_soft(px,py,pw,ph,12); fill_round(px,py,pw,ph,12,15,rgb(250,251,254));
    int hh,mm,ss,yr,mo,dy; read_clock(&hh,&mm,&ss,&yr,&mo,&dy);
    static const char*MON[13]={"","January","February","March","April","May","June","July","August","September","October","November","December"};
    { char hd[24],*p=hd; { const char*mn=MON[mo>=1&&mo<=12?mo:1]; while(*mn)*p++=*mn++; } *p++=' '; p=udec(p,(u32)yr); *p=0; dstr_ctr(px+pw/2,py+10,hd,INK,1); }
    static const char*WD[7]={"M","T","W","T","F","S","S"}; int cw=(pw-20)/7, gx=px+10, gy=py+32;
    for(int c=0;c<7;c++) dstr_ctr(gx+cw*c+cw/2,gy,WD[c],rgb(150,158,176),1);
    int col0=(cal_dow(yr,mo,1)+6)%7, dim=cal_dim(yr,mo), yy=gy+22;
    for(int day=1,cell=col0; day<=dim; day++,cell++){ int c=cell%7, r=cell/7, cxp=gx+cw*c+cw/2, cyp=yy+r*24; int today=(day==dy);
        if(today) fill_circle(cxp,cyp+4,10,ACC); char b[3],*p=b; p=udec(p,(u32)day); *p=0; dstr_ctr(cxp,cyp,b,today?rgb(255,255,255):rgb(52,60,78),1); } }
static void qs_draw(void){ if(g_dstyle==1){ cqs_draw(); return; } int pw=250, ph=150, px=g_qsx-pw+30, py=g_tby-ph-10; if(px<6)px=6; shadow_soft(px,py,pw,ph,12); fill_round(px,py,pw,ph,12,15,rgb(250,251,254));
    dstr(px+16,py+12,"Wallpaper",rgb(90,100,120),1); static const char*THN2[3]={"Porsche","Crane","Wireframe"};
    for(int k=0;k<3;k++){ int bx=px+16+k*74, by=py+32; fill_round(bx,by,64,30,6,15,THSW[k]); if(k==g_theme&&g_wallmode==0){ for(int t=0;t<64;t++){putpx(bx+t,by,ACC);putpx(bx+t,by+29,ACC);} } dstr_ctr(bx+32,by+38,THN2[k],rgb(90,100,120),1); }
    dstr(px+16,py+82,"Accent",rgb(90,100,120),1);
    for(int k=0;k<5;k++){ int bx=px+16+k*44, by=py+102; if(ACV[k]==ACC){ fill_circle(bx+14,by+12,15,rgb(255,255,255)); } fill_circle(bx+14,by+12,12,ACV[k]); } }
/* ---- rename modal ---- */
static void rename_draw(void){ int pw=360, ph=140, px=((int)W-pw)/2, py=(int)H/2-90;
    if(g_gfx) gq_blend(g_rt,0,0,(int)W,(int)H,rgb(10,12,20),110); else { (void)SCENE; for(u32 y=0;y<H;y++)for(u32 x=0;x<W;x++){ u32*p=&g_scene[(u64)y*SW+x]; *p=mix(*p,rgb(10,12,20),110); } }
    shadow_soft(px,py,pw,ph,14); fill_round(px,py,pw,ph,14,15,rgb(250,251,254));
    dstr(px+24,py+20,g_renmode==2?"New folder":g_renmode==3?"New file":g_renmode==1?"Rename folder":"Rename",INK,2);
    fill_round(px+24,py+56,pw-48,34,8,15,rgb(238,241,246)); int cx2=px+36; for(int k=0;k<g_renlen;k++){ dchar(cx2,py+66,g_renbuf[k],rgb(40,46,64),1); cx2+=8; } if((g_ticks/50)&1) fill_rect(cx2+1,py+65,2,15,ACC);
    dstr(px+24,py+ph-24,g_renmode>=2?"Enter = create    Esc = cancel":"Enter = rename    Esc = cancel",rgb(120,128,146),1); }
static void snap_preview_draw(void){ if(!g_snap)return; int wy=32, wh=(int)H-32-86, hw=((int)W-18)/2, hh=(wh-6)/2; int x=6,y=wy,w=hw,h=wh;
    if(g_snap==3){ x=6;y=wy;w=(int)W-12;h=wh; } else if(g_snap==1){ x=6;y=wy;w=hw;h=wh; } else if(g_snap==2){ x=(int)W-6-hw;y=wy;w=hw;h=wh; }
    else if(g_snap==4){ x=6;y=wy;w=hw;h=hh; } else if(g_snap==5){ x=(int)W-6-hw;y=wy;w=hw;h=hh; } else if(g_snap==6){ x=6;y=wy+hh+6;w=hw;h=hh; } else { x=(int)W-6-hw;y=wy+hh+6;w=hw;h=hh; }
    if(g_gfx){ gfx_round(x,y,w,h,12,15,rgb(120,170,250),90); fill_rect(x,y,w,1,rgb(150,190,255)); fill_rect(x,y+h-1,w,1,rgb(150,190,255)); fill_rect(x,y,1,h,rgb(150,190,255)); fill_rect(x+w-1,y,1,h,rgb(150,190,255)); return; }
    for(int j=0;j<h;j++)for(int i=0;i<w;i++){ if(cut(i,j,w,h,12,15))continue; int dx=x+i,dy=y+j; if((u32)dx<W&&(u32)dy<H){ u32*p=&SCENE[(u64)dy*SW+dx]; *p=mix(*p,rgb(120,170,250),90); } }
    for(int i=0;i<w;i++){ putpx(x+i,y,rgb(150,190,255)); putpx(x+i,y+h-1,rgb(150,190,255)); } for(int j=0;j<h;j++){ putpx(x,y+j,rgb(150,190,255)); putpx(x+w-1,y+j,rgb(150,190,255)); } }
#define ANIMW 768
#define ANIMH 600
static u32 WANIM[ANIMW*ANIMH];
int g_dmg_ok=0, g_dmx=0, g_dmy=0, g_dmw=0, g_dmh=0; u32 g_frames_skipped=0, g_frames_part=0;
static void quiet_on(void){ if(g_gfx)gfx_sync(); g_scene_quiet++; }
static void quiet_off(void){ if(g_gfx)gfx_sync(); if(g_scene_quiet>0)g_scene_quiet--; }
static void compose_build(void){
    if(g_gfx) gfx_frame_begin();
    if(g_gfx) gq_copy(gs_scene,0,0,(int)W,(int)H,gs_bg,0,0,256,0); else memcpy(SCENE,BG,(u64)W*H*4);
    if(g_gfx) gfx_wall_light();
    desk_draw();
    int order[NWIN],n=0; for(int i=0;i<NWIN;i++) if(WN[i].open&&!WN[i].min&&i!=anim_hide&&WN[i].ws==g_ws)order[n++]=i;
    for(int a=0;a<n;a++)for(int b=a+1;b<n;b++) if(WN[order[b]].z<WN[order[a]].z){int t=order[a];order[a]=order[b];order[b]=t;}
    if(drag>=0&&g_snap) snap_preview_draw();
    for(int a=0;a<n;a++){ if(g_gfx) gfx_window(order[a]); else draw_app(order[a]); }
    if(g_gfx) gfx_ghosts();
    marq_draw();
    draw_dock();
    { extern void xover_draw(void); xover_draw(); }
    if(g_launcher) launcher_draw(); if(g_qs) qs_draw(); if(g_clockpop) clockpop_draw();
    if(g_ctx) ctx_draw(); img_viewer_draw(); if(g_alttab) alttab_draw(); if(g_rename) rename_draw(); if(g_prop) prop_draw();
    if(g_wifipop) wifi_panel_draw(); if(g_notifpanel) notif_panel_draw(); toast_frame(); notif_toast_draw(); ws_overlay_draw(); toast_draw();
    night_tint(0,(int)H);
    if(g_gfx) g_dmg_ok=gfx_scene_end(&g_dmx,&g_dmy,&g_dmw,&g_dmh); else g_dmg_ok=0;
}
static u64 fps_t0=0; static int fps_cnt=0, fps_val=0;
u32 SCENE_NC_STATIC[MAXW*MAXH]; u32* SCENE_NC=SCENE_NC_STATIC;
int cxlast=-1, cylast=-1;

static void blit_region_fb(int x,int y,int w,int h);
void blit_region(int x,int y,int w,int h){
    g_scene_quiet++; void blit_region2(int,int,int,int); blit_region2(x,y,w,h); g_scene_quiet--; }
void blit_region2(int x,int y,int w,int h){
    if(x<0){w+=x;x=0;} if(y<0){h+=y;y=0;} if(x+w>(int)W)w=(int)W-x; if(y+h>(int)H)h=(int)H-y; if(w<=0||h<=0)return;
    { extern int g_igpu_flip; extern u8* g_igpu_back; extern void igpu_wait_flip(void); if(g_igpu_flip){ igpu_wait_flip(); if(g_igpu_flip){ u8* f=FBB; blit_region_fb(x,y,w,h); FBB=g_igpu_back; blit_region_fb(x,y,w,h); FBB=f; return; } } }
    blit_region_fb(x,y,w,h); }
static void blit_region_fb(int x,int y,int w,int h){
    if(BYPP==4&&g_fb_rgb){ for(int r=0;r<h;r++) copy_swap32((u32*)(FBB+(u64)(y+r)*PITCHB+(u64)x*4), SCENE+(u64)(y+r)*SW+x, w); }
    else if(BYPP==4){ for(int r=0;r<h;r++) sse_copy32((u32*)(FBB+(u64)(y+r)*PITCHB+(u64)x*4), SCENE+(u64)(y+r)*SW+x, w); }
    else { for(int r=0;r<h;r++) sse_pack24(FBB+(u64)(y+r)*PITCHB+(u64)x*3, SCENE+(u64)(y+r)*SW+x, w); }
}
int desk_popups_clear(void){
    if(drag>=0||g_resize>=0||g_fdrag||g_ddrag>=0||g_marq) return 0;
    if(g_launcher||g_qs||g_clockpop||g_ctx||g_submenu||g_imgview>=0||g_alttab||g_rename||g_prop||g_wifipop||g_notifpanel||g_toast_cur>=0||toast_t) return 0;
    return 1; }
int desk_win_exposed(int i){
    if(!WN[i].open||WN[i].min||WN[i].ws!=g_ws||i==anim_hide||drag>=0||g_resize>=0||g_fdrag||g_ddrag>=0||g_marq) return 0;
    if(g_launcher||g_qs||g_clockpop||g_ctx||g_submenu||g_imgview>=0||g_alttab||g_rename||g_prop||g_wifipop||g_notifpanel||g_toast_cur>=0||toast_t) return 0;
    for(int k=0;k<NWIN;k++) if(k!=i&&WN[k].open&&!WN[k].min&&WN[k].ws==g_ws&&WN[k].z>WN[i].z) return 0;
    return 1;
}
void present_rect(int x,int y,int w,int h){
    if(x<0){w+=x;x=0;} if(y<0){h+=y;y=0;} if(x+w>(int)W)w=(int)W-x; if(g_tby>0&&y+h>g_tby)h=g_tby-y; if(y+h>(int)H)h=(int)H-y; if(w<=0||h<=0)return;
    if(g_nightlight) for(int r=0;r<h;r++){ u32* p=&SCENE[(u64)(y+r)*SW+x]; for(int k=0;k<w;k++){ u32 c=p[k]; u32 g=(c>>8)&255, b=c&255; p[k]=(c&0xFF0000)|((g*15/16)<<8)|(b*5/8); } }
    quiet_on();
    for(int r=0;r<h;r++) sse_copy32(SCENE_NC+(u64)(y+r)*SW+x, SCENE+(u64)(y+r)*SW+x, w);
    blit_region(x,y,w,h);
    if(cxlast>=0 && cxlast+CURBB/2>x && cxlast-CURBB/2<x+w && cylast+CURBB/2>y && cylast-CURBB/2<y+h){ draw_cursor(); blit_region(mx-CURBB/2,my-CURBB/2,CURBB,CURBB); nc_restore(mx-CURBB/2,my-CURBB/2,CURBB,CURBB); cxlast=mx; cylast=my; }
    quiet_off();
}
void nc_restore(int x,int y,int w,int h){     /* copy cursor-free cache back into SCENE[region] */
    if(x<0){w+=x;x=0;} if(y<0){h+=y;y=0;} if(x+w>(int)W)w=(int)W-x; if(y+h>(int)H)h=(int)H-y; if(w<=0||h<=0)return;
    g_scene_quiet++; for(int r=0;r<h;r++){ u32* d=SCENE+(u64)(y+r)*SW+x; u32* s=SCENE_NC+(u64)(y+r)*SW+x; sse_copy32(d,s,w); } g_scene_quiet--;
}
char g_fc_msg[240]; static int fc_auto=0;
static char* fcs(char* p,const char* s){ while(*s)*p++=*s++; return p; }
static char* fcd(char* p,int v){ if(v<0){ *p++='-'; v=-v; } return udec(p,(u32)v); }
static char* fch(char* p,u32 v){ static const char X[]="0123456789ABCDEF"; for(int i=28;i>=0;i-=4)*p++=X[(v>>i)&15]; return p; }
static int fc_pass(u32* ref,int cpu,int* nd,int* bx0,int* by0,int* bx1,int* by1,int* fx,int* fy,u32* gv,u32* cv){
    extern int g_gq_mode; gfx_invalidate_all(); int m0=g_gfx, q0=g_gq_mode;
    if(cpu){ g_gfx=GFX_EMU; g_gq_mode=GQM_CPU; } else gq_log_begin(gs_scene);
    compose_build(); gfx_sync(); if(!cpu) gq_log_end();
    if(cpu){ g_gfx=m0; g_gq_mode=q0; }
    if(g_gfx==GFX_OFF) return -1;
    u32* sc=g_scene;
    if(!cpu){ for(u64 k=0;k<(u64)SW*H;k++)ref[k]=sc[k]; return 0; }
    int n=0; *bx0=1<<30; *by0=1<<30; *bx1=-1; *by1=-1;
    for(u32 y=0;y<H;y++){ u32* a=ref+(u64)y*SW; u32* b=sc+(u64)y*SW; for(u32 x=0;x<W;x++){ if(((a[x]^b[x])&0x00FFFFFFu)==0)continue;
        if(!n){ *fx=(int)x; *fy=(int)y; *gv=a[x]; *cv=b[x]; } n++; if((int)x<*bx0)*bx0=(int)x; if((int)y<*by0)*by0=(int)y; if((int)x>*bx1)*bx1=(int)x; if((int)y>*by1)*by1=(int)y; } }
    *nd=n; return 0; }
void frame_check(void){
    char* p=g_fc_msg;
    if(g_gfx==GFX_OFF){ p=fcs(p,"frame check needs GPU drawing"); *p=0; return; }
    u32* ref=(u32*)kmalloc((unsigned long)SW*H*4UL+64UL); if(!ref){ p=fcs(p,"frame check: not enough memory"); *p=0; return; }
    extern int g_gq_serial; extern int hr_precise(void); int ser0=g_gq_serial; int irq=hr_precise(); if(irq)cpu_irq_off();
    int nd=0,bx0=0,by0=0,bx1=0,by1=0,fx=0,fy=0; u32 gv=0,cv=0; char op[96]; op[0]=0; int ok=-1, nd0=0;
    for(int at=0;at<2;at++){
        if(at==1)g_gq_serial=1;
        if(fc_pass(ref,0,0,0,0,0,0,0,0,0,0)||fc_pass(ref,1,&nd,&bx0,&by0,&bx1,&by1,&fx,&fy,&gv,&cv)){ ok=-2; break; }
        if(!nd){ ok=at; break; }
        if(at==0){ nd0=nd; gq_log_find(fx,fy,op,sizeof(op));
            p=fcs(p,"frame check: "); p=udec(p,(u32)nd); p=fcs(p," pixels differ in "); p=fcd(p,bx0); *p++=','; p=fcd(p,by0); *p++='-'; p=fcd(p,bx1+1); *p++=','; p=fcd(p,by1+1);
            p=fcs(p,"; first "); p=fcd(p,fx); *p++=','; p=fcd(p,fy); p=fcs(p," GPU "); p=fch(p,gv&0xFFFFFFu); p=fcs(p," CPU "); p=fch(p,cv&0xFFFFFFu); p=fcs(p,"; last drawn by "); p=fcs(p,op); } }
    if(irq)cpu_irq_on();
    if(ok==0){ g_gq_serial=ser0; p=g_fc_msg; p=fcs(p,"frame check: the GPU frame matches the CPU frame pixel for pixel"); }
    else if(ok==1){ p=fcs(p,"; with a wait between operations it matches, so that mode stays on"); }
    else if(ok==-2){ g_gq_serial=ser0; p=g_fc_msg; p=fcs(p,"frame check could not run: GPU drawing stopped"); }
    else { g_gq_serial=ser0; p=fcs(p,"; with waits it still differs ("); p=udec(p,(u32)nd); p=fcs(p," pixels)"); (void)nd0; }
    *p=0; kfree(ref); gfx_invalidate_all(); klog(g_fc_msg); }
int frame_check_cmd(void){ if(g_state!=ST_DESKTOP||g_gfx==GFX_OFF)return -1; frame_check(); return 0; }
void compose_present(void){
    if(!fc_auto&&g_gfx==GFX_GPU&&g_gfx_frames>=8){ fc_auto=1; frame_check(); }
    compose_build();
    fps_cnt++; if(g_ticks-fps_t0>=100){ fps_val=fps_cnt; fps_cnt=0; fps_t0=g_ticks; }
    if(g_fps_overlay){ char b[16],*p=b; p=udec(p,(u32)fps_val); p=scopy(p," FPS"); *p=0; int bw=(int)slen(b)*8+18;
        fill_round((int)W-bw-10,8,bw,24,6,15,rgb(20,22,30)); dstr((int)W-bw-1,13,b,rgb(120,240,140),1); g_dmg_ok=0; }
    int part=g_gfx&&g_dmg_ok;
    if(g_gfx){ if(!part) gq_copy(gs_nc,0,0,(int)W,(int)H,gs_scene,0,0,256,0); else if(g_dmw>0&&g_dmh>0) gq_copy(gs_nc,g_dmx,g_dmy,g_dmw,g_dmh,gs_scene,g_dmx,g_dmy,256,0); }
    else memcpy(SCENE_NC, SCENE, (u64)W*H*4);
    int skip=part&&g_dmw<=0&&cxlast==mx&&cylast==my;
    quiet_on();
    if(skip) g_frames_skipped++;
    else { draw_cursor();
        if(part){ extern void igpu_present_hint(int,int,int,int); int x0=mx-CURBB/2, y0=my-CURBB/2, x1=x0+CURBB, y1=y0+CURBB;
            if(cxlast>=0){ int a=cxlast-CURBB/2, b=cylast-CURBB/2; if(a<x0)x0=a; if(b<y0)y0=b; if(a+CURBB>x1)x1=a+CURBB; if(b+CURBB>y1)y1=b+CURBB; }
            if(g_dmw>0&&g_dmh>0){ if(g_dmx<x0)x0=g_dmx; if(g_dmy<y0)y0=g_dmy; if(g_dmx+g_dmw>x1)x1=g_dmx+g_dmw; if(g_dmy+g_dmh>y1)y1=g_dmy+g_dmh; }
            igpu_present_hint(x0,y0,x1-x0,y1-y0); g_frames_part++; }
        blit_all(); }
    if(g_gfx) gfx_frame_end();
    if(!skip) nc_restore(mx-CURBB/2,my-CURBB/2,CURBB,CURBB);
    cxlast=mx; cylast=my;
    quiet_off(); if(g_gfx) gfx_scene_settle();
}
/* fast path: only the cursor moved -> repaint just the old + new cursor rectangles */

/* cheap idle update: redraw only the top bar (clock/battery) instead of the whole screen */
void present_topbar(void){
    int ty=g_tby>0?g_tby:(int)H-64, th=(int)H-ty;
    if(g_gfx){ gfx_sync(); gq_ext_mark(gs_scene,0,ty,(int)W,th); } g_scene_quiet++;
    for(int y=ty;y<ty+th;y++){ u32* d=SCENE+(u64)y*SW; u32* s=BG+(u64)y*SW; sse_copy32(d,s,(int)W); }
    draw_topbar(); night_tint(ty,ty+th);
    for(int y=ty;y<ty+th;y++){ u32* d=SCENE_NC+(u64)y*SW; u32* s=SCENE+(u64)y*SW; sse_copy32(d,s,(int)W); }
    blit_region(0,ty,(int)W,th);
    if(cxlast>=0 && cylast > ty-CURBB) present_cursor();                                             /* re-overlay cursor if it's up there */
    if(g_gfx)gfx_sync(); g_scene_quiet--;
}
void present_cursor(void){
    quiet_on();
    if(cxlast>=0) blit_region(cxlast-CURBB/2, cylast-CURBB/2, CURBB, CURBB);   /* erase old (SCENE is cursor-free) */
    draw_cursor();                                                             /* draw into SCENE at mx,my */
    blit_region(mx-CURBB/2, my-CURBB/2, CURBB, CURBB);                         /* show new */
    nc_restore(mx-CURBB/2, my-CURBB/2, CURBB, CURBB);                          /* restore SCENE to cursor-free */
    cxlast=mx; cylast=my;
    quiet_off();
}
static u64 g_pump_last=0; static int g_pump_hooked=0;
volatile int g_busy_esc=0;
static int pump_building(void){ extern int g_gfx, g_rt, gs_scene; return g_gfx&&g_rt!=gs_scene; }
void busy_pump(void){
    static int in=0; static u64 last=0, lastp=~0ULL; if(in||g_state!=ST_DESKTOP||g_ticks==lastp)return; lastp=g_ticks; in=1;
    if(xhid_n) xhci_mouse_poll();
    int dx,dy; cpu_irq_off(); dx=mouse_dx; dy=mouse_dy; mouse_dx=0; mouse_dy=0; cpu_irq_on();
    if((dx||dy)&&g_mouse_accel){ int m=(g_mouse_accel==1)?3:5; if(dx>6||dx<-6)dx=dx*m/2; if(dy>6||dy<-6)dy=dy*m/2; }
    if(dx||dy){ mx+=dx; my-=dy; if(mx<0)mx=0; if(my<0)my=0; if(mx>=(int)W)mx=W-1; if(my>=(int)H)my=H-1; g_cursor_moved=1; }
    if(kdown[0x01])g_busy_esc=1;
    if((cxlast!=mx||cylast!=my)&&g_ticks!=last&&!pump_building()){ last=g_ticks; present_cursor(); }
    in=0; }
void ui_pump(void){
    if(!g_pump_hooked){ g_wpa_pump=ui_pump; g_pump_hooked=1; }
    if(xhid_n) xhci_mouse_poll();
    if(g_ticks-g_pump_last>=2&&!pump_building()){ g_pump_last=g_ticks; present_cursor(); }
}

void win_anim(int i,int opening){ (void)i; (void)opening; return;   /* animations off: windows open/close instantly (smoother) */ }
static void win_anim_unused(int i,int opening){
    if(g_safemode) return;
    int wx=WN[i].x,wy=WN[i].y,ww=WN[i].w,wh=WN[i].h;
    if(ww<=0||wh<=0||ww>ANIMW||wh>ANIMH||wx<0||wy<0||wx+ww>(int)W||wy+wh>(int)H) return;   /* skip if off-screen/too big */
    anim_hide=-1; compose_build();
    for(int r=0;r<wh;r++){ u32* s=&SCENE[(u64)(wy+r)*SW+wx]; u32* d=&WANIM[r*ANIMW]; for(int cc=0;cc<ww;cc++)d[cc]=s[cc]; }
    int frames=7;
    for(int f=0;f<=frames;f++){ int t=opening?f:(frames-f); int sc=800+200*t/frames; int al=40+216*t/frames;
        anim_hide=i; compose_build();
        int sw=ww*sc/1000, sh=wh*sc/1000; if(sw<1)sw=1; if(sh<1)sh=1; int ox=wx+(ww-sw)/2, oy=wy+(wh-sh)/2;
        for(int r=0;r<sh;r++){ int dy=oy+r; if(dy<0||dy>=(int)H)continue; int srcr=r*wh/sh; u32* srow=&WANIM[srcr*ANIMW];
            for(int cc=0;cc<sw;cc++){ int dx=ox+cc; if(dx<0||dx>=(int)W)continue; int srcc=cc*ww/sw; u32* dd=&SCENE[(u64)dy*SW+dx]; *dd=mix(*dd,srow[srcc],al); } }
        draw_cursor(); blit_all(); spin(1300000);
    }
    anim_hide=-1;
}


void snd_apply_volume(int v){ if(g_snd_state)g_snd.volume(v); }
