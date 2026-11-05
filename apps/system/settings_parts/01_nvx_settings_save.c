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
int g_wifi_scanning=0; char g_wifi_status[96]={'r','e','a','d','y',0};

static const char *SC_LBL[9]={"System","Personalization","Network","Display","Battery","Storage","Terminal","About","Devices"};
static const char *SC_SUB[9]={"Clock, power and build integrity","Wallpaper, accent colour and pointer","Ethernet and Wi-Fi","Refresh rate, panel and pointer speed","Charge level and power source","Disks, data partition and install","Theme, font and cursor for nsh","Version and hardware","Mice, keyboards, touchpad, printer, cameras and laptop keys"};
static int st_hx[64],st_hy[64],st_hw[64],st_hh[64],st_hid[64],st_hn=0, st_x0,st_w0;
static int st_blx, st_blw=1, st_mlx, st_mlw=1;
static void st_hit(int x,int y,int w,int h,int id){ if(st_hn<64){ st_hx[st_hn]=x; st_hy[st_hn]=y; st_hw[st_hn]=w; st_hh[st_hn]=h; st_hid[st_hn]=id; st_hn++; } }
static void st_ring(int cx,int cy,int r,int t,u32 c,u32 bg){ fill_circle(cx,cy,r,c); fill_circle(cx,cy,r-t,bg); }
static void st_glyph(int x,int y,int k,u32 w,u32 c){
    switch(k){
    case 0: fill_circle(x+13,y+13,7,w); fill_circle(x+13,y+13,3,c); for(int a=0;a<4;a++){ int dx=(a==0)?0:(a==1)?8:(a==2)?0:-8, dy=(a==0)?-8:(a==1)?0:(a==2)?8:0; fill_rect(x+12+dx,y+12+dy,3,3,w); } break;
    case 1: fill_circle(x+13,y+13,8,w); fill_circle(x+10,y+10,2,c); fill_circle(x+16,y+10,2,c); fill_circle(x+10,y+16,2,c); fill_circle(x+17,y+17,3,c); break;
    case 2: for(int rr=3;rr>=1;rr--){ fill_circle(x+13,y+18,rr*4-1,w); fill_circle(x+13,y+18,rr*4-3,c); } fill_rect(x+1,y+19,24,6,c); fill_circle(x+13,y+18,2,w); break;
    case 3: fill_round(x+5,y+6,16,11,2,15,w); fill_rect(x+7,y+8,12,7,c); fill_rect(x+11,y+18,4,2,w); fill_rect(x+8,y+20,10,2,w); break;
    case 4: fill_round(x+5,y+8,15,10,2,15,w); fill_rect(x+20,y+11,2,4,w); fill_rect(x+7,y+10,8,6,c); break;
    case 5: fill_round(x+5,y+6,16,14,3,15,w); fill_rect(x+7,y+14,12,1,c); fill_circle(x+17,y+17,1,c); break;
    case 6: fill_round(x+4,y+6,18,14,3,15,w); pdstr(x+6,y+6,">",c,1); fill_rect(x+13,y+16,6,2,c); break;
    case 7: fill_circle(x+13,y+13,8,w); fill_rect(x+12,y+12,3,6,c); fill_rect(x+12,y+8,3,2,c); break;
    case 8: fill_round(x+8,y+4,10,17,5,15,w); fill_rect(x+12,y+5,2,6,c); break;
    case 9: st_ring(x+13,y+13,8,3,w,c); fill_rect(x+13,y+3,8,9,c); fill_rect(x+15,y+4,6,2,w); fill_rect(x+19,y+4,2,6,w); fill_rect(x+5,y+14,8,9,c); fill_rect(x+5,y+20,6,2,w); fill_rect(x+5,y+16,2,6,w); break;
    case 10: st_ring(x+14,y+13,9,2,w,c); fill_rect(x+3,y+3,14,21,c); fill_rect(x+6,y+10,4,6,w); for(int i=0;i<5;i++)fill_rect(x+10+i,y+10-i,1,6+2*i,w); break;
    case 11: fill_circle(x+13,y+10,5,w); fill_round(x+5,y+16,16,9,4,15,w); fill_rect(x+5,y+23,16,3,c); break;
    case 12: fill_round(x+7,y+5,12,10,3,15,w); for(int i=0;i<7;i++)fill_rect(x+7+i,y+14+i,12-2*i,1,w); fill_rect(x+12,y+9,2,6,c); fill_rect(x+10,y+11,6,2,c); break;
    case 13: st_ring(x+13,y+13,8,2,w,c); fill_rect(x+12,y+8,2,6,w); fill_rect(x+12,y+12,5,2,w); break;
    case 14: for(int a=0;a<2;a++)for(int b2=0;b2<2;b2++)fill_round(x+6+a*8,y+6+b2*8,6,6,2,15,w); break;
    case 15: fill_round(x+7,y+7,12,12,2,15,w); fill_rect(x+10,y+10,6,6,c); for(int i=0;i<3;i++){ fill_rect(x+9+i*3,y+4,1,3,w); fill_rect(x+9+i*3,y+19,1,3,w); fill_rect(x+4,y+9+i*3,3,1,w); fill_rect(x+19,y+9+i*3,3,1,w); } break;
    case 16: st_ring(x+13,y+13,8,3,w,c); fill_rect(x+12,y+4,2,6,c); fill_rect(x+12,y+16,2,6,c); fill_rect(x+4,y+12,6,2,c); fill_rect(x+16,y+12,6,2,c); break; }
    }

extern int g_dstyle;
#define ST_CL (g_dstyle==1)
#define ST_FACE rgb(212,208,200)
#define ST_BG (ST_CL?ST_FACE:rgb(250,250,251))
#define ST_SIDE (ST_CL?rgb(255,255,255):rgb(235,235,238))
#define ST_LINE (ST_CL?rgb(128,128,128):rgb(222,222,226))
#define ST_SEPC (ST_CL?rgb(128,128,128):rgb(234,234,237))
#define ST_FG (ST_CL?rgb(0,0,0):rgb(46,46,52))
#define ST_DIM (ST_CL?rgb(64,64,64):rgb(118,118,126))
#define ST_HL rgb(210,192,182)
static void st_bold(int x,int y,const char* t,u32 c){ if(!g_gm_ready)gmetrics(); pdstr(x,y,t,c,1); pdstr(x+1,y,t,c,1); }
static void st_bold_ctr(int cx,int y,const char* t,u32 c){ if(!g_gm_ready)gmetrics(); int w=pw_str(t,1)+1; st_bold(cx-w/2,y,t,c); }
static void st_chev(int x,int y,u32 c,int left){ for(int i=0;i<5;i++){ int dx=left?4-i:i; fill_rect(x+dx,y+i,2,1,c); fill_rect(x+dx,y+8-i,2,1,c); } }
static void st_toggle(int x,int y,int on,int id){ if(ST_CL){ int bx=x+18, by=y+4; ui_bevel(bx,by,16,16,1); fill_rect(bx+2,by+2,12,12,in_rect(mx,my,bx-6,by-6,28,28)?rgb(236,233,226):rgb(255,255,255)); if(on){ for(int t=0;t<3;t++){ fill_rect(bx+4+t,by+7+t,1,3,0); } for(int t=0;t<5;t++) fill_rect(bx+7+t,by+9-t,1,3,0); } st_hit(bx-10,by-8,36,32,id); return; } int hov=in_rect(mx,my,x-4,y-4,52,32); u32 tr=on?(hov?ACC2:ACC):(hov?rgb(200,200,204):rgb(214,214,218)); x-=4; y-=1; fill_round(x,y,48,26,13,15,tr); int kx=on?x+35:x+13; fill_circle(kx,y+14,10,mix(tr,rgb(0,0,0),30)); fill_circle(kx,y+13,10,rgb(255,255,255)); st_hit(x,y-3,56,32,id); }
static int st_segw(const char** l,int n){ if(!g_gm_ready)gmetrics(); int w=0; for(int k=0;k<n;k++)w+=pw_str(l[k],1)+30; return w+4; }
static void st_seg(int x,int y,const char** l,int n,int sel,int idb){ int w=st_segw(l,n); y-=1; if(ST_CL){ (void)w; int cx=x; for(int k=0;k<n;k++){ int bw=pw_str(l[k],1)+30; int on=(k==sel); ui_bevel(cx,y+2,bw,27,on); fill_rect(cx+2,y+4,bw-4,23,on?rgb(236,233,226):(in_rect(mx,my,cx,y+2,bw,27)?rgb(226,222,214):ST_FACE)); if(on)st_bold_ctr(cx+bw/2,y+8,l[k],ST_FG); else pdstr_ctr(cx+bw/2,y+8,l[k],ST_FG,1); st_hit(cx,y+2,bw,27,idb+k); cx+=bw; } return; } fill_round(x,y,w,32,9,15,rgb(232,232,236)); int cx=x+2;
    for(int k=0;k<n;k++){ int bw=pw_str(l[k],1)+30; if(k==sel){ fill_round(cx,y+3,bw,27,7,15,rgb(210,210,214)); fill_round(cx,y+2,bw,27,7,15,rgb(255,255,255)); } else if(in_rect(mx,my,cx,y+2,bw,27))fill_round(cx,y+2,bw,27,7,15,rgb(242,242,245));
        if(k==sel)st_bold_ctr(cx+bw/2,y+8,l[k],ST_FG); else pdstr_ctr(cx+bw/2,y+8,l[k],ST_DIM,1); st_hit(cx,y+2,bw,27,idb+k); cx+=bw; } }
