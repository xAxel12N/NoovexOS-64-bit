#include "include/nvx/base.h"
#include "include/nvx/lib/base/fmt.h"
#include "include/nvx/drivers/video/fb/framebuffer.h"
#include "include/nvx/graphics/theme/theme.h"
#include "include/nvx/graphics/ui/wm.h"

#define XT_MAX 6
#define XT_MENU 16
typedef struct { int used; char title[48]; u32 icon[256]; int hasicon; int nm; int mid[XT_MENU]; char ml[XT_MENU][40]; } xt_item;
static xt_item g_xt[XT_MAX];
static char g_xtq[1024]; static int g_xtqn;
static int g_xt_x[XT_MAX], g_xt_n, g_xt_x0, g_xt_y0, g_xt_h;
static int g_xtpop=-1, g_xtpop_x, g_xtpop_y;
int g_xt_gen;
extern int mx, my;
#define C_FACE rgb(212,208,200)

static int xt_hex(char c){ if(c>='0'&&c<='9')return c-'0'; if(c>='a'&&c<='f')return c-'a'+10; if(c>='A'&&c<='F')return c-'A'+10; return -1; }
static void xt_push(const char* s){ int n=0; while(s[n])n++; if(g_xtqn+n>=(int)sizeof g_xtq)return; for(int i=0;i<n;i++)g_xtq[g_xtqn++]=s[i]; }
static void xt_ev(const char* kind,int slot,int id){ char b[48]; char* p=b; const char* k=kind; while(*k)*p++=*k++; *p++=' '; *p++=(char)('0'+slot); if(id>=0){ *p++=' '; char t[12]; int n=0; u32 v=(u32)id; do{ t[n++]=(char)('0'+v%10); v/=10; }while(v); while(n)*p++=t[--n]; } *p++='\n'; *p=0; xt_push(b); }

int xtray_read(char* buf,int len){ int n=g_xtqn<len?g_xtqn:len; for(int i=0;i<n;i++)buf[i]=g_xtq[i]; for(int i=n;i<g_xtqn;i++)g_xtq[i-n]=g_xtq[i]; g_xtqn-=n; return n; }
int xtray_ready(void){ return g_xtqn>0; }

static void xt_line(const char* s,int n){
    if(n<5)return;
    int slot=s[4]-'0'; if(slot<0||slot>=XT_MAX)return; xt_item* t=&g_xt[slot];
    if(s[0]=='d'&&s[1]=='e'&&s[2]=='l'&&s[3]==' '){ t->used=0; if(g_xtpop==slot)g_xtpop=-1; g_xt_gen++; return; }
    if(s[0]=='a'&&s[1]=='d'&&s[2]=='d'&&s[3]==' '){ int i=6, k=0; while(i<n&&s[i]!=0x1f&&k<47){ unsigned char c=(unsigned char)s[i++]; t->title[k++]=(c>=32&&c<127)?(char)c:'?'; } t->title[k]=0;
        while(i<n&&s[i]!=0x1f)i++; t->hasicon=0;
        if(i<n&&s[i]==0x1f&&n-i-1>=2048){ i++; for(int q=0;q<256;q++){ u32 v=0; for(int h=0;h<8;h++){ int d=xt_hex(s[i+q*8+h]); if(d<0)d=0; v=(v<<4)|(u32)d; } t->icon[q]=v; } t->hasicon=1; }
        if(!t->used)t->nm=0; t->used=1; g_xt_gen++; return; }
    if(s[0]=='m'&&s[1]=='n'&&s[2]=='u'&&s[3]==' '){ int i=5; t->nm=0;
        while(i<n&&t->nm<XT_MENU){ if(s[i]!=0x1f)break; i++; int id=0, neg=0; if(i<n&&s[i]=='-'){ neg=1; i++; } while(i<n&&s[i]>='0'&&s[i]<='9')id=id*10+(s[i++]-'0'); if(i>=n||s[i]!=0x1e)break; i++;
            int k=0; char* l=t->ml[t->nm]; while(i<n&&s[i]!=0x1f&&k<39){ unsigned char c=(unsigned char)s[i++]; l[k++]=(c>=32&&c<127)?(char)c:'?'; } l[k]=0; while(i<n&&s[i]!=0x1f)i++;
            t->mid[t->nm++]=neg?-id:id; }
        g_xt_gen++; return; } }
int xtray_write(const char* buf,int len){ int s=0; for(int i=0;i<=len;i++){ if(i==len||buf[i]=='\n'){ xt_line(buf+s,i-s); s=i+1; } } return len; }

static void xt_icon(int x,int y,xt_item* t,int dark){
    if(t->hasicon){ for(int r=0;r<16;r++) for(int c=0;c<16;c++){ u32 v=t->icon[r*16+c]; int a=(int)(v>>24); if(!a)continue; int X=x+c, Y=y+r; if(X<0||Y<0||(u32)X>=W||(u32)Y>=H)continue; u32 bg=SCENE[(u64)Y*SW+(u64)X]; SCENE[(u64)Y*SW+(u64)X]=mix(bg,v&0xFFFFFF,a); } return; }
    fill_round(x,y,16,16,4,15,dark?rgb(88,101,242):rgb(60,70,200)); char l[2]={t->title[0]?t->title[0]:'?',0}; if(l[0]>='a'&&l[0]<='z')l[0]=(char)(l[0]-32); dstr(x+4,y+1,l,rgb(255,255,255),1); }

