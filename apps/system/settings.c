#include "include/nvx/drivers/input/touchpad/i2c_hid.h"
#include "include/nvx/base.h"
#include "include/drivers.h"
#include "include/nvx/core/sound.h"
#include "include/nvx/arch/x86_64/cpu/percpu.h"
#include "include/nvx/lib/base/fmt.h"
#include "include/nvx/arch/x86_64/cpu/simd.h"
#include "include/nvx/drivers/video/fb/framebuffer.h"
#include "include/nvx/lib/js/js.h"
#include "include/nvx/arch/x86_64/cpu/cpuinfo.h"
#include "include/nvx/drivers/bus/pci.h"
#include "include/nvx/drivers/net/ethernet/nic_names.h"
#include "include/nvx/drivers/usb/xhci.h"
#include "include/nvx/drivers/net/ethernet/nic.h"
#include "include/nvx/net/http/http.h"
#include "include/nvx/drivers/input/ps2/ps2.h"
#include "include/nvx/graphics/theme/theme.h"
#include "include/nvx/core/panic.h"
#include "include/nvx/graphics/ui/wm.h"
#include "include/nvx/fs/vfs/ramfs.h"
#include "include/nvx/shell/terminal.h"
#include "include/nvx/drivers/storage/nvme.h"
#include "include/nvx/fs/nvx/nvxfs.h"
#include "include/nvx/core/buildsig.h"
#include "include/nvx/drivers/net/wifi/rtw8821ce.h"
#include "include/nvx/core/power.h"
#include "include/nvx/drivers/acpi/acpi_dev.h"
#include "include/nvx/fs/nvx/nvx4.h"
#include "include/nvx/shell/commands.h"
#include "include/nvx/apps/system/settings.h"
#include "include/nvx/apps/system/files_classic.h"
#include "include/nvx/graphics/ui/desktop.h"
#include "include/nvx/core/init/installer.h"
#include "include/nvx/core/init/main.h"
/* =========================================================================
   Settings (expanded)
   ========================================================================= */
const char *THN[NWALL]={"Porsche","Crane","Wireframe","Peaks","Earth","Yosemite"};
u32 wall_thumb[NWALL][64*40]; int wall_thumb_ready=0; 
const u32 THSW[NWALL]={rgb(40,40,48),rgb(84,90,150),rgb(70,80,160),rgb(240,150,120),rgb(10,14,30),rgb(60,110,60)};
static const char *ACN[5]={"Blue","Violet","Teal","Rose","Amber"};
const u32 ACV[5]={rgb(60,120,224),rgb(140,90,220),rgb(30,170,160),rgb(220,80,120),rgb(230,150,40)};
static const char *CUN[5]={"Arrow","Large","White","Cross","Dot"};
static int st_wifiscan_x,st_wifiscan_y,st_wifijoin_x,st_wifijoin_y; static int st_wifi_ln=0;

int g_refresh_hz=60, g_panel_oled=0, g_mouse_accel=0, g_fps_overlay=0;   /* Display settings */
int g_setcat=1; int g_devtab=0;

/* sync accent + wallpaper to/from the signed-in Noovex account */
void nvx_settings_save(void){
    if(!g_nvx_signedin) return;
    char body[96],*p=body; p=scopy(p,"{\"accent\":"); *p++=(char)('0'+g_accent); p=scopy(p,",\"theme\":"); *p++=(char)('0'+g_theme); *p++='}'; *p=0;
    char hdr[380],*hp=hdr; hp=scopy(hp,"Content-Type: application/json\r\nAuthorization: Bearer ");hp=scopy(hp,g_nvx_token);hp=scopy(hp,"\r\n");*hp=0;
    https_request(NVX_HOST,"/api/public/settings","POST",hdr,body,(int)(p-body),http_buf,(int)sizeof(http_buf));
}
void nvx_settings_load(void){
    if(!g_nvx_signedin) return;
    char hdr[360],*hp=hdr; hp=scopy(hp,"Authorization: Bearer ");hp=scopy(hp,g_nvx_token);hp=scopy(hp,"\r\n");*hp=0;
    int n=https_request(NVX_HOST,"/api/public/settings","GET",hdr,0,0,http_buf,(int)sizeof(http_buf));
    if(n<=0) return; int he=0; for(int i=0;i+3<n;i++)if(http_buf[i]=='\r'&&http_buf[i+1]=='\n'&&http_buf[i+2]=='\r'&&http_buf[i+3]=='\n'){he=i+4;break;}
    int a=nvx_json_int(http_buf+he,n-he,"accent",-1), t=nvx_json_int(http_buf+he,n-he,"theme",-1);
    if(a>=0&&a<5){ g_accent=a; ACC=ACV[a]; ACC2=mix(ACC,rgb(0,0,0),60); }
    if(t>=0&&t<3) g_theme=t;
    build_bg();
}
int g_wifipop=0, g_wifix=0; u32 g_wifi_mmio=0;
 
int g_wifi_sel=-1;              /* selected AP index for password entry */
char g_wifi_pw[64]; int g_wifi_pwlen=0; int g_wifi_entry=0;  /* entry mode active */
char g_wifi_conn[33]={0};      /* SSID we last connected to (for status) */
int g_wifi_scanning=0; char g_wifi_status[48]={'r','e','a','d','y',0};

static const char *SC_LBL[9]={"System","Personalization","Network","Display","Battery","Storage","Terminal","About","Devices"};
static const char *SC_SUB[9]={"Clock, power and build integrity","Wallpaper, accent colour and pointer","Ethernet and Wi-Fi","Refresh rate, panel and pointer speed","Charge level and power source","Disks, data partition and install","Theme, font and cursor for nsh","Version and hardware","Mice, keyboards, touchpad, printer, cameras and laptop keys"};
static int st_hx[64],st_hy[64],st_hw[64],st_hh[64],st_hid[64],st_hn=0, st_x0,st_w0;
static int st_blx, st_blw=1, st_mlx, st_mlw=1;
static void st_hit(int x,int y,int w,int h,int id){ if(st_hn<64){ st_hx[st_hn]=x; st_hy[st_hn]=y; st_hw[st_hn]=w; st_hh[st_hn]=h; st_hid[st_hn]=id; st_hn++; } }
static void st_icon(int x,int y,int k,int on){
    static const u32 IC[9]={rgb(120,128,146),rgb(226,90,150),rgb(60,130,230),rgb(40,170,170),rgb(70,180,100),rgb(236,150,50),rgb(52,58,76),rgb(100,120,230),rgb(200,90,60)};
    u32 c=IC[k]; fill_round(x,y,26,26,7,15,c); u32 w=rgb(255,255,255);
    switch(k){
    case 0: fill_circle(x+13,y+13,7,w); fill_circle(x+13,y+13,3,c); for(int a=0;a<4;a++){ int dx=(a==0)?0:(a==1)?8:(a==2)?0:-8, dy=(a==0)?-8:(a==1)?0:(a==2)?8:0; fill_rect(x+12+dx,y+12+dy,3,3,w); } break;
    case 1: fill_circle(x+13,y+13,8,w); fill_circle(x+10,y+10,2,c); fill_circle(x+16,y+10,2,rgb(250,200,80)); fill_circle(x+10,y+16,2,rgb(80,180,240)); break;
    case 2: for(int rr=3;rr>=1;rr--){ fill_circle(x+13,y+18,rr*4-1,w); fill_circle(x+13,y+18,rr*4-3,c); } fill_rect(x+1,y+19,24,6,c); fill_circle(x+13,y+18,2,w); break;
    case 3: fill_round(x+5,y+6,16,11,2,15,w); fill_rect(x+7,y+8,12,7,c); fill_rect(x+11,y+18,4,2,w); fill_rect(x+8,y+20,10,2,w); break;
    case 4: fill_round(x+5,y+8,15,10,2,15,w); fill_rect(x+20,y+11,2,4,w); fill_rect(x+7,y+10,8,6,c); break;
    case 5: fill_round(x+5,y+6,16,14,3,15,w); fill_rect(x+7,y+14,12,1,c); fill_circle(x+17,y+17,1,c); break;
    case 6: fill_round(x+4,y+6,18,14,3,15,w); dstr(x+6,y+6,">",c,1); fill_rect(x+13,y+16,6,2,c); break;
    case 7: fill_circle(x+13,y+13,8,w); fill_rect(x+12,y+12,3,6,c); fill_rect(x+12,y+8,3,2,c); break;
    case 8: fill_round(x+8,y+4,10,17,5,15,w); fill_rect(x+12,y+5,2,6,c); break; }
    (void)on; }