static void st_btn(int x,int y,int w,const char* t,int id,int kind){ int hov=in_rect(mx,my,x,y,w,34); if(ST_CL){ if(!g_gm_ready)gmetrics(); ui_bevel(x,y+2,w,28,0); fill_rect(x+2,y+4,w-4,24,hov?rgb(226,222,214):ST_FACE); if(kind==1){ fill_rect(x,y+2,w,1,0); fill_rect(x,y+2,1,28,0); fill_rect(x+w-1,y+2,1,28,0); fill_rect(x,y+29,w,1,0); } pdstr_ctr(x+w/2,y+9,t,kind==2?rgb(160,0,0):ST_FG,1); st_hit(x,y,w,34,id); return; }
    u32 bg=kind==1?(hov?ACC2:ACC):kind==2?(hov?rgb(250,222,222):rgb(252,234,234)):(hov?rgb(224,224,228):rgb(234,234,238)); u32 fg=kind==1?rgb(255,255,255):kind==2?rgb(192,28,40):ST_FG;
    if(!g_gm_ready)gmetrics(); fill_round(x,y,w,34,9,15,bg); st_bold_ctr(x+w/2,y+10,t,fg); st_hit(x,y,w,34,id); }
static int st_card(int y,int h,const char* title){ if(!g_gm_ready)gmetrics();
    if(ST_CL){ int gy=title?y+8:y; if(h>0){ int H2=h+(title?18:0); fill_rect(st_x0,gy,st_w0,1,rgb(128,128,128)); fill_rect(st_x0,gy+1,st_w0,1,rgb(255,255,255)); fill_rect(st_x0,gy,1,H2,rgb(128,128,128)); fill_rect(st_x0+1,gy+1,1,H2-1,rgb(255,255,255)); fill_rect(st_x0+st_w0-2,gy,1,H2,rgb(128,128,128)); fill_rect(st_x0+st_w0-1,gy,1,H2+1,rgb(255,255,255)); fill_rect(st_x0,gy+H2-1,st_w0,1,rgb(128,128,128)); fill_rect(st_x0,gy+H2,st_w0,1,rgb(255,255,255)); }
        if(title){ int l=pw_str(title,1); fill_rect(st_x0+8,y,l+8,16,ST_FACE); pdstr(st_x0+12,y+1,title,ST_FG,1); return y+26; } return y; } if(title){ st_bold(st_x0+2,y+2,title,ST_FG); y+=26; }
    if(h>0){ fill_round(st_x0,y,st_w0,h,12,15,ST_LINE); fill_round(st_x0+1,y+1,st_w0-2,h-2,11,15,rgb(255,255,255)); } return y; }
static void st_rowtext(int y,const char* t,const char* d){ if(!g_gm_ready)gmetrics(); pdstr(st_x0+20,d?y+14:y+25,t,ST_FG,1); if(d)pdstr(st_x0+20,y+35,d,ST_DIM,1); }
static void st_sep(int y){ if(ST_CL){ fill_rect(st_x0+10,y,st_w0-20,1,rgb(128,128,128)); fill_rect(st_x0+10,y+1,st_w0-20,1,rgb(255,255,255)); return; } fill_rect(st_x0+1,y,st_w0-2,1,ST_SEPC); }
static char* st_mb(char* p,u64 bytes){ u64 mb=bytes/1048576ULL; if(mb>=1024){ p=udec(p,(u32)(mb/1024)); *p++='.'; p=udec(p,(u32)((mb%1024)*10/1024)); return scopy(p," GB"); } p=udec(p,(u32)mb); return scopy(p," MB"); }
static int st_part=0, st_notabs=0;
enum { P_DISPLAY=20, P_GRAPHICS, P_GFXADV, P_SOUND, P_POWER, P_STORAGE, P_ABOUT, P_NVIDIA, P_SCREENS, P_SERVICES, P_BACKGROUND=30, P_COLOURS, P_TERMINAL, P_WALLFX, P_WIFI=40, P_NETINFO, P_DEV0=50, P_MOUSE=57, P_APPS=60, P_ACCOUNT=70, P_TIME=80, P_LANG, P_SECURITY=90, P_RECOVERY=100, P_UPDATE=110, P_FOCUS=120 };
int g_stpg=0;
static const char* CATN[9]={"System","Personalization","Network & internet","Bluetooth & devices","Apps","Accounts","Time & language","Privacy & security","Update & recovery"};
static const char* CATD[9]={"Display, sound, power, storage","Background, colours, terminal","Wi-Fi, Ethernet, addresses","Input, USB, printers, laptop","Installed apps","Your account","Clock and region","Build integrity","Updates, previous version, recovery"};
static const int CATI[9]={0,1,2,8,14,11,13,12,9};
typedef struct { int pg, icon; const char* t; const char* d; } stile;
static const stile T_SYS[8]={{P_FOCUS,12,"Focus","Do not disturb, sleep, work, gaming"},{P_DISPLAY,3,"Display","Refresh rate, brightness, panel, graphics"},{P_SOUND,10,"Sound","Volume and output device"},{P_POWER,4,"Power & battery","Charge level, battery health"},{P_STORAGE,5,"Storage","Disks, data partition"},{P_GRAPHICS,15,"Graphics","GPU drawing, effects, wallpaper effects"},{P_ABOUT,7,"About","Device specifications, version"},{P_SERVICES,12,"Background services","Web requests, downloads, clock sync, power"}};
static const stile T_PER[4]={{P_BACKGROUND,1,"Background","Wallpaper picture"},{P_COLOURS,1,"Colours","Accent colour and pointer"},{P_WALLFX,1,"Wallpaper effects","Smooth colours, glow, light"},{P_TERMINAL,6,"Terminal","Theme, font, cursor, transparency"}};
static const stile T_NET[2]={{P_WIFI,2,"Wi-Fi & Ethernet","Connect to a network"},{P_NETINFO,2,"Network details","Addresses, adapters, gateway"}};
static const stile T_DEV[8]={{P_DEV0+0,8,"Input","Keyboard, touchpad, mice"},{P_MOUSE,8,"Mouse & pointer","Pointer speed and style"},{P_DEV0+1,8,"USB","Controllers and devices"},{P_DEV0+2,5,"Storage devices","Disks and controllers"},{P_DEV0+3,2,"Network adapters","Ethernet and Wi-Fi cards"},{P_DEV0+4,6,"Media","Graphics, display, sound, camera"},{P_DEV0+5,0,"Printers","Network printing"},{P_DEV0+6,4,"Laptop","Keyboard light, charge limit"}};
static const stile T_APP[1]={{P_APPS,14,"Installed apps","Open or find apps"}};
static const stile T_ACC[1]={{P_ACCOUNT,11,"Accounts","Your info, other users, guest, lock and sign-out"}};
static const stile T_TIM[2]={{P_TIME,13,"Date & time","Clock, seconds"},{P_LANG,2,"Language & region","Display language, keyboard"}};
static const stile T_PRV[1]={{P_SECURITY,12,"Device security","Build integrity check"}};
static const stile T_UPD[3]={{P_UPDATE,9,"NoovexOS Update","Check for updates, install them, go back"},{P_RECOVERY,16,"Recovery","Restart, install, crash test"},{P_ABOUT,7,"Version","NoovexOS version and build"}};
static const stile* cat_tiles(int c,int* n){ switch(c){ case 0:*n=8;return T_SYS; case 1:*n=4;return T_PER; case 2:*n=2;return T_NET; case 3:*n=8;return T_DEV; case 4:*n=1;return T_APP; case 5:*n=1;return T_ACC; case 6:*n=2;return T_TIM; case 7:*n=1;return T_PRV; default:*n=3;return T_UPD; } }
static int pg_parent(int p){ if(p<9)return -1; if(p==P_SERVICES||p==P_FOCUS)return 0; if(p==P_GFXADV||p==P_NVIDIA)return P_GRAPHICS; if(p==P_SCREENS)return P_DISPLAY; if(p==P_GRAPHICS)return P_DISPLAY; if(p>=P_DISPLAY&&p<=P_ABOUT)return 0; if(p>=P_BACKGROUND&&p<=P_WALLFX)return 1; if(p>=P_WIFI&&p<=P_NETINFO)return 2; if(p>=P_DEV0&&p<=P_MOUSE)return 3; if(p==P_APPS)return 4; if(p==P_ACCOUNT)return 5; if(p==P_TIME||p==P_LANG)return 6; if(p==P_SECURITY)return 7; return 8; }
static int pg_cat(int p){ while(p>=9)p=pg_parent(p); return p<0?0:p; }
static const char* pg_title(int p){ if(p<9)return CATN[p]; switch(p){ case P_GFXADV:return "Advanced graphics"; case P_NVIDIA:return "NVIDIA graphics"; case P_SCREENS:return "Screen and monitors"; case P_MOUSE:return "Mouse & pointer"; case P_SERVICES:return "Background services"; default:break; }
    for(int c=0;c<9;c++){ int n; const stile* t=cat_tiles(c,&n); for(int k=0;k<n;k++) if(t[k].pg==p)return t[k].t; } return "Settings"; }
