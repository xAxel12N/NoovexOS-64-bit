/* =========================================================================
   Theming + blend helpers
   ========================================================================= */
static u32 ACC=rgb(60,120,224), ACC2=rgb(42,96,196);
#define INK  rgb(38,46,66)
#define CYAN rgb(90,210,230)
#define GREEN rgb(120,230,150)
#define GREY rgb(150,160,180)
#define WHT  rgb(230,235,245)
#define RED  rgb(224,96,90)
static int g_theme=0, g_showsec=1, g_cursor=0, g_accent=0, g_safemode=0;
enum { ST_SETUP, ST_LOGIN, ST_DESKTOP, ST_BOOTMENU, ST_RECOVERY, ST_DISKMGR, ST_INSTALL };
static int g_state=ST_DESKTOP;
static char g_user[32]="user";
static int g_ctx=0, ctx_x=0, ctx_y=0;
static void ctx_draw(void); static int ctx_click(int px,int py);   /* fwd */
static char login_pass[32]=""; static int login_pl=0;

static inline void blend_px(int x,int y,u32 c,int a){ if((u32)x<W&&(u32)y<H) SCENE[(u32)y*SW+x]=mix(SCENE[(u32)y*SW+x],c,a); }
static void fill_circle_bl(int cx,int cy,int r,u32 c,int a){
    for(int j=-r;j<=r;j++)for(int i=-r;i<=r;i++) if(i*i+j*j<=r*r) blend_px(cx+i,cy+j,c,a);
}
static void line_bl(int x0,int y0,int x1,int y1,u32 c,int a,int th){
    int dx=x1-x0,dy=y1-y0; if(dx<0)dx=-dx; if(dy<0)dy=-dy;
    int sx=x0<x1?1:-1, sy=y0<y1?1:-1, err=dx-dy;
    for(;;){ for(int j=-th;j<=th;j++)for(int i=-th;i<=th;i++) if(i*i+j*j<=th*th) blend_px(x0+i,y0+j,c,a);
        if(x0==x1&&y0==y1)break; int e2=2*err; if(e2>-dy){err-=dy;x0+=sx;} if(e2<dx){err+=dx;y0+=sy;} }
}
static void radial(int cx,int cy,int r,u32 c,int amax){
    long r2=(long)r*r;
    for(int j=-r;j<=r;j++)for(int i=-r;i<=r;i++){ long d2=(long)i*i+(long)j*j; if(d2>r2)continue;
        int a=(int)(amax-(amax*d2)/r2); if(a>0) blend_px(cx+i,cy+j,c,a); }
}

/* ---- brand "n" mark ---- */
static int inside_n(int i,int j,int Wm,int Hm,int t,int R){
    if(i<0||i>=Wm||j<0||j>=Hm) return 0;
    if(cut(i,j,Wm,Hm,R,1|2)) return 0;
    int nw=Wm-2*t, nh=Hm-t, nr=R-t; if(nr<0)nr=0; int ni=i-t, nj=j-t;
    if(ni>=0&&ni<nw&&nj>=0&&nj<nh && !cut(ni,nj,nw,nh,nr,1|2)) return 0;
    return 1;
}
static void draw_logo(int cx,int top,int S,u32 color,int a){
    int Wm=S,Hm=S,t=S*18/100,R=S*30/100, x0=cx-Wm/2,y0=top;
    for(int j=0;j<Hm;j++)for(int i=0;i<Wm;i++) if(inside_n(i,j,Wm,Hm,t,R)) blend_px(x0+i,y0+j,color,a);
    int uw=Wm,uh=S*14/100,uy=y0+Hm+S*9/100,ux=cx-uw/2;
    for(int j=0;j<uh;j++)for(int i=0;i<uw;i++) if(!cut(i,j,uw,uh,uh/2,15)) blend_px(ux+i,uy+j,color,a);
}

/* =========================================================================
   Mouse cursor (styles)
   ========================================================================= */
static int mx=0,my=0;
static const char *ARROW[19]={
 "X","XX","X.X","X..X","X...X","X....X","X.....X","X......X","X.......X",
 "X........X","X.........X","X....XXXXXX","X..X..X","X.X X..X","XX  X..X",
 "X    X..X","     X..X","      X..X","      XXXX"};
static void arrow_at(int px,int py,int s,u32 edge,u32 fill){
    for(int r=0;r<19;r++){ const char*row=ARROW[r]; for(int c=0;row[c];c++){ if(row[c]==' ')continue;
        for(int a=0;a<s;a++)for(int b=0;b<s;b++) pblend(px+c*s+a+2,py+r*s+b+3,rgb(0,0,0),46); } }
    for(int r=0;r<19;r++){ const char*row=ARROW[r];
        for(int c=0;row[c];c++){ u32 col; if(row[c]=='X')col=edge; else if(row[c]=='.')col=fill; else continue;
            if(s==1)putpx(px+c,py+r,col); else fill_rect(px+c*s,py+r*s,s,s,col); } }
}
static int g_doom_mcap; static int g_doom_state;   /* defined in the DOOM section */
static void draw_cursor(void){
    if(g_doom_mcap) return;   /* hidden while DOOM has the mouse */
    if(g_cursor==0) arrow_at(mx,my,2,rgb(18,20,26),rgb(255,255,255));
    else if(g_cursor==1) arrow_at(mx,my,3,rgb(18,20,26),rgb(255,255,255));
    else if(g_cursor==2) arrow_at(mx,my,2,rgb(120,130,150),rgb(255,255,255));
    else if(g_cursor==3){ u32 w=rgb(255,255,255),d=rgb(20,22,28);
        for(int i=-14;i<=14;i++){ if(i>-4&&i<4)continue; putpx(mx+i,my-1,d);putpx(mx+i,my,w);putpx(mx+i,my+1,d);
            putpx(mx-1,my+i,d);putpx(mx,my+i,w);putpx(mx+1,my+i,d); } }
    else { fill_circle(mx,my,7,rgb(20,22,28)); fill_circle(mx,my,5,ACC); fill_circle(mx,my,2,rgb(255,255,255)); }
}

