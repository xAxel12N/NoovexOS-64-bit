/* =========================================================================
   Window manager
   ========================================================================= */
enum { W_WELCOME, W_TERM, W_FILES, W_EDITOR, W_CALC, W_BROWSER, W_CLOCK, W_SYSMON, W_CALENDAR, W_SNAKE, W_FLAPPY, W_NEOFETCH, W_PAINT, W_DOOM, W_SETTINGS, W_ABOUT, W_CHATGPT, W_PPTX, W_LOGS, W_TFIND, NWIN };
typedef struct{ int x,y,w,h,open,z,min,max,sx,sy,sw,sh,ws; } Win;
static Win WN[NWIN];
static int g_ws=0;                                   /* current workspace (0..3) */
#define NWS 4
/* ---- notifications: toast queue + history ---- */
typedef struct{ char title[40]; char body[72]; u64 t; int shown; } Notif;
#define NOTIF_MAX 32
static Notif g_notif[NOTIF_MAX]; static int g_notif_n=0; static u64 g_toast_until=0; static int g_toast_cur=-1;
static int g_notifpanel=0;                            /* history panel open */
static void notify(const char* title,const char* body){
    int i=g_notif_n%NOTIF_MAX; int k=0; while(title[k]&&k<39){g_notif[i].title[k]=title[k];k++;} g_notif[i].title[k]=0;
    k=0; if(body){ while(body[k]&&k<71){g_notif[i].body[k]=body[k];k++;} } g_notif[i].body[k]=0;
    g_notif[i].shown=0; g_notif_n++; g_toast_cur=i; g_toast_until=0;   /* shown next frame */
}
static int focus=-1, ztop=1, drag=-1, dox=0, doy=0;
static int g_ctxkind=0, g_ctxwin=-1;                 /* right-click menu: 0 desktop 1 window 2 files 4 tree */
static int g_tctx_type=-1, g_tctx_val=-1;            /* tree ctx target: 0 folder,1 fs-file,2 curated */
static int g_dctx_item=-1;                           /* desktop right-click target (app 0-4, or 100+slot) */
static int g_prop=0, g_prop_app=-1, g_prop_slot=-1; static void prop_draw(void);
static int g_submenu=0, g_sub_x=0, g_sub_y=0, g_subn=0;   /* hover submenu */
static int g_fdrag=0, g_fdrag_slot=-1, g_fdrag_x=0, g_fdrag_y=0;   /* drag a tree file */
static const char* g_subit[8]; static char g_subparent[16]="";
static int g_snap=0, g_resize=-1, r_sw0=0, r_sh0=0, r_mx0=0, r_my0=0;
static int g_launcher=0, g_alttab=0, g_altsel=0, g_altlist[NWIN], g_altn=0, alt_prev=0;
static int g_wallmode=0, wall_custom_slot=-1, g_clockpop=0, g_qs=0;
static int g_rename=0, g_renslot=-1, g_renlen=0; static char g_renbuf[64]="";
static int g_clockx=0, g_qsx=0;
static int g_marq=0, marq_x0=0, marq_y0=0;
static char toast_msg[80]=""; static u64 toast_t=0;
static void toast(const char*m){ int i=0; while(m[i]&&i<79){toast_msg[i]=m[i];i++;} toast_msg[i]=0; toast_t=g_ticks; }
static const char *APPNAME[NWIN]={"Welcome","Terminal","Files","Editor","Calculator","Browser","Clock","Task Manager","Calendar","Snake","Flappy Bird","neofetch","Paint","DOOM","Settings","About","ChatGPT","Presentation","Logs","Terminal Finder"};
static void win_anim(int i,int opening);   /* fwd - defined near compositor */
static void snake_reset(void); static void flappy_reset(void); static void cal_open(void);
static void open_win(int i){ int was=WN[i].open; WN[i].open=1; WN[i].min=0; WN[i].ws=g_ws; focus=i; WN[i].z=++ztop;
    if(!was){ if(i==W_SNAKE)snake_reset(); else if(i==W_FLAPPY)flappy_reset(); else if(i==W_CALENDAR)cal_open(); }
    if(!was && g_state==ST_DESKTOP) win_anim(i,1); }
static void close_win(int i){ if(WN[i].open&&!WN[i].min&&g_state==ST_DESKTOP) win_anim(i,0); WN[i].open=0; WN[i].min=0; if(focus==i)focus=-1; if(i==W_DOOM) g_doom_mcap=0; }
static void focus_win(int i){ if(!WN[i].open){ open_win(i); return; } WN[i].min=0; focus=i; WN[i].z=++ztop; }
static void win_min(int i){ WN[i].min=1; if(focus==i)focus=-1; }
static void win_max(int i){ if(!WN[i].max){ WN[i].sx=WN[i].x;WN[i].sy=WN[i].y;WN[i].sw=WN[i].w;WN[i].sh=WN[i].h;
        WN[i].x=6; WN[i].y=32; WN[i].w=(int)W-12; WN[i].h=(int)H-32-86; WN[i].max=1; }
    else { WN[i].x=WN[i].sx; WN[i].y=WN[i].sy; WN[i].w=WN[i].sw; WN[i].h=WN[i].sh; WN[i].max=0; } focus=i; WN[i].z=++ztop; }