static void st_tiles(int* cy,const stile* t,int n){ if(!g_gm_ready)gmetrics(); if(n<=0)return;
    int RH=56, y0=*cy; st_card(y0,n*RH,0);
    for(int k=0;k<n;k++){ int y=y0+k*RH; int hov=in_rect(mx,my,st_x0+1,y+1,st_w0-2,RH-2);
        u32 hb=ST_CL?ST_HL:rgb(246,246,248), nb=ST_CL?ST_FACE:rgb(255,255,255);
        if(hov)fill_round(st_x0+1,y+1,st_w0-2,RH-2,(k==0||k==n-1)?11:0,(k==0?3:0)|(k==n-1?12:0)|(n==1?15:0),hb);
        if(k)st_sep(y); st_glyph(st_x0+14,y+15,t[k].icon,ST_FG,hov?hb:nb);
        pdstr(st_x0+54,y+10,t[k].t,ST_FG,1); pdstr(st_x0+54,y+30,t[k].d,ST_DIM,1); st_chev(st_x0+st_w0-26,y+RH/2-4,ST_DIM,0); st_hit(st_x0,y,st_w0,RH,1000+t[k].pg); }
    *cy=y0+n*RH+24; }
static void st_infocard(int* cy,const char* title,const char** a,char b[][80],int n){ int y=st_card(*cy,n*44,title); int X1=st_x0+st_w0-20;
    for(int r=0;r<n;r++){ int ry=y+r*44; if(r)st_sep(ry); pdstr(st_x0+20,ry+14,a[r],ST_FG,1); { int w=pw_str(b[r],1); pdstr(X1-w,ry+14,b[r],ST_DIM,1); } } *cy=y+n*44+24; }
static void old_leaf(int cat,int cy);
static int st_para(int x,int y,int w,const char* t,u32 c){ int cols=w/8; if(cols<20)cols=20; if(cols>110)cols=110; char ln[112];
    while(*t){ int n=0, cut=0; while(t[n]&&n<cols){ if(t[n]==' ')cut=n; n++; } if(t[n]&&cut>0)n=cut; for(int k=0;k<n;k++)ln[k]=t[k]; ln[n]=0; pdstr(x,y,ln,c,1); y+=18; t+=n; while(*t==' ')t++; } return y; }
static const char* NV_NOTE_MODE="The screen mode is still the one set at boot. There's no NVIDIA mode setting, no tear-free page flips and no second monitor yet. That's stage 6, the display engine.";
static const char* NV_NOTE_MEM="The desktop images sit in normal RAM, not on the card. The GPU reads and writes them over the PCIe bus. That works and is correct, but it's slower than keeping them in the card's own memory, which a full driver would do. It's the next big speed step.";
static int g_scr_n=-2; static char g_scr_t[4][96]; static u64 g_scr_at=0;
int isin(int deg); int icos(int deg);
static void st_chip(int x,int y,const char* t,u32 bg,u32 fg){ if(!g_gm_ready)gmetrics(); int w=pw_str(t,1)+24; fill_round(x-w,y,w,24,12,15,bg); pdstr_ctr(x-w/2,y+5,t,fg,1); }
static u64 g_upd_seen_pg=0; static int st_ybot=0;
static void st_upd_status(const char** title,const char** sub,char* subbuf,int* tone,int* btn,const char** bl){
    *btn=0; *bl=0; *tone=0; *sub=subbuf; subbuf[0]=0; char* q=subbuf;
    if(g_upd.trial){ *title="Finishing the update"; *sub="Checking the new version. It will be kept after one minute."; *tone=2; return; }
    if(g_upd.state==UPS_FAIL&&!(g_upd.newv[0]&&g_upd.cur[0]&&upd_vnum(g_upd.newv)==upd_vnum(g_upd.cur))){ *title="The update did not start"; *sub=g_upd.ntext[0]?g_upd.ntext:"NoovexOS went back to the previous version."; *tone=3; *btn=402; *bl="Try again"; return; }
    if(g_upd.state==UPS_OLD&&!g_upd.has_new){ *title="The last update was interrupted"; *sub=g_upd.ntext[0]?g_upd.ntext:"NoovexOS started the previous version. You can try the update again."; *tone=2; *btn=401; *bl="Try again"; return; }
    if(g_upd.state==UPS_OLD&&g_upd.newv[0]&&upd_vnum(g_upd.newv)!=upd_vnum(g_upd.cur)){ *title="Using the previous version"; if(g_upd.ntext[0])q=scopy(q,g_upd.ntext); else { q=scopy(q,"NoovexOS "); q=scopy(q,g_upd.newv); q=scopy(q," is still installed and can be used again."); } *q=0; *tone=2; *btn=402; *bl="Use it again"; return; }
    if(g_upd.busy||g_upd.want_check){ *title="Checking for updates..."; *sub="Asking noovexos.site for the newest version"; *tone=1; return; }
    if(g_upd.checked==-1){ *title="Couldn't check for updates"; *sub=g_upd.why[0]?g_upd.why:"The update server did not answer."; *tone=3; *btn=400; *bl="Try again"; return; }
    if(g_upd.checked==1&&g_upd.avail){ *title="Update available"; q=scopy(q,"NoovexOS "); q=scopy(q,g_upd.latest); q=scopy(q,", "); q=udec(q,(u32)(g_upd.iso_bytes/1048576)); q=scopy(q," MB. The current version is kept so you can go back."); *q=0; *tone=1; *btn=401; *bl="Download and install";
        if(!g_upd.installed&&!g_upd.usb&&g_upd.probed){ *sub=g_upd.why[0]?g_upd.why:"This copy runs from the ISO or a CD and cannot update itself. Write the new ISO to a USB stick or install NoovexOS on a disk."; *btn=0; } return; }
    if(g_upd.checked==1){ *title="You're up to date"; extern volatile u64 g_ticks; u64 m=(g_ticks-g_upd.checked_at)/6000; q=scopy(q,"NoovexOS "); q=scopy(q,g_upd.cur); q=scopy(q," - last checked "); if(m==0)q=scopy(q,"just now"); else { q=udec(q,(u32)m); q=scopy(q,m==1?" minute ago":" minutes ago"); } *q=0; *tone=0; *btn=400; *bl="Check for updates"; return; }
    *title="NoovexOS Update"; q=scopy(q,"NoovexOS "); q=scopy(q,(char*)g_buildsig.version); *q=0; *btn=400; *bl="Check for updates"; *tone=0; }
static void st_upd_icon(int cx,int cy,int tone){ u32 c=tone==0?rgb(46,170,90):tone==1?ACC:tone==2?rgb(230,150,40):rgb(210,70,70); u32 w=rgb(255,255,255);
    fill_circle(cx,cy,26,c);
    if(tone==0){ for(int i=0;i<4;i++)fill_rect(cx-11+i,cy-1+i,3,3,w); for(int i=0;i<10;i++)fill_rect(cx-7+i,cy+2-i,3,3,w); }
    else if(tone==3){ fill_rect(cx-2,cy-13,5,16,w); fill_rect(cx-2,cy+7,5,5,w); }
    else { extern volatile u64 g_ticks; int sp=(g_upd.busy||g_upd.want_check||g_upd.trial)?(int)(g_ticks%100)*36/10:0; st_ring(cx,cy,15,4,w,c); for(int k=0;k<2;k++){ int a=sp+k*180; int ex=cx+icos(a)*15/1000, ey=cy+isin(a)*15/1000; fill_circle(ex,ey,5,c); int ax=cx+icos(a+28)*15/1000, ay=cy+isin(a+28)*15/1000; fill_circle(ax,ay,4,w); } } }
static int st_upd_hero(int cy,int full){
    const char* t; const char* sub; char sb[200]; int tone, btn; const char* bl; st_upd_status(&t,&sub,sb,&tone,&btn,&bl);
    int h=112; int y=st_card(cy,h,0); int X1=st_x0+st_w0-24;
    st_upd_icon(st_x0+56,y+h/2,tone);
    int bw=bl?pw_str(bl,1)+44:0; if(bw&&bw<150)bw=150;
    int tx=st_x0+100, tw=st_w0-100-(bw?bw+40:24);
    int tsz=1; (void)full;
    st_bold(tx,y+30,t,ST_FG);
    { int cols=tw/7; if(cols<24)cols=24; char ln[2][120]; int nl=0; const char* r=sub; while(*r&&nl<2){ int n=0,cut=0; while(r[n]&&n<cols&&n<118){ if(r[n]==' ')cut=n; n++; } if(r[n]&&cut>0)n=cut; for(int k=0;k<n;k++)ln[nl][k]=r[k]; ln[nl][n]=0; nl++; r+=n; while(*r==' ')r++; }
      for(int k=0;k<nl;k++)pdstr(tx,y+(tsz==2?62:52)+k*19,ln[k],rgb(110,118,138),1); }
    if(bl)st_btn(X1-bw,y+h/2-17,bw,bl,btn,btn==400&&tone==0?0:1);
    return y+h+20; }