static void st_toggle(int x,int y,int on,int id){ int hov=in_rect(mx,my,x-4,y-4,52,32); u32 tr=on?(hov?ACC2:ACC):(hov?rgb(196,201,214):rgb(214,218,228)); x-=4; y-=1; fill_round(x,y,48,26,13,15,tr); int kx=on?x+35:x+13; fill_circle(kx,y+14,10,mix(tr,rgb(0,0,0),40)); fill_circle(kx,y+13,10,rgb(255,255,255)); st_hit(x,y-3,56,32,id); }
static int st_segw(const char** l,int n){ if(!g_gm_ready)gmetrics(); int w=0; for(int k=0;k<n;k++)w+=pw_str(l[k],1)+32; return w+6; }
static void st_seg(int x,int y,const char** l,int n,int sel,int idb){ int w=st_segw(l,n); y-=1; fill_round(x,y,w,32,10,15,rgb(234,236,242)); int cx=x+3;
    for(int k=0;k<n;k++){ int bw=pw_str(l[k],1)+32; if(k==sel){ fill_round(cx,y+3,bw,26,8,15,rgb(214,218,228)); fill_round(cx,y+3,bw,25,8,15,rgb(255,255,255)); } else if(in_rect(mx,my,cx,y+3,bw,26))fill_round(cx,y+3,bw,26,8,15,rgb(244,246,250));
        pdstr_ctr(cx+bw/2,y+9,l[k],k==sel?INK:rgb(100,108,126),1); st_hit(cx,y+3,bw,26,idb+k); cx+=bw; } }
static void st_btn(int x,int y,int w,const char* t,int id,int kind){ int hov=in_rect(mx,my,x,y,w,32);
    u32 bg=kind==1?(hov?ACC2:ACC):kind==2?(hov?rgb(252,232,232):rgb(255,242,242)):(hov?rgb(228,232,240):rgb(238,241,246)); u32 fg=kind==1?rgb(255,255,255):kind==2?rgb(200,60,60):INK;
    if(!g_gm_ready)gmetrics(); fill_round(x,y,w,34,10,15,bg); pdstr_ctr(x+w/2,y+10,t,fg,1); st_hit(x,y,w,34,id); }
static int st_card(int y,int h,const char* title){ if(!g_gm_ready)gmetrics(); if(title){ pdstr(st_x0+6,y,title,rgb(110,118,138),1); y+=26; }
    fill_round(st_x0,y+1,st_w0,h,14,15,rgb(232,234,240)); fill_round(st_x0,y,st_w0,h,14,15,rgb(228,231,238)); fill_round(st_x0+1,y+1,st_w0-2,h-2,13,15,rgb(255,255,255)); return y; }
static void st_rowtext(int y,const char* t,const char* d){ if(!g_gm_ready)gmetrics(); pdstr(st_x0+24,d?y+15:y+25,t,INK,1); if(d)pdstr(st_x0+24,y+35,d,rgb(128,136,154),1); }
static void st_sep(int y){ fill_rect(st_x0+24,y,st_w0-48,1,rgb(238,240,245)); }
static char* st_mb(char* p,u64 bytes){ u64 mb=bytes/1048576ULL; if(mb>=1024){ p=udec(p,(u32)(mb/1024)); *p++='.'; p=udec(p,(u32)((mb%1024)*10/1024)); return scopy(p," GB"); } p=udec(p,(u32)mb); return scopy(p," MB"); }
static int st_part=0, st_notabs=0;
enum { P_DISPLAY=20, P_GRAPHICS, P_GFXADV, P_SOUND, P_POWER, P_STORAGE, P_ABOUT, P_BACKGROUND=30, P_COLOURS, P_TERMINAL, P_WALLFX, P_WIFI=40, P_NETINFO, P_DEV0=50, P_MOUSE=57, P_APPS=60, P_ACCOUNT=70, P_TIME=80, P_LANG, P_SECURITY=90, P_RECOVERY=100 };
int g_stpg=0;
static const char* CATN[9]={"System","Personalization","Network & internet","Bluetooth & devices","Apps","Accounts","Time & language","Privacy & security","Update & recovery"};
static const char* CATD[9]={"Display, sound, power, storage","Background, colours, terminal","Wi-Fi, Ethernet, addresses","Input, USB, printers, laptop","Installed apps","Your account","Clock and region","Build integrity","Restart, recovery, install"};
static const int CATI[9]={0,1,2,8,5,7,6,3,4};
typedef struct { int pg, icon; const char* t; const char* d; } stile;
static const stile T_SYS[6]={{P_DISPLAY,3,"Display","Refresh rate, brightness, panel, graphics"},{P_SOUND,6,"Sound","Volume and output device"},{P_POWER,4,"Power & battery","Charge level, battery health"},{P_STORAGE,5,"Storage","Disks, data partition"},{P_GRAPHICS,3,"Graphics","GPU drawing, effects, wallpaper effects"},{P_ABOUT,7,"About","Device specifications, version"}};
static const stile T_PER[4]={{P_BACKGROUND,1,"Background","Wallpaper picture"},{P_COLOURS,1,"Colours","Accent colour and pointer"},{P_WALLFX,1,"Wallpaper effects","Smooth colours, glow, light"},{P_TERMINAL,6,"Terminal","Theme, font, cursor, transparency"}};
static const stile T_NET[2]={{P_WIFI,2,"Wi-Fi & Ethernet","Connect to a network"},{P_NETINFO,2,"Network details","Addresses, adapters, gateway"}};
static const stile T_DEV[8]={{P_DEV0+0,8,"Input","Keyboard, touchpad, mice"},{P_MOUSE,8,"Mouse & pointer","Pointer speed and style"},{P_DEV0+1,8,"USB","Controllers and devices"},{P_DEV0+2,5,"Storage devices","Disks and controllers"},{P_DEV0+3,2,"Network adapters","Ethernet and Wi-Fi cards"},{P_DEV0+4,6,"Media","Graphics, display, sound, camera"},{P_DEV0+5,0,"Printers","Network printing"},{P_DEV0+6,4,"Laptop","Keyboard light, charge limit"}};
static const stile T_APP[1]={{P_APPS,5,"Installed apps","Open or find apps"}};
static const stile T_ACC[1]={{P_ACCOUNT,7,"Your info","Name and account type"}};
static const stile T_TIM[2]={{P_TIME,0,"Date & time","Clock, seconds"},{P_LANG,6,"Language & region","Display language, keyboard"}};
static const stile T_PRV[1]={{P_SECURITY,7,"Device security","Build integrity check"}};
static const stile T_UPD[2]={{P_RECOVERY,4,"Recovery","Restart, crash test"},{P_ABOUT,7,"Version","NoovexOS version and build"}};
static const stile* cat_tiles(int c,int* n){ switch(c){ case 0:*n=6;return T_SYS; case 1:*n=4;return T_PER; case 2:*n=2;return T_NET; case 3:*n=8;return T_DEV; case 4:*n=1;return T_APP; case 5:*n=1;return T_ACC; case 6:*n=2;return T_TIM; case 7:*n=1;return T_PRV; default:*n=2;return T_UPD; } }
static int pg_parent(int p){ if(p<9)return -1; if(p==P_GFXADV)return P_GRAPHICS; if(p==P_GRAPHICS)return P_DISPLAY; if(p>=P_DISPLAY&&p<=P_ABOUT)return 0; if(p>=P_BACKGROUND&&p<=P_WALLFX)return 1; if(p>=P_WIFI&&p<=P_NETINFO)return 2; if(p>=P_DEV0&&p<=P_MOUSE)return 3; if(p==P_APPS)return 4; if(p==P_ACCOUNT)return 5; if(p==P_TIME||p==P_LANG)return 6; if(p==P_SECURITY)return 7; return 8; }
static int pg_cat(int p){ while(p>=9)p=pg_parent(p); return p<0?0:p; }
static const char* pg_title(int p){ if(p<9)return CATN[p]; switch(p){ case P_GFXADV:return "Advanced graphics"; case P_MOUSE:return "Mouse & pointer"; default:break; }
    for(int c=0;c<9;c++){ int n; const stile* t=cat_tiles(c,&n); for(int k=0;k<n;k++) if(t[k].pg==p)return t[k].t; } return "Settings"; }
static void st_tiles(int* cy,const stile* t,int n){ if(!g_gm_ready)gmetrics();
    for(int k=0;k<n;k++){ int y=*cy; int hov=in_rect(mx,my,st_x0,y,st_w0,58); fill_round(st_x0,y+1,st_w0,58,10,15,rgb(232,234,240)); fill_round(st_x0,y,st_w0,58,10,15,hov?rgb(250,251,253):rgb(255,255,255));
        st_icon(st_x0+18,y+16,t[k].icon,0); pdstr(st_x0+62,y+12,t[k].t,INK,1); pdstr(st_x0+62,y+32,t[k].d,rgb(128,136,154),1); pdstr(st_x0+st_w0-30,y+22,">",rgb(140,146,160),1); st_hit(st_x0,y,st_w0,58,1000+t[k].pg); *cy+=62; } }