int xtray_bar(int right,int y,int h,int dark){
    g_xt_n=0; int n=0; for(int i=0;i<XT_MAX;i++) if(g_xt[i].used)n++;
    if(!n){ g_xt_x0=right; return right; }
    int w=n*22+6, x0=right-w; g_xt_x0=x0; g_xt_y0=y; g_xt_h=h;
    if(!dark){ ui_bevel(x0,y+4,w,h-7,1); fill_rect(x0+2,y+6,w-4,h-11,C_FACE); }
    int ix=x0+4;
    for(int i=0;i<XT_MAX;i++){ if(!g_xt[i].used)continue; int hv=(mx>=ix&&mx<ix+20&&my>=y&&my<y+h); if(hv)fill_rect(ix,y+6,20,h-12,dark?rgb(60,64,84):rgb(168,168,176)); xt_icon(ix+2,y+h/2-8,&g_xt[i],dark); g_xt_x[g_xt_n++]=i; ix+=22; }
    return x0-4; }

int xtray_bar_click(int px,int py,int right){
    if(!g_xt_n||py<g_xt_y0||py>=g_xt_y0+g_xt_h||px<g_xt_x0+4||px>=g_xt_x0+4+g_xt_n*22)return 0;
    int k=(px-g_xt_x0-4)/22; if(k<0||k>=g_xt_n)return 0; int slot=g_xt_x[k];
    if(right&&g_xt[slot].nm>0){ g_xtpop=slot; g_xtpop_x=px; g_xtpop_y=g_xt_y0; xt_ev("open",slot,-1); return 1; }
    if(right){ xt_ev("menu",slot,-1); return 1; }
    xt_ev("act",slot,-1); g_xtpop=-1; return 1; }

static int xt_pop_geo(int* x,int* y,int* w,int* h){ if(g_xtpop<0||!g_xt[g_xtpop].used)return 0; xt_item* t=&g_xt[g_xtpop]; int ww=180; for(int i=0;i<t->nm;i++){ int l=0; while(t->ml[i][l])l++; if(l*8+32>ww)ww=l*8+32; } int hh=t->nm*24+8; int xx=g_xtpop_x-ww/2; if(xx+ww>(int)W-4)xx=(int)W-4-ww; if(xx<4)xx=4; *x=xx; *y=g_xtpop_y-hh-4; *w=ww; *h=hh; return 1; }
void xtray_pop_draw(int dark){
    int x,y,w,h; if(!xt_pop_geo(&x,&y,&w,&h))return; xt_item* t=&g_xt[g_xtpop];
    if(dark){ shadow_soft(x,y,w,h,10); fill_round(x,y,w,h,10,15,rgb(32,36,46)); } else { fill_rect(x,y,w,h,rgb(212,208,200)); ui_bevel(x,y,w,h,0); }
    for(int i=0;i<t->nm;i++){ int iy=y+4+i*24; const char* l=t->ml[i];
        if(l[0]=='-'&&!l[1]){ fill_rect(x+8,iy+11,w-16,1,dark?rgb(70,74,90):rgb(128,128,128)); continue; }
        int dis=t->mid[i]<0; int hv=!dis&&(mx>=x&&mx<x+w&&my>=iy&&my<iy+24); if(hv)fill_rect(x+3,iy,w-6,24,dark?rgb(60,64,84):rgb(210,192,182));
        dstr(x+12,iy+4,l,dis?rgb(128,128,128):(dark?rgb(230,232,240):rgb(0,0,0)),1); } }
int xtray_pop_open(void){ return g_xtpop>=0; }
int xtray_pop_click(int px,int py){
    int x,y,w,h; if(!xt_pop_geo(&x,&y,&w,&h)){ g_xtpop=-1; return 0; }
    int slot=g_xtpop; g_xtpop=-1;
    if(px<x||px>=x+w||py<y||py>=y+h)return py>=g_xt_y0&&px>=g_xt_x0&&px<g_xt_x0+4+g_xt_n*22;
    int i=(py-y-4)/24; xt_item* t=&g_xt[slot]; if(i<0||i>=t->nm)return 1; if(t->mid[i]<0||(t->ml[i][0]=='-'&&!t->ml[i][1]))return 1;
    xt_ev("sel",slot,t->mid[i]); return 1; }
extern char g_clip[]; extern int g_clip_len;
static u32 g_clh_seen; static int g_clh_init;
static u32 xclip_hash(void){ u32 h=2166136261u; for(int i=0;i<g_clip_len;i++){ h^=(u8)g_clip[i]; h*=16777619u; } return h^(u32)g_clip_len; }
int xclip_ready(void){ if(!g_clh_init){ g_clh_init=1; g_clh_seen=xclip_hash(); } return xclip_hash()!=g_clh_seen; }
static int g_clrd;
int xclip_read(char* buf,int len){ u32 h=xclip_hash(); if(h!=g_clh_seen||!g_clh_init)g_clrd=0; g_clh_seen=h; g_clh_init=1; if(g_clrd>=g_clip_len){ g_clrd=0; return 0; } int n=g_clip_len-g_clrd; if(n>len)n=len; if(n<0)n=0; for(int i=0;i<n;i++)buf[i]=g_clip[g_clrd+i]; g_clrd+=n; return n; }
int xclip_write(const char* buf,int len){ if(len>65535)len=65535; if(len<0)len=0; for(int i=0;i<len;i++)g_clip[i]=buf[i]; g_clip[len]=0; g_clip_len=len; g_clh_seen=xclip_hash(); g_clh_init=1; g_clrd=0; return len; }