static void apply_snap(int i,int s){ int wy=32, wh=(int)H-32-86, hw=((int)W-18)/2, hh=(wh-6)/2; WN[i].max=1;
    if(s==3){ WN[i].x=6;WN[i].y=wy;WN[i].w=(int)W-12;WN[i].h=wh; }
    else if(s==1){ WN[i].x=6;WN[i].y=wy;WN[i].w=hw;WN[i].h=wh; }
    else if(s==2){ WN[i].x=(int)W-6-hw;WN[i].y=wy;WN[i].w=hw;WN[i].h=wh; }
    else if(s==4){ WN[i].x=6;WN[i].y=wy;WN[i].w=hw;WN[i].h=hh; }
    else if(s==5){ WN[i].x=(int)W-6-hw;WN[i].y=wy;WN[i].w=hw;WN[i].h=hh; }
    else if(s==6){ WN[i].x=6;WN[i].y=wy+hh+6;WN[i].w=hw;WN[i].h=hh; }
    else { WN[i].x=(int)W-6-hw;WN[i].y=wy+hh+6;WN[i].w=hw;WN[i].h=hh; } focus=i; WN[i].z=++ztop; }
#define TITLEH 40
static int win_ctl_hit(int i,int px,int py){          /* 0=min 1=max 2=close, -1=none */
    if(py<WN[i].y||py>=WN[i].y+TITLEH) return -1;
    int cw=46,xr=WN[i].x+WN[i].w; for(int b=0;b<3;b++){ int bx=xr-(3-b)*cw; if(px>=bx&&px<bx+cw) return b; } return -1;
}
static void win_frame(int i,const char*title){
    int x=WN[i].x,y=WN[i].y,w=WN[i].w,h=WN[i].h,r=14; int foc=(focus==i);
    shadow_soft(x,y,w,h,r);
    fill_round(x,y,w,h,r,15,rgb(250,251,253));
    for(int j=0;j<TITLEH;j++){ int shade=255-(j*30/TITLEH); u32 tc=foc?mix(rgb(252,253,255),rgb(238,240,246),255-shade):rgb(240,241,245);
        for(int ii=0;ii<w;ii++) if(!cut(ii,j,w,TITLEH,r,1|2)) putpx(x+ii,y+j,tc); }
    for(int ii=0;ii<w;ii++){ if(!cut(ii,0,w,TITLEH,r,1|2)){ u32* p=&SCENE[(u64)y*SW+x+ii]; *p=mix(*p,rgb(255,255,255),foc?150:200); } }
    dstr(x+16,y+11,title,foc?rgb(48,54,70):rgb(150,157,172),2);
    int cw=46,xr=x+w;
    for(int b=0;b<3;b++){ int bx=xr-(3-b)*cw; int hov=(focus==i && mx>=bx&&mx<bx+cw && my>=y&&my<y+TITLEH);
        if(hov){ u32 hc=(b==2)?rgb(232,64,54):rgb(224,226,233);
            for(int j=0;j<TITLEH;j++) for(int ii=0;ii<cw;ii++){ int gx=(bx-x)+ii; if(!cut(gx,j,w,TITLEH,r,1|2)) putpx(bx+ii,y+j,hc); } }
        int gx=bx+cw/2, gy=y+TITLEH/2; u32 gc=(hov&&b==2)?rgb(255,255,255):(foc?rgb(78,84,100):rgb(150,157,172));
        if(b==0){ for(int t=-5;t<=5;t++){ putpx(gx+t,gy,gc); putpx(gx+t,gy+1,gc); } }
        else if(b==1){ for(int t=-5;t<=5;t++){ putpx(gx+t,gy-5,gc); putpx(gx+t,gy+5,gc); putpx(gx-5,gy+t,gc); putpx(gx+5,gy+t,gc); } }
        else { for(int t=-5;t<=5;t++){ putpx(gx+t,gy+t,gc); putpx(gx+t+1,gy+t,gc); putpx(gx+t,gy-t,gc); putpx(gx+t+1,gy-t,gc); } }
    }
    fill_rect(x+1,y+TITLEH,w-2,1,rgb(230,233,239));
    { int gx=x+w-6, gy=y+h-6; u32 gc=foc?rgb(170,176,190):rgb(200,204,214); for(int k=0;k<3;k++){ int o=k*4; for(int t=0;t<=o;t++){ putpx(gx-o+t,gy-t,gc); } } }
}
static int in_rect(int px,int py,int x,int y,int w,int h){ return px>=x&&px<x+w&&py>=y&&py<y+h; }