static void st_infocard(int* cy,const char* title,const char** a,char b[][80],int n){ int y=st_card(*cy,n*38+8,title); int X1=st_x0+st_w0-24;
    for(int r=0;r<n;r++){ int ry=y+4+r*38; if(r)fill_rect(st_x0+20,ry,st_w0-40,1,rgb(238,240,245)); pdstr(st_x0+24,ry+11,a[r],INK,1); dstr(X1-slen(b[r])*8,ry+13,b[r],rgb(110,118,136),1); } *cy=y+n*38+8+20; }
static void old_leaf(int cat,int cy);
static void st_page(int p,int cy){
    int X1=st_x0+st_w0-24; char b[96],*q;
    if(p<9){ int n; const stile* t=cat_tiles(p,&n); st_tiles(&cy,t,n); return; }
    switch(p){
    case P_DISPLAY: { stile g={P_GRAPHICS,3,"Graphics","GPU drawing, effects, screen copies"}; st_tiles(&cy,&g,1); cy+=10; old_leaf(3,cy); } break;
    case P_GRAPHICS: { extern int g_gfx, g_gfx_fx, g_gfx_srec, g_wall_fx, g_wall_light;
        int y=st_card(cy,64*5,"GPU"); st_rowtext(y,"GPU drawing",g_gfx==1?"The Intel GPU draws the desktop":g_gfx==2?"Emulated on the CPU (test mode)":"The CPU draws the desktop"); st_toggle(X1-44,y+20,g_gfx!=0,300); st_sep(y+64);
        st_rowtext(y+64,"Visual effects","Shadows, rounded corners, glass, animations"); st_toggle(X1-44,y+84,g_gfx_fx,301); st_sep(y+128);
        st_rowtext(y+128,"Partial redraws","Only redraw the parts of the screen that changed"); st_toggle(X1-44,y+148,g_gfx_srec,302); st_sep(y+192);
        st_rowtext(y+192,"Wallpaper effects","Smooth colours, glow, depth, vignette, light"); st_toggle(X1-44,y+212,g_wall_fx,303); st_sep(y+256);
        st_rowtext(y+256,"Drifting light","A soft light moves across the wallpaper"); st_toggle(X1-44,y+276,g_wall_light,304); cy=y+320+24;
        stile a={P_GFXADV,3,"Advanced graphics","GPU details, screen copy, frame check"}; st_tiles(&cy,&a,1); } break;
    case P_GFXADV: { extern int igpu_info(const char**,u32*,u32*,u32*,u32*,u32*,u32*,u32*); extern int g_pres_eng; extern char g_fc_msg[]; extern u32 g_gfx_frame_us;
        const char* nm; u32 did=0,ss=0,eu=0,mhz=0,mxz=0,stl=0,tf=0; int hg=igpu_info(&nm,&did,&ss,&eu,&mhz,&mxz,&stl,&tf);
        const char* a[5]={"Graphics","Clock","Execution units","Graphics memory","Last frame"}; char v[5][80];
        q=v[0]; q=scopy(q,hg?nm:"No Intel graphics driver"); *q=0; q=v[1]; q=udec(q,mhz); q=scopy(q," of "); q=udec(q,mxz); q=scopy(q," MHz"); *q=0;
        q=v[2]; q=udec(q,ss*eu); *q=0; q=v[3]; q=udec(q,stl); q=scopy(q," MB"); *q=0; q=v[4]; q=udec(q,g_gfx_frame_us/1000); *q++='.'; q=udec(q,(g_gfx_frame_us/100)%10); q=scopy(q," ms"); *q=0;
        st_infocard(&cy,"GPU",a,v,5);
        static const char* PE[2]={"Blitter","Shader"}; int y=st_card(cy,128,"Screen"); st_rowtext(y,"Screen copy engine","Which GPU engine copies the picture to the screen"); { int w=st_segw(PE,2); st_seg(X1-w,y+17,PE,2,g_pres_eng,310); } st_sep(y+64);
        st_rowtext(y+64,"Tear-free","Double buffering, no tearing"); st_toggle(X1-44,y+84,(int)tf,312); cy=y+128+24;
        y=st_card(cy,64,"Frame check"); { char m[72]; int n=0, mxc=(st_w0-220)/8; if(mxc>70)mxc=70; while(g_fc_msg[n]&&n<mxc){ m[n]=g_fc_msg[n]; n++; } m[n]=0; dstr(st_x0+24,y+24,m[0]?m:"Compare one GPU frame with the CPU",rgb(110,118,136),1); } st_btn(X1-130,y+15,130,"Run check",313,1); } break;
    case P_SOUND: { int y=st_card(cy,128,"Output"); st_rowtext(y,"Output device",g_snd_state&&g_snd.name?g_snd.name:"No sound driver running");
        st_sep(y+64); q=b; q=udec(q,(u32)g_snd_vol); *q++='%'; *q=0; st_rowtext(y+64,"Volume",b);
        int bw=st_w0-320; if(bw<140)bw=140; int bx=X1-bw-10; int fw=g_snd_vol*bw/100; fill_round(bx,y+92,bw,8,4,15,rgb(226,229,236)); if(fw>0)fill_round(bx,y+92,fw,8,4,15,ACC); fill_circle(bx+fw,y+96,10,rgb(255,255,255)); fill_circle(bx+fw,y+96,4,ACC);
        st_blx=bx; st_blw=bw; st_hit(bx-12,y+78,bw+24,36,320); cy=y+128+24;
        y=st_card(cy,64,0); st_rowtext(y,"Mute","Silence all sound"); st_toggle(X1-44,y+20,g_snd_vol==0,321); cy=y+64+24;
        { extern const char* snd_rec_name(void); extern int snd_rec_avail(void); extern int g_mic_gain, g_cap_on, g_cap_peak;
          y=st_card(cy,192,"Input"); st_rowtext(y,"Microphone",snd_rec_name()); st_sep(y+64);
          q=b; q=udec(q,(u32)g_mic_gain); *q++='%'; *q=0; st_rowtext(y+64,"Microphone volume",b);
          int bw2=st_w0-320; if(bw2<140)bw2=140; int bx2=X1-bw2-10; int fw2=g_mic_gain*bw2/100; fill_round(bx2,y+92,bw2,8,4,15,rgb(226,229,236)); if(fw2>0)fill_round(bx2,y+92,fw2,8,4,15,ACC); fill_circle(bx2+fw2,y+96,10,rgb(255,255,255)); fill_circle(bx2+fw2,y+96,4,ACC);
          st_mlx=bx2; st_mlw=bw2; st_hit(bx2-12,y+78,bw2+24,36,322); st_sep(y+128);
          st_rowtext(y+128,"Test your microphone",g_cap_on?"Speak - the bar shows the level":snd_rec_avail()?"Open Voice Recorder to see the level":"No microphone input was found");
          if(g_cap_on){ int lv=g_cap_peak>1000?1000:g_cap_peak; fill_round(bx2,y+156,bw2,8,4,15,rgb(226,229,236)); int f3=lv*bw2/1000; if(f3>4)fill_round(bx2,y+156,f3,8,4,15,rgb(46,170,90)); } else st_btn(X1-150,y+144,150,"Voice Recorder",323,1); } } break;
    case P_POWER: old_leaf(4,cy); break; case P_STORAGE: old_leaf(5,cy); break; case P_ABOUT: old_leaf(7,cy); break;
    case P_BACKGROUND: st_part=1; old_leaf(1,cy); st_part=0; break; case P_COLOURS: st_part=2; old_leaf(1,cy); st_part=0; break; case P_TERMINAL: old_leaf(6,cy); break;
    case P_WALLFX: { extern int g_wall_fx, g_wall_light; extern u32 g_wall_fx_us; int y=st_card(cy,128,"Wallpaper");
        st_rowtext(y,"Wallpaper effects","Remove colour bands, add glow, depth, vignette and light"); st_toggle(X1-44,y+20,g_wall_fx,303); st_sep(y+64);
        st_rowtext(y+64,"Drifting light","Drawn by the GPU every frame - uses a little power"); st_toggle(X1-44,y+84,g_wall_light,304); cy=y+128+24;
        if(g_wall_fx_us){ y=st_card(cy,64,0); q=b; q=scopy(q,"Made in "); q=udec(q,g_wall_fx_us/1000); q=scopy(q," ms when the wallpaper was set"); *q=0; st_rowtext(y,"Last update",b); } } break;
    case P_WIFI: old_leaf(2,cy); break;
    case P_NETINFO: { const char* a[5]={"Wired adapter","Wired status","Wi-Fi","IPv4 address","MAC address"}; char v[5][80]; if(!pci_n)pci_scan(); int ek=nic_index();
        q=v[0]; q=scopy(q,ek>=0?nic_name(pci_ven[ek],pci_dev[ek]):"None"); *q=0; q=v[1]; q=scopy(q,net_ok?"Connected":"Not connected"); *q=0; q=v[2]; q=scopy(q,g_wifi_conn[0]?g_wifi_conn:"Not connected"); *q=0;
        q=v[3]; if(net_ok||g_wifi_conn[0]){ q=udec(q,my_ip>>24);*q++='.';q=udec(q,(my_ip>>16)&255);*q++='.';q=udec(q,(my_ip>>8)&255);*q++='.';q=udec(q,my_ip&255); } else q=scopy(q,"-"); *q=0;
        q=v[4]; for(int k=0;k<6;k++){ q=hx(q,my_mac[k],2); if(k<5)*q++=':'; } *q=0; st_infocard(&cy,"Addresses",a,v,5); } break;
    case P_MOUSE: { static const char* MA[3]={"Off","Low","High"}; int y=st_card(cy,128,"Pointer"); st_rowtext(y,"Pointer acceleration","Moves faster when you move the mouse quickly"); { int w=st_segw(MA,3); st_seg(X1-w,y+17,MA,3,g_mouse_accel,65); } st_sep(y+64);
        st_rowtext(y+64,"Pointer style",0); { int w=st_segw(CUN,5); st_seg(X1-w,y+81,CUN,5,g_cursor,30); } } break;
    case P_APPS: { int y=st_card(cy,0,0); (void)y; int cols=2, n=0; int bw=(st_w0-12)/cols;
        for(int a=0;a<NWIN;a++){ if(a==W_XDISP||a==W_TFIND)continue; int bx=st_x0+(n%cols)*(bw+12), by=cy+(n/cols)*52; if(by+46>WN[W_SETTINGS].y+WN[W_SETTINGS].h-8)break;
            int hov=in_rect(mx,my,bx,by,bw,46); fill_round(bx,by,bw,46,10,15,hov?rgb(250,251,253):rgb(255,255,255)); launch_icon(a,bx+6,by+2); pdstr(bx+56,by+15,APPNAME[a],INK,1); pdstr(bx+bw-60,by+15,WN[a].open?"Open":"Start",ACC,1); st_hit(bx,by,bw,46,2000+a); n++; } } break;
    case P_ACCOUNT: { int y=st_card(cy,110,0); fill_circle(st_x0+60,y+55,32,ACC); char ini=g_user[0]; if(ini>='a'&&ini<='z')ini-=32; char s2[2]={ini?ini:'U',0}; pdstr_ctr(st_x0+60,y+46,s2,rgb(255,255,255),2);
        pdstr(st_x0+112,y+32,g_user,INK,2); dstr(st_x0+112,y+66,g_uid==0?"Administrator":"Standard user",rgb(120,128,146),1); cy=y+110+24;
        y=st_card(cy,64,"Noovex account"); st_rowtext(y,g_nvx_signedin?"Signed in":"Not signed in",g_nvx_signedin?"Accent colour and wallpaper sync to your account":"Sign in during setup to sync settings"); } break;
    case P_TIME: { int hh,mm,ss2,yy,mo,dd; read_clock(&hh,&mm,&ss2,&yy,&mo,&dd); int y=st_card(cy,64,0); q=b; q=u2(q,hh);*q++=':';q=u2(q,mm);*q++=':';q=u2(q,ss2); q=scopy(q,"   "); q=udec(q,(u32)yy);*q++='-';q=u2(q,mo);*q++='-';q=u2(q,dd); *q=0; st_rowtext(y,"Current date and time",b); cy=y+64+24;
        y=st_card(cy,64,"Clock"); st_rowtext(y,"Show seconds","Adds seconds to the clock on the taskbar"); st_toggle(X1-44,y+20,g_showsec,1); } break;
    case P_LANG: { const char* a[3]={"Display language","Region format","Keyboard"}; char v[3][80]; scopy(v[0],"English")[0]=0; scopy(v[1],"24-hour clock, year-month-day")[0]=0; scopy(v[2],"Layout chosen during setup")[0]=0; st_infocard(&cy,0,a,v,3); } break;
    case P_SECURITY: { int y=st_card(cy,64,"Build"); { q=b; q=scopy(q,"NoovexOS 64 v"); q=scopy(q,(char*)g_buildsig.version); *q=0; st_rowtext(y,b,g_sig_ok?"Integrity check passed at boot":"Integrity check reported a mismatch at boot"); }
        fill_round(X1-110,y+20,110,24,12,15,g_sig_ok?rgb(226,246,233):rgb(253,236,226)); dstr_ctr(X1-55,y+25,g_sig_ok?"Verified":"Mismatch",g_sig_ok?rgb(40,140,80):rgb(200,110,40),1); } break;
    case P_RECOVERY: { int y=st_card(cy,192,"Recovery"); st_rowtext(y,"Restart","Restart the computer now"); st_btn(X1-110,y+16,110,"Restart",2,1); st_sep(y+64);
        st_rowtext(y+64,"Install NoovexOS","Put this system on a disk"); st_btn(X1-120,y+80,120,"Open Setup",95,1); st_sep(y+128);
        st_rowtext(y+128,"Crash test","Shows the kernel panic screen (for testing)"); st_btn(X1-110,y+144,110,"Crash",3,2); } break;
    default: if(p>=P_DEV0&&p<P_DEV0+7){ g_devtab=p-P_DEV0; st_notabs=1; old_leaf(8,cy); st_notabs=0; } break; } }