static int st_sys_hero(int cy){
    int h=112; int y=st_card(cy,h,0); int X1=st_x0+st_w0-24; char b[96],*q;
    fill_round(st_x0+22,y+24,84,56,8,15,rgb(40,46,66)); fill_round(st_x0+26,y+28,76,46,5,15,mix(ACC,rgb(20,30,60),90)); fill_round(st_x0+30,y+32,30,10,4,15,mix(ACC,rgb(255,255,255),120)); fill_rect(st_x0+58,y+80,12,6,rgb(40,46,66)); fill_round(st_x0+46,y+86,36,5,2,15,rgb(40,46,66));
    st_bold(st_x0+126,y+30,"NoovexOS 64",ST_FG);
    q=b; q=scopy(q,"Version "); q=scopy(q,(char*)g_buildsig.version); q=scopy(q,"  -  "); q=udec(q,g_ram_mb); q=scopy(q," MB memory"); *q=0; pdstr(st_x0+126,y+58,b,rgb(110,118,138),1);
    { int n=0; while(g_brand[n]&&n<60){ b[n]=g_brand[n]; n++; } b[n]=0; while(n&&b[n-1]==' ')b[--n]=0; const char* bb=b; while(*bb==' ')bb++; pdstr(st_x0+126,y+78,*bb?bb:"x86-64 processor",rgb(110,118,138),1); }
    { const char* ct; u32 cb, cf; if(g_upd.checked==1&&g_upd.avail){ ct="Update available"; cb=mix(ACC,rgb(255,255,255),210); cf=ACC2; } else if(g_upd.checked==1){ ct="Up to date"; cb=rgb(226,246,233); cf=rgb(40,140,80); } else { ct="NoovexOS Update"; cb=rgb(238,241,246); cf=rgb(90,98,118); }
      int w=pw_str(ct,1)+24; st_chip(X1,y+h/2-12,ct,cb,cf); st_hit(X1-w,y+h/2-12,w,24,1000+P_UPDATE); }
    return y+h+20; }
static void st_upd_page(int cy){
    int X1=st_x0+st_w0-24; char b[200],*q;
    if(g_upd_seen_pg==0){ g_upd_seen_pg=1; if(!g_upd.busy&&g_upd.checked==0)g_upd.want_check=1; }
    cy=st_upd_hero(cy,1);
    int room=st_ybot-cy-(26+192+20)-48;
    if(g_upd.checked==1&&g_upd.nnotes>0&&room>26+28+19){ int cols=(st_w0-80)/7; if(cols<30)cols=30; int maxr=(room-26-28)/19, rows=0;
        for(int i=0;i<g_upd.nnotes;i++){ int l=slen(g_upd.notes[i]); rows+=1+l/cols; } if(rows>maxr)rows=maxr;
        q=b; q=scopy(q,g_upd.avail?"What's new in ":"What's in "); q=scopy(q,g_upd.latest); *q=0; int h=rows*19+28; int y=st_card(cy,h,b); int yy=y+14, used=0;
        for(int i=0;i<g_upd.nnotes&&used<rows;i++){ fill_circle(st_x0+30,yy+7,3,ACC); const char* r=g_upd.notes[i];
            while(*r&&used<rows){ int n=0,cut=0; char ln[124]; while(r[n]&&n<cols&&n<120){ if(r[n]==' ')cut=n; n++; } if(r[n]&&cut>0)n=cut; for(int k=0;k<n;k++)ln[k]=r[k]; ln[n]=0; pdstr(st_x0+44,yy,ln,INK,1); yy+=19; used++; r+=n; while(*r==' ')r++; } }
        cy=y+h+20; }
    int y=st_card(cy,64*3,"Safety");
    int onold=(g_upd.state==UPS_OLD||g_upd.state==UPS_FAIL)&&g_upd.newv[0]&&upd_vnum(g_upd.newv)!=upd_vnum(g_upd.cur);
    q=b; if(onold&&g_upd.has_new){ q=scopy(q,"You are using it now. NoovexOS "); q=scopy(q,g_upd.newv); q=scopy(q," is also on the disk"); } else if(onold)q=scopy(q,"You are using it now");
    else if(g_upd.has_old&&g_upd.oldv[0]){ q=scopy(q,"NoovexOS "); q=scopy(q,g_upd.oldv); q=scopy(q,g_upd.usb?" is kept on the USB stick":" is kept on the boot partition"); } else q=scopy(q,(g_upd.installed||g_upd.usb)?"Kept automatically when you update":"Available on installed systems and USB sticks"); *q=0;
    st_rowtext(y,"Previous version",b);
    if(g_upd.has_old&&(g_upd.installed||g_upd.usb)&&!onold)st_btn(X1-120,y+16,120,"Go back",403,0);
    st_sep(y+64); st_rowtext(y+64,"Automatic fallback","If a new version does not start twice in a row, the previous one starts by itself");
    st_chip(X1,y+84,"On",rgb(226,246,233),rgb(40,140,80)); st_sep(y+128);
    st_rowtext(y+128,"Checked before it is used","The download and the new version are checked with SHA-256 before anything changes");
    st_chip(X1,y+148,"On",rgb(226,246,233),rgb(40,140,80)); cy=y+192+16;
    { char u[200]; upd_server(u,120); q=b; q=scopy(q,"NoovexOS "); q=scopy(q,(char*)g_buildsig.version); q=scopy(q,g_upd.installed?" (installed)":g_upd.usb?" (USB stick)":g_upd.probed?" (ISO)":""); q=scopy(q,"   -   update server: "); q=scopy(q,u); *q=0;
      int mx2=(st_w0-12)/7; if(mx2<20)mx2=20; if(slen(b)>mx2)b[mx2]=0; pdstr(st_x0+6,cy,b,rgb(128,136,154),1); } }

int g_focus_newmode=0;
static int g_ftab=0, g_focsel=0, g_fedit=0, g_fnew_icon=4, g_fnew_col=0, g_fbuf_n=0; static char g_fbuf[96];
static const u32 FCOL[8]={0x6C5CE7,0x3B5BDB,0x2F9E44,0xE8590C,0xD6336C,0x1098AD,0xF08C00,0x868E96};
static void fe_begin(int k,const char* init){ g_fedit=k; g_fbuf_n=0; if(init)while(init[g_fbuf_n]&&g_fbuf_n<94){ g_fbuf[g_fbuf_n]=init[g_fbuf_n]; g_fbuf_n++; } g_fbuf[g_fbuf_n]=0; }
static void fe_field(int x,int y,int w,const char* ph,int k,int id){ int on=g_fedit==k; if(ST_CL){ ui_bevel(x,y,w,26,1); fill_rect(x+2,y+2,w-4,22,rgb(255,255,255)); } else { fill_round(x,y,w,30,8,15,on?ACC:rgb(222,222,226)); fill_round(x+1,y+1,w-2,28,7,15,rgb(255,255,255)); }
    const char* t=on?g_fbuf:ph; int cols=(w-20)/8; int L=0; while(t[L])L++; int st=L>cols?L-cols:0; char sh[100]; int n=0; for(int i=st;i<L&&n<98;i++)sh[n++]=t[i]; sh[n]=0;
    pdstr(x+10,y+8,sh,on?ST_FG:ST_DIM,1); if(on&&((g_ticks/50)&1))fill_rect(x+10+pw_str(sh,1)+1,y+6,2,16,ST_FG); st_hit(x,y,w,30,id); }
static void fe_commit(void){ focus_mode* f=focus_get(g_focsel);
    if(g_fedit==1){ if(g_fbuf_n){ int k=focus_create(g_fbuf,g_fnew_icon,FCOL[g_fnew_col]); if(k>=0){ g_focsel=k; g_ftab=1; g_focus_newmode=0; } else { toast(k==-2?"A focus with that name already exists":"No room for more focuses"); return; } } }
    else if(g_fedit==2&&f){ int i=0; for(;i<g_fbuf_n&&i<95;i++)f->allow[i]=g_fbuf[i]; f->allow[i]=0; focus_save(g_focsel); }
    else if(g_fedit==3&&f&&g_fbuf_n){ int i=0; for(;i<g_fbuf_n&&i<23;i++)f->name[i]=g_fbuf[i]; f->name[i]=0; focus_save(g_focsel); }
    g_fedit=0; }