void settings_wheel(int sc){ (void)sc; }
void settings_page_name(const char* n){ static const char* NM[16]={"system","personalization","network","devices","apps","accounts","time","privacy","update","display","graphics","advanced","background","sound","mouse","installed"}; static const int PG[16]={0,1,2,3,4,5,6,7,8,P_DISPLAY,P_GRAPHICS,P_GFXADV,P_BACKGROUND,P_SOUND,P_MOUSE,P_APPS};
    for(int k=0;k<16;k++){ int j=0; while(NM[k][j]&&n[j]==NM[k][j])j++; if(!NM[k][j]&&(!n[j]||n[j]==' ')){ g_stpg=PG[k]; return; } } }
void settings_draw(int i){
    { static int seen=1; if(g_setcat!=seen){ static const int MP[9]={0,P_BACKGROUND,P_WIFI,P_DISPLAY,P_POWER,P_STORAGE,P_TERMINAL,P_ABOUT,3}; seen=g_setcat; if(g_setcat>=0&&g_setcat<9)g_stpg=MP[g_setcat]; } }
    win_frame(i,"Settings"); st_hn=0; if(g_stpg<0)g_stpg=0;
    int wx=WN[i].x, wy=WN[i].y+TITLEH, ww=WN[i].w, wh=WN[i].h-TITLEH; int sbw=ww>=900?256:ww>=760?230:190;
    fill_rect(wx+1,wy,sbw,wh-1,rgb(243,244,247)); fill_rect(wx+sbw+1,wy,ww-sbw-2,wh-1,rgb(247,248,251)); fill_rect(wx+sbw,wy,1,wh-1,rgb(228,230,236));
    { fill_round(wx+14,wy+16,sbw-28,58,14,15,rgb(255,255,255)); fill_circle(wx+44,wy+45,18,ACC); char ini=g_user[0]; if(ini>='a'&&ini<='z')ini-=32; char s[2]={ini?ini:'U',0}; if(!g_gm_ready)gmetrics(); pdstr_ctr(wx+44,wy+38,s,rgb(255,255,255),1);
      pdstr(wx+72,wy+28,g_user,INK,1); pdstr(wx+72,wy+47,g_uid==0?"Administrator":"Standard user",rgb(130,138,156),1); }
    int cur=pg_cat(g_stpg); int sstep=(wh-110)/9; if(sstep>50)sstep=50; if(sstep<40)sstep=40;
    for(int k=0;k<9;k++){ int iy=wy+92+k*sstep; int on=(k==cur), hov=in_rect(mx,my,wx+14,iy,sbw-28,40);
        if(on){ fill_round(wx+14,iy,sbw-28,40,11,15,rgb(255,255,255)); fill_rect(wx+16,iy+11,3,18,ACC); } else if(hov)fill_round(wx+14,iy,sbw-28,40,11,15,rgb(234,236,242));
        st_icon(wx+26,iy+7,CATI[k],on); pdstr(wx+64,iy+13,CATN[k],rgb(40,46,62),1); st_hit(wx+14,iy,sbw-28,40,100+k); }
    st_x0=wx+sbw+36; st_w0=ww-sbw-72; int cy=wy+26;
    { int chain[6], n=0; int p=g_stpg; while(p>=0&&n<6){ chain[n++]=p; p=pg_parent(p); } int x=st_x0;
      if(n>1){ int hov=in_rect(mx,my,x,cy+2,30,30); fill_round(x,cy+2,30,30,8,15,hov?rgb(232,235,242):rgb(247,248,251)); pdstr(x+10,cy+8,"<",INK,1); st_hit(x,cy+2,30,30,1999); x+=40; }
      for(int k=n-1;k>=0;k--){ const char* t=pg_title(chain[k]); int last=(k==0); if(!g_gm_ready)gmetrics(); int sz=(last||n==1)?2:1; int tw=pw_str(t,sz);
          int hov=!last&&in_rect(mx,my,x,cy,tw,36); pdstr(x,sz==2?cy:cy+12,t,last?INK:(hov?ACC:rgb(110,118,136)),sz); if(!last)st_hit(x,cy,tw,36,1000+chain[k]); x+=tw+8; if(!last){ pdstr(x,cy+12,">",rgb(150,156,170),1); x+=pw_str(">",1)+8; } } }
    cy+=50; if(g_stpg<9){ pdstr(st_x0,cy-6,CATD[g_stpg],rgb(128,136,154),1); cy+=26; }
    st_page(g_stpg,cy); }
static void old_leaf(int cat,int cy){ int X1=st_x0+st_w0-24; int g_setcat_sv=g_setcat; g_setcat=cat;
    if(g_setcat==0){
        int y=st_card(cy,64,"Clock"); st_rowtext(y,"Show seconds","Adds seconds to the clock in the top bar"); st_toggle(X1-44,y+20,g_showsec,1); cy=y+64+28;
        y=st_card(cy,128,"Power"); st_rowtext(y,"Restart","Restart the computer now"); st_btn(X1-110,y+16,110,"Restart",2,1); st_sep(y+64);
        st_rowtext(y+64,"Crash test","Triggers a kernel panic screen (for testing)"); st_btn(X1-110,y+80,110,"Crash",3,2); cy=y+128+28;
        y=st_card(cy,64,"Build"); { char b[64],*p=b; p=scopy(p,"NoovexOS 64 v"); p=scopy(p,(char*)g_buildsig.version); *p=0; st_rowtext(y,b,g_sig_ok?"Integrity check passed at boot":"Integrity check reported a mismatch at boot"); }
        fill_round(X1-110,y+20,110,24,12,15,g_sig_ok?rgb(226,246,233):rgb(253,236,226)); dstr_ctr(X1-55,y+25,g_sig_ok?"Verified":"Mismatch",g_sig_ok?rgb(40,140,80):rgb(200,110,40),1);
    } else if(g_setcat==1){
        wall_thumbs_build(); int cols=3, gap=14, tw=(st_w0-40-gap*(cols-1))/cols; if(tw>132)tw=132; int th=tw*10/16; int y=cy;
        if(st_part!=2){ y=st_card(cy,((NWALL+cols-1)/cols)*(th+30)+26,"Wallpaper");
        for(int k=0;k<NWALL;k++){ int bx=st_x0+20+(k%cols)*(tw+gap), by=y+18+(k/cols)*(th+30);
            if(k==g_theme&&!g_wallmode)fill_round(bx-3,by-3,tw+6,th+6,9,15,ACC);
            for(int yy=0;yy<th;yy++){ int ty=yy*40/th; for(int xx=0;xx<tw;xx++){ if(cut(xx,yy,tw,th,6,15))continue; putpx(bx+xx,by+yy,wall_thumb[k][ty*64+xx*64/tw]); } }
            dstr_ctr(bx+tw/2,by+th+7,THN[k],k==g_theme?INK:rgb(120,128,146),1); st_hit(bx,by,tw,th,10+k); }
        cy=y+((NWALL+cols-1)/cols)*(th+30)+26+28; }
        if(st_part!=1){ y=st_card(cy,128,"Colour and pointer"); st_rowtext(y,"Accent colour",ACN[g_accent]);
        for(int k=0;k<5;k++){ int bx=X1-16-(4-k)*40; if(k==g_accent)fill_circle(bx,y+32,15,ACV[k]); fill_circle(bx,y+32,k==g_accent?13:12,k==g_accent?rgb(255,255,255):ACV[k]); if(k==g_accent)fill_circle(bx,y+32,9,ACV[k]); st_hit(bx-15,y+17,30,30,20+k); }
        st_sep(y+64); st_rowtext(y+64,"Pointer style",0); { int w=st_segw(CUN,5); st_seg(X1-w,y+81,CUN,5,g_cursor,30); } }
    } else if(g_setcat==2){
        if(!pci_n)pci_scan(); int ek=nic_index();
        int y=st_card(cy,64,"Ethernet"); st_rowtext(y,ek>=0?(char*)nic_name(pci_ven[ek],pci_dev[ek]):"No wired adapter",net_ok?"Connected":"Not started - run 'net' in the Terminal");
        if(net_ok){ char b[40],*p=b; p=udec(p,my_ip>>24);*p++='.';p=udec(p,(my_ip>>16)&255);*p++='.';p=udec(p,(my_ip>>8)&255);*p++='.';p=udec(p,my_ip&255); *p=0; dstr(X1-slen(b)*8,y+24,b,INK,1); }
        cy=y+64+28;
        int rows=g_apn<8?g_apn:8; int extra=(g_wifi_entry&&g_wifi_sel>=0)?44:0; int ch=64+(rows?rows*44:40)+extra;
        y=st_card(cy,ch,"Wi-Fi"); { u32 wm=rtw_find_mmio(); st_rowtext(y,wm?"Wireless adapter":"No Wi-Fi adapter",wm?(g_rxnum?g_wifi_status:"Run wifi init in the Terminal first"):"This computer has no supported Wi-Fi card"); }
        st_wifiscan_x=X1-96; st_wifiscan_y=y+16; st_btn(X1-96,y+16,96,g_wifi_scanning?"Scanning":"Scan",40,1); st_sep(y+64);
        int ry=y+64; st_wifi_ln=0;
        if(!rows)dstr(st_x0+20,ry+14,"Press Scan to find networks",rgb(150,158,176),1);
        for(int q=0;q<rows;q++){ int open=(g_aps[q].sec==0), isconn=0;
            if(g_wifi_conn[0]){ int kk=0; while(g_aps[q].ssid[kk]&&g_wifi_conn[kk]&&g_aps[q].ssid[kk]==g_wifi_conn[kk])kk++; isconn=(!g_aps[q].ssid[kk]&&!g_wifi_conn[kk]); }
            int sel=(g_wifi_sel==q); if(sel||in_rect(mx,my,st_x0+8,ry+4,st_w0-16,36))fill_round(st_x0+8,ry+4,st_w0-16,36,8,15,sel?mix(ACC,rgb(255,255,255),215):rgb(245,247,250));
            for(int b=0;b<3;b++)fill_rect(st_x0+22+b*5,ry+26-b*5,3,4+b*5,isconn?ACC:rgb(120,128,146));
            dstr(st_x0+48,ry+14,g_aps[q].ssid[0]?g_aps[q].ssid:"<hidden>",INK,1);
            const char* tag=isconn?"Connected":open?"Open":"Secured"; dstr(X1-slen(tag)*8,ry+14,tag,isconn?rgb(40,150,90):rgb(130,138,156),1);
            st_hit(st_x0+8,ry+4,st_w0-16,36,50+q); ry+=44;
            if(sel&&!open&&g_wifi_entry){ fill_round(st_x0+20,ry+2,st_w0-150,32,8,15,rgb(240,242,246)); char mask[40]; int n=g_wifi_pwlen>36?36:g_wifi_pwlen; for(int z=0;z<n;z++)mask[z]='*'; mask[n]=0;
                dstr(st_x0+32,ry+11,mask[0]?mask:"Password",mask[0]?INK:rgb(150,158,176),1); if((g_ticks/50)&1)fill_rect(st_x0+33+n*8,ry+10,2,15,ACC);
                st_wifijoin_x=X1-100; st_wifijoin_y=ry+2; st_btn(X1-100,ry+2,100,"Join",41,1); ry+=44; } }
    } else if(g_setcat==3){
        static const char* HZ[3]={"30 Hz","60 Hz","120 Hz"}; static const char* PN[2]={"LCD","OLED"}; static const char* MA[3]={"Off","Low","High"};
        int y=st_card(cy,64*3,"Screen"); st_rowtext(y,"Refresh rate","How often the desktop redraws"); { int w=st_segw(HZ,3); st_seg(X1-w,y+17,HZ,3,g_refresh_hz==30?0:g_refresh_hz==60?1:2,60); } st_sep(y+64);
        st_rowtext(y+64,"Panel type","OLED shifts pixels slightly to avoid burn-in"); { int w=st_segw(PN,2); st_seg(X1-w,y+81,PN,2,g_panel_oled,63); } st_sep(y+128);
        { char b[48],*p=b; p=udec(p,W); *p++='x'; p=udec(p,H); p=scopy(p,"  "); p=udec(p,BPP); p=scopy(p,"-bit colour"); *p=0; st_rowtext(y+128,"Resolution",b); }
        cy=y+192+28;
        if(g_bl.ok){ y=st_card(cy,64,"Brightness"); { char b[24],*p=b; p=udec(p,(u32)(g_bl.cur<0?0:g_bl.cur)); *p++='%'; *p=0; st_rowtext(y,"Screen brightness",b); }
            int bw=st_w0-320; if(bw<140)bw=140; int bx=X1-bw-10; st_blx=bx; st_blw=bw; int fw=(g_bl.cur<0?0:g_bl.cur)*bw/100;
            fill_round(bx,y+28,bw,8,4,15,rgb(226,229,236)); if(fw>0)fill_round(bx,y+28,fw,8,4,15,ACC); fill_circle(bx+fw,y+33,11,rgb(200,204,216)); fill_circle(bx+fw,y+32,10,rgb(255,255,255)); fill_circle(bx+fw,y+32,4,ACC);
            st_hit(bx-12,y+14,bw+24,36,120); cy=y+64+28; }
        y=st_card(cy,64*3,"Performance"); st_rowtext(y,"Mouse acceleration","Moves faster when you move the mouse quickly"); { int w=st_segw(MA,3); st_seg(X1-w,y+17,MA,3,g_mouse_accel,65); } st_sep(y+64);
        st_rowtext(y+64,"2D acceleration","Use SSE for fills and frame copies"); st_toggle(X1-44,y+84,g_accel2d,68); st_sep(y+128);
        st_rowtext(y+128,"Performance overlay","Show an FPS counter in the top-right corner"); st_toggle(X1-44,y+148,g_fps_overlay,70);
    } else if(g_setcat==4){
        battery_tick(); int y=st_card(cy,150,0);
        if(bat_present){ char b[16],*p=b; p=udec(p,(u32)bat_pct); *p++='%'; *p=0; pdstr(st_x0+24,y+22,b,INK,3);
            dstr(st_x0+24,y+72,bat_charging?"Plugged in, charging":"On battery",bat_charging?rgb(40,150,90):rgb(150,110,70),1);
            int bw=st_w0-48; fill_round(st_x0+24,y+98,bw,16,8,15,rgb(230,233,240)); fill_round(st_x0+24,y+98,(int)bat_pct*bw/100,16,8,15,bat_charging?rgb(80,190,120):(bat_pct<=20?rgb(230,90,90):ACC));
            { char b[64],*p=b; if(bat_via_ec==2){ if(g_acb_mins>=0){ p=udec(p,(u32)(g_acb_mins/60)); p=scopy(p," h "); p=udec(p,(u32)(g_acb_mins%60)); p=scopy(p,bat_charging?" min until full - ":" min left - "); } p=scopy(p,"reported by the ACPI firmware"); } else p=scopy(p,bat_via_ec?"Read from the Embedded Controller":"Estimated"); *p=0;
              dstr(st_x0+24,y+124,b,rgb(130,138,156),1); }
            int rows=0; for(int i=0;i<g_acb_n;i++) if(g_acb[i].present)rows++;
            if(bat_via_ec==2&&rows){ cy=y+150+28; y=st_card(cy,64*rows,"Battery details"); int ry=y;
                for(int i=0;i<g_acb_n;i++){ acbat* b2=&g_acb[i]; if(!b2->present)continue; char t[64],*p=t; p=scopy(p,b2->model[0]?b2->model:"Battery"); if(b2->oem[0]){ p=scopy(p,"  "); p=scopy(p,b2->oem); } *p=0;
                    char d[80],*q=d; u32 cap=b2->full!=0xFFFFFFFFu?b2->full:0; if(cap&&b2->design&&b2->design!=0xFFFFFFFFu){ q=scopy(q,"Health "); q=udec(q,(u32)((u64)cap*100/b2->design)); q=scopy(q,"%  ("); q=udec(q,cap); q=scopy(q," of "); q=udec(q,b2->design); q=scopy(q,b2->mwh?" mWh)":" mAh)"); }
                    if(b2->cycles&&b2->cycles!=0xFFFFFFFFu){ q=scopy(q,"   "); q=udec(q,b2->cycles); q=scopy(q," cycles"); } *q=0;
                    st_rowtext(ry,t,d); ry+=64; if(ry<y+64*rows)st_sep(ry); } } }
        else { pdstr(st_x0+24,y+30,"No battery detected",INK,2); dstr(st_x0+24,y+70,"Running on mains power, or the battery can't be read.",rgb(130,138,156),1); dstr(st_x0+24,y+92,"Desktops and virtual machines usually have none.",rgb(130,138,156),1); }
    } else if(g_setcat==5){
        gdisk_ensure(); int y=st_card(cy,120,"System disk");
        if(g_dktype!=DK_NONE){ char b[64],*p=b; p=scopy(p,gdisk_name()); p=scopy(p," disk"); *p=0; pdstr(st_x0+20,y+16,b,INK,1);
            p=b; p=st_mb(p,(u64)g_dksectors*512ULL); if(g_dktype==DK_NVME&&g_nvme_bsz>512)p=scopy(p,"   4K sectors"); *p=0; dstr(X1-slen(b)*8,y+16,b,rgb(120,128,146),1);
            long used=0; int nf=0; for(int k=0;k<FS_N;k++)if(FS[k].used){ used+=FS[k].len; nf++; }
            int bw=st_w0-40; fill_round(st_x0+20,y+46,bw,12,6,15,rgb(230,233,240)); if(disk_ok){ u64 t4=0,f4=0; n4_usage(&t4,&f4); long cap=t4?(long)t4:(long)g_dksectors*512L; if(t4)used=(long)(t4-f4); if(cap<=0)cap=1; int fw=(int)((long long)used*bw/cap); if(fw<6)fw=6; fill_round(st_x0+20,y+46,fw,12,6,15,ACC); }
            p=b; if(disk_ok){ p=udec(p,(u32)nf); p=scopy(p," files, "); p=udec(p,(u32)(used/1024)); p=scopy(p," KB in "); p=scopy(p,g_nvx_note); } else { p=scopy(p,"Files are kept in memory only - "); p=scopy(p,g_nvx_note[0]?g_nvx_note:"no data partition"); } *p=0;
            dstr(st_x0+20,y+72,b,disk_ok?rgb(40,150,90):rgb(170,120,60),1); }
        else { pdstr(st_x0+20,y+20,"No disk found",INK,1); dstr(st_x0+20,y+48,"No IDE, SATA or NVMe disk was detected.",rgb(130,138,156),1); }
        cy=y+120+28;
        y=st_card(cy,64,"Removable"); { char b[64],*p=b; if(msd_ok){ p=scopy(p,"USB drive, "); p=st_mb(p,(u64)(msd_lastlba+1)*512ULL); } else p=scopy(p,"No USB drive connected"); *p=0; st_rowtext(y,b,msd_ok?"Mounted - open it in File Explorer":"Plug in a USB stick to use it"); }
        cy=y+64+28;
        y=st_card(cy,64,"Install"); st_rowtext(y,"Install NoovexOS","Put this system on a disk so it starts without the ISO"); st_btn(X1-120,y+16,120,"Open Setup",95,1);
    } else if(g_setcat==6){
        int tw=(st_w0-40-5*10)/6; if(tw<70)tw=70; int th=64;
        int y=st_card(cy,th+50,"Theme");
        for(int k=0;k<6;k++){ const TTheme* t=&TTH[k]; int bx=st_x0+20+k*(tw+10), by=y+16; if(bx+tw>st_x0+st_w0-10)break;
            if(k==g_tth)fill_round(bx-3,by-3,tw+6,th+6,11,15,ACC); fill_round(bx,by,tw,th,9,15,t->bg); fill_round(bx,by,tw,14,9,1|2,t->bar);
            fill_circle(bx+9,by+7,2,rgb(236,106,100)); fill_circle(bx+16,by+7,2,rgb(240,190,80)); fill_circle(bx+23,by+7,2,rgb(110,200,120));
            dstr(bx+8,by+22,"$",t->green,1); fill_rect(bx+20,by+27,tw-40,3,t->fg); fill_rect(bx+8,by+42,tw-30,3,t->dim); fill_rect(bx+8,by+54,tw-50,3,t->blue); fill_rect(bx+tw-26,by+52,7,11,t->cur);
            dstr_ctr(bx+tw/2,by+th+8,t->name,k==g_tth?INK:rgb(120,128,146),1); st_hit(bx,by,tw,th,80+k); }
        cy=y+th+50+16;
        { static const char* OP[4]={"Off","Low","Medium","High"}; int sel=g_topa>=100?0:g_topa>=90?1:g_topa>=80?2:3;
          y=st_card(cy,128,"Window"); st_rowtext(y,"Transparency","See the desktop through the terminal"); { int w=st_segw(OP,4); st_seg(X1-w,y+17,OP,4,sel,110); } st_sep(y+64);
          st_rowtext(y+64,"Blur behind","Soften what shows through, like frosted glass"); st_toggle(X1-44,y+84,g_tblur,114); cy=y+128+16; }
        static const char* FN[2]={"Smooth","Pixel"}; static const char* CS[3]={"Block","Bar","Underline"};
        y=st_card(cy,64*3,"Text"); st_rowtext(y,"Font","Anti-aliased or classic bitmap glyphs"); { int w=st_segw(FN,2); st_seg(X1-w,y+17,FN,2,g_tfont?0:1,85); } st_sep(y+64);
        st_rowtext(y+64,"Cursor",0); { int w=st_segw(CS,3); st_seg(X1-w,y+81,CS,3,g_tcur,87); } st_sep(y+128);
        st_rowtext(y+128,"Status bar","Shows the user, current folder and size"); st_toggle(X1-44,y+148,g_tstatus,90);

    } else if(g_setcat==8){
        extern int g_lap_kind, g_lap_kbd, g_lap_kbdmax, g_lap_chg, g_lap_chg_ok, g_lap_tp_off; extern const char* g_lap_name; extern char g_prn_status[]; extern u32 g_prn_ip; extern char g_prn_model[];
        extern int g_syn_ready, g_etp_ready; extern int net_ok; extern u32 my_ip; extern int g_wnd_up; extern int g_snd_state; extern int gdisk_count(void); extern u32 gdisk_size_of(int); extern const char* gdisk_name_of(int); extern int gdisk_current(void);
        static const char* TABS[7]={"Input","USB","Storage","Network","Media","Printers","Laptop"};
        if(!st_notabs){ int w=st_segw(TABS,7); st_seg(st_x0,cy,TABS,7,g_devtab,140); cy+=52; }
        char b[160],*p; const char* rt[24]; char rd[24][96]; u32 rc[24]; int n=0;
        #define DROW(T,COL) do{ if(n<24){ rt[n]=(T); rc[n]=(COL); n++; } }while(0)
        u32 OK=rgb(40,140,80), WARN=rgb(200,120,40), OFF=rgb(140,146,160);
        if(!pci_n)pci_scan();
        if(g_devtab==0){
            DROW("Keyboard",OK); scopy(rd[n-1],"Built-in keyboard, PS/2 controller i8042")[0]=0;
            DROW("Touchpad",g_lap_tp_off?WARN:OK); p=rd[n-1]; p=scopy(p,g_tp.ok?(g_tp.mode==1?"Precision touchpad, HID over I2C":g_tp.proto==1?"ELAN touchpad, I2C":"HID touchpad over I2C"):g_syn_ready?"Synaptics touchpad, PS/2 absolute mode":g_etp_ready?"ELAN touchpad, PS/2":"PS/2 pointing device"); if(g_lap_tp_off)p=scopy(p,"  (switched off)"); *p=0;
            for(int k=0;k<g_usbdev_n&&n<24;k++) if(g_usbdev[k].used&&g_usbdev[k].hid){ DROW(g_usbdev[k].kind,OK); p=rd[n-1]; p=scopy(p,g_usbdev[k].name[0]?g_usbdev[k].name:"USB HID device"); p=scopy(p,", USB"); *p=0; } }
        else if(g_devtab==1){
            for(int k=0;k<pci_n&&n<24;k++) if(pci_cls[k]==0x0C&&pci_sub[k]==0x03){ DROW("USB controller",OK); p=rd[n-1]; p=hx(p,pci_ven[k],4); *p++=':'; p=hx(p,pci_dev[k],4); p=scopy(p,"  PCI bus "); p=udec(p,pci_bus[k]); *p=0; }
            for(int k=0;k<g_usbdev_n&&n<24;k++) if(g_usbdev[k].used){ DROW(g_usbdev[k].kind,g_usbdev[k].cam?WARN:OK); p=rd[n-1]; p=scopy(p,g_usbdev[k].name[0]?g_usbdev[k].name:"(no name)"); p=scopy(p,"  "); p=hx(p,g_usbdev[k].vid,4); *p++=':'; p=hx(p,g_usbdev[k].pid,4); p=scopy(p,"  port "); p=udec(p,(u32)g_usbdev[k].port+1); if(g_ticks-g_usbdev[k].t<1000)p=scopy(p,"  NEW"); *p=0; }
            if(msd_ok){ DROW("USB drive",OK); scopy(rd[n-1],ufat_ok?"Mass storage, FAT file system mounted":"Mass storage, no FAT file system")[0]=0; } }
        else if(g_devtab==2){
            for(int k=0;k<gdisk_count()&&n<24;k++){ DROW(gdisk_name_of(k),k==gdisk_current()?OK:OFF); p=rd[n-1]; u64 mb=(u64)gdisk_size_of(k)*512ULL/1048576ULL; if(mb>=1024){ p=udec(p,(u32)(mb/1024)); p=scopy(p," GB"); } else { p=udec(p,(u32)mb); p=scopy(p," MB"); } if(k==gdisk_current())p=scopy(p,"  (in use)"); *p=0; }
            for(int k=0;k<pci_n&&n<24;k++) if(pci_cls[k]==0x01){ DROW(pci_sub[k]==0x08?"NVMe controller":pci_sub[k]==0x06?"SATA controller":pci_sub[k]==0x04?"RAID / Intel RST controller":pci_sub[k]==0x01?"IDE controller":"Storage controller",OK); p=rd[n-1]; p=hx(p,pci_ven[k],4); *p++=':'; p=hx(p,pci_dev[k],4); *p=0; }
            if(!n){ DROW("No disks",WARN); scopy(rd[0],"No IDE, SATA, NVMe or VirtIO disk was found")[0]=0; } }
        else if(g_devtab==3){
            for(int k=0;k<pci_n&&n<24;k++) if(pci_cls[k]==0x02){ int wl=pci_sub[k]==0x80; DROW(wl?"Wi-Fi adapter":"Ethernet adapter",wl?(g_wnd_up?OK:OFF):(net_ok?OK:OFF)); p=rd[n-1]; p=scopy(p,(char*)nic_name(pci_ven[k],pci_dev[k])); p=scopy(p,wl?(g_wnd_up?"  connected":"  not connected"):(net_ok?"  connected":"  no link")); *p=0; }
            if(net_ok||g_wnd_up){ DROW("IP address",OK); p=rd[n-1]; for(int q=3;q>=0;q--){ p=udec(p,(my_ip>>(8*(3-q)))&0xFF); if(q)*p++='.'; } *p=0; }
            if(!n){ DROW("No network adapter",WARN); scopy(rd[0],"No supported Ethernet or Wi-Fi card was found")[0]=0; } }
        else if(g_devtab==4){
            for(int k=0;k<pci_n&&n<24;k++) if(pci_cls[k]==0x03){ DROW("Graphics",OK); p=rd[n-1]; p=scopy(p,pci_ven[k]==0x10DE?"NVIDIA":pci_ven[k]==0x8086?"Intel":pci_ven[k]==0x1002?"AMD":"GPU"); p=scopy(p,"  "); p=hx(p,pci_ven[k],4); *p++=':'; p=hx(p,pci_dev[k],4); *p=0; }
            DROW("Display",OK); p=rd[n-1]; p=udec(p,W); *p++='x'; p=udec(p,H); p=scopy(p,"  "); p=udec(p,BPP); p=scopy(p,"-bit framebuffer"); *p=0;
            for(int k=0;k<pci_n&&n<24;k++) if(pci_cls[k]==0x04){ DROW("Sound card",g_snd_state?OK:OFF); p=rd[n-1]; p=scopy(p,g_snd_state&&g_snd.name?g_snd.name:pci_sub[k]==0x03?"HD Audio":"Audio"); p=scopy(p,g_snd_state?"  driver active":"  driver not started"); *p=0; }
            for(int k=0;k<g_usbdev_n&&n<24;k++) if(g_usbdev[k].used&&g_usbdev[k].cam){ DROW("Camera",WARN); p=rd[n-1]; p=scopy(p,g_usbdev[k].name[0]?g_usbdev[k].name:"USB video camera"); p=scopy(p,"  detected, no video yet"); *p=0; } }
        else if(g_devtab==5){
            DROW("Network printer",g_prn_ip?OK:OFF); p=rd[n-1]; p=scopy(p,g_prn_model[0]?g_prn_model:g_prn_status); *p=0;
            if(g_prn_ip){ DROW("Status",OK); scopy(rd[n-1],g_prn_status)[0]=0; }
            for(int k=0;k<g_usbdev_n&&n<24;k++) if(g_usbdev[k].used&&g_usbdev[k].prn){ DROW("USB printer",WARN); p=rd[n-1]; p=scopy(p,g_usbdev[k].name[0]?g_usbdev[k].name:"USB printer"); p=scopy(p,"  use it over the network"); *p=0; } }
        else if(g_devtab==6){
            if(!g_lap_kind){ DROW("No laptop extras",OFF); scopy(rd[0],"No Lenovo ThinkPad/IdeaPad or ASUS firmware interface found")[0]=0; }
            else { DROW(g_lap_name,OK); scopy(rd[0],"Hotkeys, keyboard light and charge limit through ACPI")[0]=0; } }
        int y=st_card(cy,n*44+8,0);
        for(int r=0;r<n;r++){ int ry=y+6+r*44; fill_circle(st_x0+30,ry+20,5,rc[r]); pdstr(st_x0+46,ry+6,rt[r],INK,1); dstr(st_x0+46,ry+24,rd[r],rgb(120,128,146),1); if(r)fill_rect(st_x0+20,ry,st_w0-40,1,rgb(236,238,243)); }
        cy=y+n*44+8+24;
        if(g_devtab==5&&g_prn_ip){ st_btn(st_x0,cy,150,"Print test page",134,1); cy+=48; }
        if(g_devtab==6&&g_lap_kind){ static const char* KL[4]={"Off","Low","Medium","High"}; static const char* KL3[3]={"Off","Low","High"};
            y=st_card(cy,128,0);
            st_rowtext(y,"Keyboard light",g_lap_kbdmax?0:"Not available on this laptop");
            if(g_lap_kbdmax==2){ int w=st_segw(KL3,3); st_seg(X1-w,y+17,KL3,3,g_lap_kbd<0?0:g_lap_kbd,130); } else if(g_lap_kbdmax>=3){ int w=st_segw(KL,4); st_seg(X1-w,y+17,KL,4,g_lap_kbd<0?0:g_lap_kbd,130); } else if(g_lap_kbdmax==1){ st_toggle(X1-44,y+20,g_lap_kbd>0,135); }
            st_sep(y+64); st_rowtext(y+64,"Battery charge limit",g_lap_chg_ok?(g_lap_kind==2?"Conservation mode":"Stop charging at 80%"):"Not available on this laptop"); if(g_lap_chg_ok)st_toggle(X1-44,y+84,g_lap_chg,136); }
        #undef DROW
    } else {
        int y=st_card(cy,110,0); draw_logo(st_x0+62,y+22,54,ACC,256); pdstr(st_x0+120,y+26,"NoovexOS 64",INK,2);
        { char b[48],*p=b; p=scopy(p,"Version "); p=scopy(p,(char*)g_buildsig.version); p=scopy(p,"  -  built from scratch"); *p=0; dstr(st_x0+120,y+64,b,rgb(120,128,146),1); }
        cy=y+110+28; y=st_card(cy,64*4,"Hardware");
        char b[96],*p;
        st_rowtext(y,"Processor",0); { char pb[48]; int n=slen(g_brand); if(n>(st_w0-200)/8){ int m=(st_w0-200)/8; for(int q=0;q<m;q++)pb[q]=g_brand[q]; pb[m]=0; } else scopy(pb,g_brand)[0]=0; dstr(X1-slen(pb)*8,y+24,pb,rgb(90,98,116),1); } st_sep(y+64);
        st_rowtext(y+64,"Memory",0); p=b; p=udec(p,g_ram_mb); p=scopy(p," MB"); *p=0; dstr(X1-slen(b)*8,y+88,b,rgb(90,98,116),1); st_sep(y+128);
        st_rowtext(y+128,"Display",0); p=b; p=udec(p,W); *p++='x'; p=udec(p,H); p=scopy(p,"  "); p=udec(p,BPP); p=scopy(p,"-bit"); *p=0; dstr(X1-slen(b)*8,y+152,b,rgb(90,98,116),1); st_sep(y+192);
        st_rowtext(y+192,"Kernel",0); p=b; p=udec(p,g_buildsig.image_size/1024); p=scopy(p," KB, x86-64 long mode"); *p=0; dstr(X1-slen(b)*8,y+216,b,rgb(90,98,116),1);
    }

    g_setcat=g_setcat_sv; }