static void fr_label(int y,const char* t){ pdstr(st_x0+20,y+16,t,ST_FG,1); }
static void fr_time(char* o,int m){ char* q=o; q=u2(q,m/60); *q++=':'; q=u2(q,m%60); *q=0; }
static void st_focus_page(int cy){
    int X1=st_x0+st_w0-24; int n=focus_count(); if(g_focsel>=n)g_focsel=0; focus_mode* f=focus_get(g_focsel); if(g_focus_newmode){ g_focus_newmode=0; g_ftab=0; fe_begin(1,""); }
    static const char* TB[3]={"Focuses","Settings","Turn on automatically"}; st_seg(st_x0,cy,TB,3,g_ftab,1350); cy+=44;
    int bot=WN[W_SETTINGS].y+WN[W_SETTINGS].h-12; char b[80],*q;
    if(g_ftab==0){ int rows=n; int maxr=(bot-cy-170)/44; if(maxr<3)maxr=3; if(rows>maxr)rows=maxr;
        int y=st_card(cy,rows*44,"Choose a focus");
        for(int i=0;i<rows;i++){ focus_mode* g=focus_get(i); int ry=y+i*44; if(i)st_sep(ry); fill_circle(st_x0+30,ry+22,12,g->color); focus_glyph(st_x0+30,ry+22,g->icon,rgb(255,255,255));
            pdstr(st_x0+52,ry+15,g->name,ST_FG,1); if(i==g_focus_cur){ q=b; q=scopy(q,"On"); if(focus_why()==1)q=scopy(q," (schedule)"); else if(focus_why()==2)q=scopy(q," (app)"); else if(focus_why()==3)q=scopy(q," (sign-in)"); else if(focus_why()==4)q=scopy(q," (low battery)"); *q=0; pdstr(st_x0+52+pw_str(g->name,1)+16,ry+15,b,ACC,1); }
            st_btn(X1-210,ry+5,100,i==g_focus_cur?"Turn off":"Turn on",1300+i,i==g_focus_cur?0:1); st_btn(X1-100,ry+5,100,"Edit",1320+i,0); }
        cy=y+rows*44+20; y=st_card(cy,104,"Create focus");
        fe_field(st_x0+20,y+10,st_w0-40-230,"Name, for example Reading",1,1343);
        { int bx=X1-216; st_btn(bx,y+8,70,"Icon",1341,0); fill_circle(bx+84,y+25,12,FCOL[g_fnew_col]); focus_glyph(bx+84,y+25,g_fnew_icon,rgb(255,255,255)); st_btn(bx+104,y+8,64,"Colour",1342,0); fill_circle(bx+184,y+25,9,FCOL[g_fnew_col]); }
        pdstr(st_x0+20,y+56,"New focuses hide pop-ups. Change the rest under Settings.",ST_DIM,1); st_btn(X1-110,y+52,110,"Create",1340,1); return; }
    if(!f)return;
    { q=b; q=scopy(q,g_ftab==1?"Settings for ":"Turn on "); q=scopy(q,f->name); *q=0; }
    if(g_ftab==1){ int R=44; int y=st_card(cy,R*4,b);
        fr_label(y,"Hide pop-up notifications"); st_toggle(X1-44,y+10,f->hide_popups,1200); st_sep(y+R);
        fr_label(y+R,"Still allow (apps, comma separated)"); fe_field(st_x0+290,y+R+7,st_w0-310,f->allow[0]?f->allow:"No apps",2,1240); st_sep(y+2*R);
        fr_label(y+2*R,"Mute sounds"); st_toggle(X1-44,y+2*R+10,f->mute,1201); st_sep(y+3*R);
        fr_label(y+3*R,"Name"); fe_field(st_x0+290,y+3*R+7,st_w0-310,f->name,3,1251);
        cy=y+R*4+14; y=st_card(cy,R*5,"While it is on");
        { static const char* K3[3]={"Keep","On","Off"}; static const char* KB[4]={"Keep","30%","60%","100%"};
          fr_label(y,"Night light"); { int w=st_segw(K3,3); st_seg(X1-w,y+7,K3,3,f->nl<0?0:f->nl?1:2,1210); } st_sep(y+R);
          fr_label(y+R,"Brightness"); { int w=st_segw(KB,4); st_seg(X1-w,y+R+7,KB,4,f->bright<0?0:f->bright<=30?1:f->bright<=60?2:3,1213); } st_sep(y+2*R);
          fr_label(y+2*R,"Battery saver"); { int w=st_segw(K3,3); st_seg(X1-w,y+2*R+7,K3,3,f->bsave<0?0:f->bsave?1:2,1217); } st_sep(y+3*R);
          fr_label(y+3*R,"Theme"); st_btn(X1-150,y+3*R+5,150,f->theme[0]?f->theme:"Keep current",1241,0); st_sep(y+4*R);
          fr_label(y+4*R,"Lock after 1 minute"); st_toggle(st_x0+230,y+4*R+10,f->lock1,1202); pdstr(st_x0+310,y+4*R+16,"Hide games",ST_FG,1); st_toggle(st_x0+410,y+4*R+10,f->hide_games,1203); }
        cy=y+R*5+14; if(cy+R+30<bot){ y=st_card(cy,R,"Show it"); fr_label(y,"On the taskbar"); st_toggle(st_x0+150,y+10,f->show_tb,1204); pdstr(st_x0+230,y+16,"On the lock screen",ST_FG,1); st_toggle(st_x0+380,y+10,f->show_lock,1205);
            if(!f->builtin)st_btn(X1-90,y+5,90,"Delete",1250,2); } return; }
    { int R=48; int y=st_card(cy,R*2,b); char t1[8],t2[8]; fr_time(t1,f->sch_from); fr_time(t2,f->sch_to);
      fr_label(y,"On a schedule"); st_toggle(st_x0+170,y+12,f->sch_on,1260);
      { int x=X1-300; pdstr(x,y+16,"From",ST_DIM,1); st_btn(x+44,y+7,30,"-",1261,0); pdstr(x+82,y+16,t1,ST_FG,1); st_btn(x+128,y+7,30,"+",1262,0); pdstr(x+170,y+16,"to",ST_DIM,1); st_btn(x+192,y+7,30,"-",1263,0); pdstr(x+228,y+16,t2,ST_FG,1); st_btn(x+270,y+7,30,"+",1264,0); }
      st_sep(y+R); fr_label(y+R,"Days");
      { static const char* DN[7]={"Mon","Tue","Wed","Thu","Fri","Sat","Sun"}; int x=X1-7*54; for(int d=0;d<7;d++){ int on=(f->sch_days>>d)&1; st_btn(x+d*54,y+R+7,50,DN[d],1270+d,on?1:0); if(on)fill_rect(x+d*54+8,y+R+39,34,3,ST_CL?rgb(0,0,128):ACC); } }
      cy=y+R*2+14; y=st_card(cy,R*3,"Other triggers");
      fr_label(y,"When this app starts"); st_btn(X1-170,y+7,170,focus_app_preset_name(f->app_mask),1265,0); st_sep(y+R);
      fr_label(y+R,"When I sign in"); st_toggle(X1-44,y+R+12,f->on_login,1266); st_sep(y+2*R);
      fr_label(y+2*R,"When the battery drops to 20%"); st_toggle(X1-44,y+2*R+12,f->low_batt,1267);
      cy=y+R*3+10; if(cy+40<bot)pdstr(st_x0+4,cy+6,"Schedules and app starts turn the focus off again when they end.",ST_DIM,1); } }
static int st_focus_click(int id){
    int n=focus_count(); focus_mode* f=focus_get(g_focsel);
    if(g_fedit&&!((id==1240&&g_fedit==2)||(id==1251&&g_fedit==3)||(id==1343&&g_fedit==1)))fe_commit();
    if(id>=1350&&id<1353){ g_ftab=id-1350; g_focus_newmode=0; return 1; }
    if(id>=1300&&id<1300+n){ int i=id-1300; focus_set(i==g_focus_cur?-1:i,0); return 1; }
    if(id>=1320&&id<1320+n){ g_focsel=id-1320; g_ftab=1; return 1; }
    if(id==1343){ if(g_fedit!=1)fe_begin(1,""); return 1; }
    if(id==1341){ g_fnew_icon=(g_fnew_icon+1)%10; return 1; } if(id==1342){ g_fnew_col=(g_fnew_col+1)%8; return 1; }
    if(id==1340){ if(g_fedit==1)fe_commit(); else fe_begin(1,""); return 1; }
    if(!f)return 1;
    int ap=(id==1200||id==1201||id==1202||id==1203||(id>=1210&&id<1220)||id==1241); int on=ap&&g_focus_cur==g_focsel, why=focus_why(); if(on)focus_set(-1,why==0?3:why);
    if(id==1200)f->hide_popups^=1; else if(id==1201)f->mute^=1; else if(id==1202)f->lock1^=1; else if(id==1203)f->hide_games^=1; else if(id==1204)f->show_tb^=1; else if(id==1205)f->show_lock^=1;
    else if(id==1240){ if(g_fedit!=2)fe_begin(2,f->allow); } else if(id==1251){ if(g_fedit!=3)fe_begin(3,f->name); }
    else if(id>=1210&&id<1213)f->nl=id==1210?-1:id==1211?1:0; else if(id>=1213&&id<1217){ static const int BV[4]={-1,30,60,100}; f->bright=BV[id-1213]; } else if(id>=1217&&id<1220)f->bsave=id==1217?-1:id==1218?1:0;
    else if(id==1241){ const char* t=focus_theme_next(f->theme); int i=0; for(;t[i]&&i<15;i++)f->theme[i]=t[i]; f->theme[i]=0; }
    else if(id==1250){ if(focus_delete(g_focsel)==0){ g_focsel=0; g_ftab=0; } return 1; }
    else if(id==1260)f->sch_on^=1; else if(id==1261)f->sch_from=(f->sch_from+1440-30)%1440; else if(id==1262)f->sch_from=(f->sch_from+30)%1440; else if(id==1263)f->sch_to=(f->sch_to+1440-30)%1440; else if(id==1264)f->sch_to=(f->sch_to+30)%1440;
    else if(id>=1270&&id<1277)f->sch_days^=1<<(id-1270); else if(id==1265)f->app_mask=focus_app_preset_next(f->app_mask); else if(id==1266)f->on_login^=1; else if(id==1267)f->low_batt^=1;
    if(id!=1240&&id!=1251)focus_save(g_focsel); if(on)focus_set(g_focsel,why); return 1; }
int settings_focus_key(int c){ if(g_stpg!=P_FOCUS||!g_fedit)return 0;
    if(c==27){ g_fedit=0; g_focus_newmode=0; return 1; } if(c=='\n'){ fe_commit(); return 1; }
    if(c=='\b'){ if(g_fbuf_n>0)g_fbuf[--g_fbuf_n]=0; return 1; }
    if(c>=32&&c<127){ int cap=g_fedit==3?22:g_fedit==1?22:94; if(g_fbuf_n<cap){ g_fbuf[g_fbuf_n++]=(char)c; g_fbuf[g_fbuf_n]=0; } return 1; } return 1; }
static void st_page(int p,int cy){
    int X1=st_x0+st_w0-24; char b[96],*q;
    { static int lp=-1; if(p!=lp){ if(lp==P_WIFI&&g_wifi_entry&&g_wifi_sel!=-2){ extern void wifi_entry_cancel(void); if(!g_wifipop)wifi_entry_cancel(); } if(lp==P_FOCUS&&g_fedit){ if(g_fedit==1)g_fedit=0; else fe_commit(); } lp=p; } }
    if(p!=P_UPDATE)g_upd_seen_pg=0;
    if(p<9){ if(p==0)cy=st_sys_hero(cy); if(p==8)cy=st_upd_hero(cy,0); int n; const stile* t=cat_tiles(p,&n); st_tiles(&cy,t,n); return; }
    if(p==P_FOCUS){ st_focus_page(cy); return; }
    switch(p){
    case P_DISPLAY: { stile g={P_GRAPHICS,3,"Graphics","GPU drawing, NVIDIA, effects, screen copies"}; st_tiles(&cy,&g,1); cy+=10; old_leaf(3,cy); } break;
    case P_SCREENS: { extern volatile u64 g_ticks; if(g_scr_n==-2||g_ticks-g_scr_at>200){ g_scr_at=g_ticks; g_scr_n=nvm_screens(g_scr_t,4); }
        const char* a[3]={"Screen mode","Refresh rate","Set by"}; char v[3][80]; q=v[0]; q=udec(q,W); q=scopy(q," x "); q=udec(q,H); *q=0; q=v[1]; q=udec(q,(u32)(g_refresh_hz>0?g_refresh_hz:60)); q=scopy(q," Hz"); *q=0; q=v[2]; q=scopy(q,"the firmware at boot (UEFI GOP or VESA)"); *q=0; st_infocard(&cy,"Current screen",a,v,3);
        { const nvgsp_mon_t* ml=0; int mn=nvgsp_mon_get(&ml,0,0,0); nvgsp_info_t ni; nvgsp_info(&ni);
          if(ni.state>=7){ int rows=0; for(int i=0;i<mn;i++)rows++; if(rows<1)rows=1; if(rows>4)rows=4; int y=st_card(cy,rows*38+8+44,"Monitors (read through GSP-RM, like Linux)"); int r=0;
            if(mn<=0){ pdstr(st_x0+24,y+15,mn<0?"Not read yet":"No outputs reported",INK,1); r=1; }
            for(int i=0;i<mn&&r<4;i++,r++){ const nvgsp_mon_t* m=&ml[i]; int ry=y+4+r*38; if(r)fill_rect(st_x0+20,ry,st_w0-40,1,rgb(238,240,245)); char t[96],*p=t;
                p=scopy(p,nvgsp_mon_sig(m)); p=scopy(p,": "); if(!m->connected)p=scopy(p,"nothing connected"); else if(m->edid_ok){ p=scopy(p,m->name[0]?m->name:"monitor"); p=scopy(p," ("); p=scopy(p,m->maker); p=scopy(p,")"); if(m->nmodes){ p=scopy(p,"  native "); p=udec(p,m->mw[0]); *p++='x'; p=udec(p,m->mh[0]); p=scopy(p," @ "); p=udec(p,m->mhz[0]); p=scopy(p," Hz"); } } else p=scopy(p,"connected, EDID not readable yet");
                *p=0; pdstr(st_x0+24,ry+11,t,m->connected?INK:rgb(128,136,154),1); }
            int by=y+4+r*38+4; st_btn(X1-130,by,130,"Read again",335,1); cy=by+40+20; } }
        if(g_scr_n>=0){ int n=g_scr_n?g_scr_n:1; int y=st_card(cy,n*38+8,"Current mode on the card (display engine, read only)"); for(int r=0;r<n;r++){ int ry=y+4+r*38; if(r)fill_rect(st_x0+20,ry,st_w0-40,1,rgb(238,240,245)); pdstr(st_x0+24,ry+11,g_scr_n?g_scr_t[r]:"No head is scanning out",INK,1); } cy=y+n*38+8+20; }
        int y=st_card(cy,96,"Stage 6"); st_para(st_x0+24,y+14,st_w0-48,NV_NOTE_MODE,rgb(110,118,136)); cy=y+96+20; } break;
    case P_NVIDIA: { nvgsp_info_t ni; nvgsp_info(&ni); extern int g_nvd_auto; char d[96];
        int y=st_card(cy,64*4,"NVIDIA GeForce");
        if(ni.state<7){ q=d; q=scopy(q,ni.state?ni.state_name:"Not started"); q=scopy(q," - start takes about 10 s"); *q=0; st_rowtext(y,"NVIDIA driver (GSP-RM)",d); st_btn(X1-130,y+16,130,"Start now",334,1); }
        else if(ni.off){ q=d; q=scopy(q,"Off "); q=udec(q,(u32)ni.off_age_s); q=scopy(q," s ago: "); { int n=0; const char* w=ni.off_why; while(w[n]&&n<60){ *q++=w[n]; n++; } } *q=0; st_rowtext(y,"NVIDIA drawing",d); st_toggle(X1-44,y+20,0,333); }
        else { q=d; q=scopy(q,"On - "); q=scopy(q,ni.state_name); *q=0; st_rowtext(y,"NVIDIA drawing",d); st_toggle(X1-44,y+20,1,333); }
        st_sep(y+64); st_rowtext(y+64,"Start by itself after boot","About 3 s after the desktop appears (Turing cards)"); st_toggle(X1-44,y+84,g_nvd_auto,330); st_sep(y+128);
        q=d; if(ni.present_ok){ q=udec(q,ni.frames); q=scopy(q," frames, "); q=udec(q,ni.readback_bad); q=scopy(q," bad read-backs, 2D engine "); q=scopy(q,ni.twod?"working":"off"); } else q=scopy(q,"Not set up yet"); *q=0;
        st_rowtext(y+128,"Screen through the copy engine",d); if(ni.present_ok&&!ni.off)st_toggle(X1-44,y+148,ni.present,331); st_sep(y+192);
        q=d; if(ni.compute){ q=scopy(q,"Compute shaders work, last batch "); q=udec(q,ni.cp_last_us); q=scopy(q," us"); } else if(ni.cp_why&&ni.cp_why[0]){ int n=0; while(ni.cp_why[n]&&n<70){ *q++=ni.cp_why[n]; n++; } } else q=scopy(q,"Compute shaders not ready"); *q=0;
        st_rowtext(y+192,"GPU draws the desktop",d); if(ni.compute)st_toggle(X1-44,y+212,ni.drawing,332); cy=y+256+24;
        y=st_card(cy,124,"Where the desktop images live"); int yy=st_para(st_x0+24,y+12,st_w0-48,NV_NOTE_MEM,rgb(110,118,136));
        q=d; q=scopy(q,"Already on the card: "); q=udec(q,ni.vram_surfaces); q=scopy(q," GPU-only work buffers ("); q=udec(q,(u32)(ni.vram_surface_bytes>>20)); q=scopy(q," MB)"); *q=0; pdstr(st_x0+24,yy+6,d,INK,1); } break;
    case P_GRAPHICS: { extern int g_gfx, g_gfx_fx, g_gfx_srec, g_wall_fx, g_wall_light;
        int y=st_card(cy,64*5,"GPU"); { extern int g_nvd_on; st_rowtext(y,"GPU drawing",g_gfx==1&&g_nvd_on?"The NVIDIA GPU draws the desktop (compute shaders)":g_gfx==1?"The Intel GPU draws the desktop":g_gfx==2?"Emulated on the CPU (test mode)":"The CPU draws the desktop"); } st_toggle(X1-44,y+20,g_gfx!=0,300); st_sep(y+64);
        st_rowtext(y+64,"Visual effects","Shadows, rounded corners, glass, animations"); st_toggle(X1-44,y+84,g_gfx_fx,301); st_sep(y+128);
        st_rowtext(y+128,"Partial redraws","Only redraw the parts of the screen that changed"); st_toggle(X1-44,y+148,g_gfx_srec,302); st_sep(y+192);
        st_rowtext(y+192,"Wallpaper effects","Smooth colours, glow, depth, vignette, light"); st_toggle(X1-44,y+212,g_wall_fx,303); st_sep(y+256);
        st_rowtext(y+256,"Drifting light","A soft light moves across the wallpaper"); st_toggle(X1-44,y+276,g_wall_light,304); cy=y+320+24;
        { extern u16 g_nv_dev; stile a[2]={{P_GFXADV,3,"Advanced graphics","GPU details, screen copy, frame check"},{P_NVIDIA,3,"NVIDIA graphics","Driver status, switches, where the desktop images live"}}; st_tiles(&cy,a,g_nv_dev?2:1); } } break;
    case P_GFXADV: { extern int igpu_info(const char**,u32*,u32*,u32*,u32*,u32*,u32*,u32*); extern int g_pres_eng; extern char g_fc_msg[]; extern u32 g_gfx_frame_us;
        const char* nm; u32 did=0,ss=0,eu=0,mhz=0,mxz=0,stl=0,tf=0; int hg=igpu_info(&nm,&did,&ss,&eu,&mhz,&mxz,&stl,&tf);
        const char* a[5]={"Graphics","Clock","Execution units","Graphics memory","Last frame"}; char v[5][80];
        q=v[0]; if(hg)q=scopy(q,nm); else { extern int gpu_list(void*,int); extern const char* gpu_name(u16,u16); struct { u16 ven, dev; u8 bus, slot; const char* vendor; const char* name; const char* driver; int accel; } gl[2]; if(gpu_list(gl,2))q=scopy(q,gl[0].name); else q=scopy(q,"No graphics card found"); } *q=0; q=v[1]; q=udec(q,mhz); q=scopy(q," of "); q=udec(q,mxz); q=scopy(q," MHz"); *q=0;
        q=v[2]; q=udec(q,ss*eu); *q=0; q=v[3]; q=udec(q,stl); q=scopy(q," MB"); *q=0; q=v[4]; q=udec(q,g_gfx_frame_us/1000); *q++='.'; q=udec(q,(g_gfx_frame_us/100)%10); q=scopy(q," ms"); *q=0;
        st_infocard(&cy,"GPU",a,v,5);
        static const char* PE[2]={"Blitter","Shader"}; int y=st_card(cy,128,"Screen"); st_rowtext(y,"Screen copy engine","Which GPU engine copies the picture to the screen"); { int w=st_segw(PE,2); st_seg(X1-w,y+17,PE,2,g_pres_eng,310); } st_sep(y+64);
        st_rowtext(y+64,"Tear-free","Double buffering, no tearing"); st_toggle(X1-44,y+84,(int)tf,312); cy=y+128+24;
        y=st_card(cy,64,"Frame check"); { char m[72]; int n=0, mxc=(st_w0-220)/8; if(mxc>70)mxc=70; while(g_fc_msg[n]&&n<mxc){ m[n]=g_fc_msg[n]; n++; } m[n]=0; pdstr(st_x0+24,y+24,m[0]?m:"Compare one GPU frame with the CPU",rgb(110,118,136),1); } st_btn(X1-130,y+15,130,"Run check",313,1); } break;
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
    case P_SERVICES: { extern int svc_count(void); extern int svc_info(int,const char**,const char**,const char**,int*,int*); int n=svc_count();
        int y=st_card(cy,64*n,"Background services"); int ry=y;
        for(int i=0;i<n;i++){ const char *nm,*ds,*stt; int on,req; svc_info(i,&nm,&ds,&stt,&on,&req); char d2[160],*q=d2; q=scopy(q,stt); q=scopy(q," - "); q=scopy(q,ds); *q=0; st_rowtext(ry,nm,d2);
            if(req){ fill_round(X1-104,ry+20,104,24,12,15,rgb(226,246,233)); pdstr_ctr(X1-52,ry+25,"Always on",rgb(40,140,80),1); } else st_toggle(X1-44,ry+20,on,400+i);
            ry+=64; if(i<n-1)st_sep(ry); }
        cy=y+64*n+28; y=st_card(cy,64,"Clock"); st_rowtext(y,"Sync the clock now","Asks an internet time server for the exact time"); st_btn(X1-110,y+16,110,"Sync now",420,1); } break;
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
        q=v[4]; for(int k=0;k<6;k++){ q=hx(q,my_mac[k],2); if(k<5)*q++=':'; } *q=0; st_infocard(&cy,"Addresses",a,v,5);
        if(g_wm_state==WM_CONNECTED){ extern int g_wifi_rekeys; extern int g_wpa_akm; const char* wa[7]={"Network","Access point","Channel","Signal","Speed","Security","Gateway and DNS"}; char wv[7][80]; cy+=10;
            q=wv[0]; q=scopy(q,g_wifi_conn); *q=0; q=wv[1]; for(int k=0;k<6;k++){ q=hx(q,g_wm_bssid[k],2); if(k<5)*q++=':'; } *q=0;
            q=wv[2]; q=udec(q,g_wm_ch); q=scopy(q,g_wm_ch>14?"  (5 GHz)":"  (2.4 GHz)"); *q=0;
            q=wv[3]; if(g_wifi_rssi>-127){ *q++='-'; q=udec(q,(u32)(-g_wifi_rssi)); q=scopy(q," dBm  ("); q=udec(q,(u32)wifi_signal_bars(g_wifi_rssi)); q=scopy(q," of 4 bars)"); } else q=scopy(q,"not measured yet"); *q=0;
            q=wv[4]; q=scopy(q,"up to "); q=udec(q,(u32)wifi_rate_mbps()); q=scopy(q," Mbit/s"); q=scopy(q,g_wifi_ra?(g_wifi_assoc_ht?" (802.11n, auto rate)":" (auto rate)"):" (safe fixed rate)"); *q=0;
            q=wv[5]; q=scopy(q,g_wnd_open?"Open (not encrypted)":g_wpa_akm==8?"WPA3-Personal":g_wpa_akm==1?"WPA2-Enterprise (PEAP)":"WPA2-Personal"); if(!g_wnd_open){ q=scopy(q,", group key renewed "); q=udec(q,(u32)g_wifi_rekeys); q=scopy(q,"x"); } *q=0;
            q=wv[6]; q=ipstr(q,gw_ip); q=scopy(q,"  /  "); q=ipstr(q,dns_ip); *q=0; st_infocard(&cy,"Wi-Fi connection",wa,wv,7); } } break;
    case P_MOUSE: { static const char* MA[3]={"Off","Low","High"}; int y=st_card(cy,128,"Pointer"); st_rowtext(y,"Pointer acceleration","Moves faster when you move the mouse quickly"); { int w=st_segw(MA,3); st_seg(X1-w,y+17,MA,3,g_mouse_accel,65); } st_sep(y+64);
        st_rowtext(y+64,"Pointer style",0); { int w=st_segw(CUN,5); st_seg(X1-w,y+81,CUN,5,g_cursor,30); } } break;
    case P_APPS: { extern int progfiles_count(void); extern const pf_app* progfiles_get(int); extern int app_icon_draw(int,int,int,int); extern int acct_is_admin(int);
        int np=progfiles_count(); int nshow=0; for(int i=0;i<np;i++) if(!progfiles_get(i)->hidden)nshow++;
        q=b; q=udec(q,(u32)nshow); q=scopy(q," apps - each one has a folder in /Programs with an app.info file"); *q=0; pdstr(st_x0+4,cy,b,ST_DIM,1); cy+=24;
        int rh=46, bot=WN[W_SETTINGS].y+WN[W_SETTINGS].h-8; int skip=g_st_appscroll, n=0;
        for(int i=0;i<np;i++){ const pf_app* a=progfiles_get(i); if(a->hidden)continue; if(skip>0){ skip--; continue; } int by=cy+n*(rh+6); if(by+rh>bot)break;
            int hov=in_rect(mx,my,st_x0,by,st_w0,rh); fill_round(st_x0,by,st_w0,rh,10,15,hov?rgb(250,251,253):rgb(255,255,255));
            if(a->win>=0)launch_icon(a->win,st_x0+6,by+(rh-48)/2+1); else if(!app_icon_draw(1100+a->xi,st_x0+12,by+(rh-36)/2,36)){ fill_round(st_x0+12,by+(rh-36)/2,36,36,8,15,mix(ACC,rgb(255,255,255),30)); char t2[2]={a->name[0],0}; pdstr_ctr(st_x0+30,by+(rh-16)/2,t2,rgb(255,255,255),1); }
            pdstr(st_x0+62,by+6,a->name,INK,1);
            q=b; q=scopy(q,a->kind==PF_BUILTIN?"Built in":a->kind==PF_LINUX?"Linux app":"Installed with apt"); if(a->kind!=PF_BUILTIN){ q=scopy(q,"  -  "); u32 kb=(a->size+1023)/1024; if(a->kind==PF_LINUX)q=scopy(q,"in the Linux files"); else if(kb>=1024){ q=udec(q,kb/1024); q=scopy(q," MB"); } else { q=udec(q,kb); q=scopy(q," KB"); } } if(a->pkg[0]){ q=scopy(q,"  -  package "); q=scopy(q,a->pkg); } *q=0; pdstr(st_x0+62,by+25,b,ST_DIM,1);
            int X1b=st_x0+st_w0-12; if(a->kind==PF_PACKAGE){ st_btn(X1b-100,by+(rh-26)/2,100,"Uninstall",2300+i,2); X1b-=110; } st_btn(X1b-80,by+(rh-26)/2,80,(a->win>=0&&WN[a->win].open)?"Show":"Open",2200+i,1);
            n++; }
        if(nshow>n+g_st_appscroll||g_st_appscroll){ q=b; q=scopy(q,"Scroll for more ("); q=udec(q,(u32)(g_st_appscroll+1)); *q++='-'; q=udec(q,(u32)(g_st_appscroll+n)); q=scopy(q," of "); q=udec(q,(u32)nshow); *q++=')'; *q=0; pdstr(st_x0+4,bot-18,b,ST_DIM,1); } } break;
    case P_ACCOUNT: { extern int acct_is_admin(int); extern char g_fullname[]; extern int g_guest_on, g_lock_min; extern int acct_ui_others(acct_info*,int);
        int admin=acct_is_admin(g_uid); const char* dn=g_fullname[0]?g_fullname:g_user;
        int y=st_card(cy,96,0); fill_circle(st_x0+52,y+48,28,ACC); { char ini=dn[0]; if(ini>='a'&&ini<='z')ini-=32; char s2[2]={ini?ini:'U',0}; st_bold_ctr(st_x0+52,y+41,s2,rgb(255,255,255)); }
        st_bold(st_x0+96,y+28,dn,ST_FG); q=b; q=scopy(q,g_user); q=scopy(q,"  -  "); q=scopy(q,admin?"Administrator":"Standard user"); *q=0; pdstr(st_x0+96,y+52,b,ST_DIM,1);
        st_btn(X1-150,y+12,150,"Change password",601,1); st_btn(X1-150,y+52,150,"Change name",600,0); cy=y+96+20;
        acct_info o[4]; int n=acct_ui_others(o,4); int shown=n>3?3:n; int rh=56; int h=(shown?shown:1)*rh+rh;
        y=st_card(cy,h,"Other users");
        if(!shown){ st_rowtext(y,"No other accounts","Add an account for each person who uses this computer"); }
        for(int i=0;i<shown;i++){ int ry=y+i*rh; const char* f=o[i].full[0]?o[i].full:o[i].name; q=b; q=scopy(q,o[i].name); q=scopy(q,o[i].admin?"  -  Administrator":"  -  Standard user"); *q=0;
            pdstr(st_x0+20,ry+10,f,ST_FG,1); pdstr(st_x0+20,ry+30,b,ST_DIM,1);
            if(admin){ st_btn(X1-90,ry+11,90,"Remove",660+i,2); st_btn(X1-196,ry+11,96,"Password",640+i,0); st_btn(X1-336,ry+11,130,o[i].admin?"Make standard":"Make admin",620+i,0); }
            st_sep(ry+rh); }
        { int ry=y+(shown?shown:1)*rh; if(admin)st_btn(X1-150,ry+11,150,"Add account",602,1); pdstr(st_x0+20,ry+20,admin?(n>3?"More accounts: use the Terminal (useradd, userdel)":"New accounts get a home folder in /home"):"Only an administrator can change other accounts",ST_DIM,1); }
        cy=y+h+20;
        extern int lg_installed(void); extern int live_lock_on(void); int llk=lg_installed()?-1:live_lock_on();
        y=st_card(cy,rh*(llk>=0?5:4),"Sign-in options");
        { extern int fid_has(const char*); int hf=fid_has(g_user); st_rowtext(y,"Face ID",hf?"Set up - sign in by looking at the camera":"Sign in by looking at the camera instead of typing");
          if(hf){ st_btn(X1-90,y+11,90,"Remove",681,2); st_btn(X1-216,y+11,116,"Scan again",680,0); } else st_btn(X1-110,y+11,110,"Set up",680,1); st_sep(y+rh); y+=rh; }
        st_rowtext(y,"Guest account","Lets someone sign in without a password; their files are removed at sign out"); st_toggle(X1-44,y+16,g_guest_on,603); st_sep(y+rh);
        { static const char* LM[4]={"5 min","15 min","30 min","Never"}; int sel=g_lock_min==5?0:g_lock_min==15?1:g_lock_min==30?2:g_lock_min<=0?3:1; st_rowtext(y+rh,"Lock the screen after","When idle, on lid close, and with Win+L"); { int w=st_segw(LM,4); st_seg(X1-w,y+rh+13,LM,4,sel,610); } st_sep(y+rh*2); }
        st_rowtext(y+rh*2,"Lock or sign out",0); st_btn(X1-110,y+rh*2+11,110,"Sign out",605,0); st_btn(X1-230,y+rh*2+11,110,"Switch user",606,0); st_btn(X1-340,y+rh*2+11,100,"Lock",604,0);
        if(llk>=0){ st_sep(y+rh*3); st_rowtext(y+rh*3,"Sign-in screen when starting from the USB stick","Saved on the USB stick; turn off to go straight to the Terminal"); st_toggle(X1-44,y+rh*3+16,llk,607); y+=rh; }
        cy=y+rh*3+20;
        if(cy+90<WN[W_SETTINGS].y+WN[W_SETTINGS].h){ y=st_card(cy,56,"Noovex account"); st_rowtext(y,g_nvx_signedin?"Signed in":"Not signed in",g_nvx_signedin?"Accent colour and wallpaper sync to your account":"Sign in during setup to sync settings"); } } break;
    case P_TIME: { int hh,mm,ss2,yy,mo,dd; read_clock(&hh,&mm,&ss2,&yy,&mo,&dd); int y=st_card(cy,64,0); q=b; q=u2(q,hh);*q++=':';q=u2(q,mm);*q++=':';q=u2(q,ss2); q=scopy(q,"   "); q=udec(q,(u32)yy);*q++='-';q=u2(q,mo);*q++='-';q=u2(q,dd); *q=0; st_rowtext(y,"Current date and time",b); cy=y+64+24;
        y=st_card(cy,64,"Clock"); st_rowtext(y,"Show seconds","Adds seconds to the clock on the taskbar"); st_toggle(X1-44,y+20,g_showsec,1); } break;
    case P_LANG: { const char* a[2]={"Display language","Region format"}; char v[2][80]; scopy(v[0],"English")[0]=0; scopy(v[1],"24-hour clock, year-month-day")[0]=0; st_infocard(&cy,0,a,v,2);
        extern int g_kbd_layout; extern const char* kbd_layout_name(int); int y=st_card(cy,64,"Keyboard"); st_rowtext(y,kbd_layout_name(g_kbd_layout),"Layout of the physical keyboard; click Change to pick the next one"); st_btn(X1-110,y+16,110,"Change",4,1);
        cy=y+64+24; { extern int g_krep_speed; extern const char* kbd_repeat_name(int); y=st_card(cy,64,"Key repeat"); st_rowtext(y,kbd_repeat_name(g_krep_speed),"How fast a held key repeats; click Change for Slow, Normal or Fast"); st_btn(X1-110,y+16,110,"Change",5,1); } } break;
    case P_SECURITY: { int y=st_card(cy,64,"Build"); { q=b; q=scopy(q,"NoovexOS 64 v"); q=scopy(q,(char*)g_buildsig.version); *q=0; st_rowtext(y,b,g_sig_ok?"Integrity check passed at boot":"Integrity check reported a mismatch at boot"); }
        fill_round(X1-110,y+20,110,24,12,15,g_sig_ok?rgb(226,246,233):rgb(253,236,226)); pdstr_ctr(X1-55,y+25,g_sig_ok?"Verified":"Mismatch",g_sig_ok?rgb(40,140,80):rgb(200,110,40),1); } break;
    case P_UPDATE: st_upd_page(cy); break;
    case P_RECOVERY: { int y=st_card(cy,192,"Recovery"); st_rowtext(y,"Restart","Restart the computer now"); st_btn(X1-110,y+16,110,"Restart",2,1); st_sep(y+64);
        st_rowtext(y+64,"Install NoovexOS","Put this system on a disk"); st_btn(X1-120,y+80,120,"Open Setup",95,1); st_sep(y+128);
        st_rowtext(y+128,"Crash test","Shows the kernel panic screen (for testing)"); st_btn(X1-110,y+144,110,"Crash",3,2); } break;
    default: if(p>=P_DEV0&&p<P_DEV0+7){ g_devtab=p-P_DEV0; st_notabs=1; old_leaf(8,cy); st_notabs=0; } break; } }