void settings_click(int px,int py){
    for(int h=st_hn-1;h>=0;h--) if(in_rect(px,py,st_hx[h],st_hy[h],st_hw[h],st_hh[h])){ int id=st_hid[h];
        if(id>=140&&id<147){ g_devtab=id-140; return; }
        if(id>=130&&id<134){ extern void laptop_kbd_set(int); laptop_kbd_set(id-130); return; }
        if(id==134){ extern int prn_test_page(void); prn_test_page(); return; }
        if(id==135){ extern int g_lap_kbd; extern void laptop_kbd_set(int); laptop_kbd_set(g_lap_kbd>0?0:1); return; }
        if(id==136){ extern int g_lap_chg; extern void laptop_charge_set(int); laptop_charge_set(!g_lap_chg); return; }
        if(id>=100&&id<109){ g_stpg=id-100; g_wifi_entry=0; return; }
        if(id==1999){ int pp=pg_parent(g_stpg); if(pp>=0)g_stpg=pp; return; }
        if(id>=1000&&id<1200){ g_stpg=id-1000; return; }
        if(id>=2000&&id<2000+NWIN){ open_win(id-2000); return; }
        if(id==300){ extern int g_gfx; extern int igpu_draw_mode(int); igpu_draw_mode(g_gfx?0:1); return; }
        if(id==301){ extern int g_gfx_fx; g_gfx_fx^=1; return; } if(id==302){ extern int g_gfx_srec; g_gfx_srec^=1; return; }
        if(id==303){ extern int g_wall_fx; g_wall_fx^=1; build_bg(); return; } if(id==304){ extern int g_wall_light; g_wall_light^=1; return; }
        if(id==310||id==311){ extern int g_pres_eng; g_pres_eng=id-310; return; }
        if(id==312){ extern int g_igpu_flip; extern int igpu_flip_on(void); extern void igpu_flip_off(const char*); if(g_igpu_flip)igpu_flip_off("turned off in Settings"); else igpu_flip_on(); return; }
        if(id==313){ extern int frame_check_cmd(void); frame_check_cmd(); return; }
        if(id==322){ extern void snd_rec_gain(int); int pc=(px-st_mlx)*100/(st_mlw>0?st_mlw:1); snd_rec_gain(pc); return; }
        if(id==323){ open_win(W_RECORDER); return; }
        if(id==320){ int pc=(px-st_blx)*100/(st_blw>0?st_blw:1); if(pc<0)pc=0; if(pc>100)pc=100; g_snd_vol=pc; if(g_snd_state)g_snd.volume(pc); return; }
        if(id==321){ static int sv=50; if(g_snd_vol){ sv=g_snd_vol; g_snd_vol=0; } else g_snd_vol=sv?sv:50; if(g_snd_state)g_snd.volume(g_snd_vol); return; }
        if(id==1){ g_showsec^=1; return; } if(id==2){ power_reboot(); return; } if(id==3){ panic(0,"Crash test triggered from Settings."); return; }
        if(id>=10&&id<10+NWALL){ g_theme=id-10; g_wallmode=0; build_bg(); return; }
        if(id>=20&&id<25){ g_accent=id-20; ACC=ACV[g_accent]; ACC2=mix(ACC,rgb(0,0,0),60); build_bg(); return; }
        if(id>=30&&id<35){ g_cursor=id-30; return; }
        if(id==40){ wifi_gui_scan(1); return; }
        if(id==41){ if(g_wifi_entry&&g_wifi_sel>=0){ g_wifi_pw[g_wifi_pwlen]=0; wifi_gui_connect(g_wifi_sel); g_wifi_entry=0; } return; }
        if(id>=50&&id<58){ int q=id-50; if(g_aps[q].sec==0){ g_wifi_sel=q; g_wifi_entry=0; wifi_gui_connect(q); } else { g_wifi_sel=q; g_wifi_entry=1; g_wifi_pwlen=0; g_wifi_pw[0]=0; } return; }
        if(id>=60&&id<63){ static const int hz[3]={30,60,120}; g_refresh_hz=hz[id-60]; return; }
        if(id==63||id==64){ g_panel_oled=(id==64); return; }
        if(id>=65&&id<68){ g_mouse_accel=id-65; return; }
        if(id==68){ g_accel2d^=1; return; } if(id==70){ g_fps_overlay^=1; return; }
        if(id>=80&&id<86){ g_tth=id-80; return; }
        if(id>=110&&id<114){ static const int op[4]={100,90,80,65}; g_topa=op[id-110]; return; } if(id==114){ g_tblur^=1; return; } if(id==85||id==86){ g_tfont=(id==85); return; }
        if(id>=87&&id<90){ g_tcur=id-87; return; } if(id==90){ g_tstatus^=1; term_geom(); return; }
        if(id==95){ ip_open(ST_DESKTOP); return; }
        if(id==120){ int pc=(px-st_blx)*100/(st_blw>0?st_blw:1); if(pc<0)pc=0; if(pc>100)pc=100; bl_set(pc); return; }
        return; }
}

