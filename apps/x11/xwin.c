#include "include/nvx/base.h"
#include "include/nvx/lib/base/fmt.h"
#include "include/nvx/drivers/video/fb/framebuffer.h"
#include "include/nvx/graphics/theme/theme.h"
#include "include/nvx/graphics/ui/wm.h"

#define XW_MAX 32
typedef struct { u32 id; int x, y, w, h, min, foc; char title[64]; } xw_ent;
static xw_ent g_xw[XW_MAX], g_xws[XW_MAX];
static int g_xwn, g_xwsn, g_xw_in;
static char g_xwq[2048]; static int g_xwqn;
static char g_xwl[256]; static int g_xwll;
static int g_xw_ax=-1, g_xw_ay, g_xw_aw, g_xw_ah;
int g_xz;
extern int g_xt_gen, g_xkbd, g_xover;

static void xw_kick(void){ extern void jc_kick_waiters(void); jc_kick_waiters(); }
static void xw_push(const char* s){ int n=0; while(s[n])n++; if(g_xwqn+n>=(int)sizeof g_xwq)return; for(int i=0;i<n;i++)g_xwq[g_xwqn++]=s[i]; xw_kick(); }
static void xw_cmd(char c,u32 id){ char b[24],*p=b; *p++=c; *p++=' '; p=udec(p,id); *p++='\n'; *p=0; xw_push(b); }
static int xw_num(const char* s,int n,int* i,long* v){ while(*i<n&&s[*i]==' ')(*i)++; int neg=0; if(*i<n&&s[*i]=='-'){ neg=1; (*i)++; } if(*i>=n||s[*i]<'0'||s[*i]>'9')return 0; long r=0; while(*i<n&&s[*i]>='0'&&s[*i]<='9'){ r=r*10+(s[*i]-'0'); (*i)++; } *v=neg?-r:r; return 1; }
int xwin_top(void){ for(int i=0;i<NWIN;i++) if(WN[i].open&&!WN[i].min&&WN[i].ws==g_ws&&WN[i].z>g_xz)return 0; return 1; }
static void xw_raise_layer(void){ if(!xwin_top()){ g_xz=++ztop; g_xt_gen++; extern void xover_invalidate(void); xover_invalidate(); } }
static void xw_line(const char* s,int n){
    if(n<1)return;
    if(s[0]=='B'){ g_xwsn=0; g_xw_in=1; return; }
    if(s[0]=='E'){ if(!g_xw_in)return; g_xw_in=0; int added=0;
        for(int i=0;i<g_xwsn;i++){ int f=0; for(int j=0;j<g_xwn;j++) if(g_xw[j].id==g_xws[i].id)f=1; if(!f&&!g_xws[i].min)added=1; }
        for(int i=0;i<g_xwsn;i++)g_xw[i]=g_xws[i]; g_xwn=g_xwsn;
        if(added){ g_xz=++ztop; g_xkbd=1; extern void xover_invalidate(void); xover_invalidate(); }
        else { int vis=0; for(int i=0;i<g_xwn;i++) if(!g_xw[i].min)vis=1; if(!vis)g_xkbd=0; }
        g_xt_gen++; return; }
    if(s[0]=='W'&&g_xw_in&&g_xwsn<XW_MAX){ int i=1; long v[7]; for(int k=0;k<7;k++) if(!xw_num(s,n,&i,&v[k]))return;
        xw_ent* e=&g_xws[g_xwsn]; e->id=(u32)v[0]; e->x=(int)v[1]; e->y=(int)v[2]; e->w=(int)v[3]; e->h=(int)v[4]; e->min=(int)v[5]; e->foc=(int)v[6];
        if(i<n&&s[i]==' ')i++; int k=0; while(i<n&&k<63){ unsigned char c=(unsigned char)s[i++]; e->title[k++]=(c>=32&&c<127)?(char)c:'?'; } e->title[k]=0; g_xwsn++; return; }
}
int xwin_write(const char* buf,int len){ for(int i=0;i<len;i++){ char c=buf[i]; if(c=='\n'){ xw_line(g_xwl,g_xwll); g_xwll=0; } else if(g_xwll<(int)sizeof g_xwl)g_xwl[g_xwll++]=c; } return len; }
static void xw_area(void){ extern int g_tby; int ah=g_tby>0?g_tby:(int)H-40; if(g_xw_ax==0&&g_xw_ay==0&&g_xw_aw==(int)W&&g_xw_ah==ah)return;
    char b[64],*p=b; p=scopy(p,"A 0 0 "); p=udec(p,(u32)W); *p++=' '; p=udec(p,(u32)ah); *p++='\n'; *p=0; int q=g_xwqn; xw_push(b); if(g_xwqn==q)return; g_xw_ax=0; g_xw_ay=0; g_xw_aw=(int)W; g_xw_ah=ah; }
int xwin_ready(void){ xw_area(); return g_xwqn>0; }
int xwin_read(char* buf,int len){ xw_area(); int n=g_xwqn<len?g_xwqn:len; for(int i=0;i<n;i++)buf[i]=g_xwq[i]; for(int i=n;i<g_xwqn;i++)g_xwq[i-n]=g_xwq[i]; g_xwqn-=n; return n; }
int xwin_count(void){ return g_xover?g_xwn:0; }
const char* xwin_title(int k){ return (k>=0&&k<g_xwn)?(g_xw[k].title[0]?g_xw[k].title:"Linux app"):""; }
int xwin_active(int k){ return k>=0&&k<g_xwn&&g_xw[k].foc&&!g_xw[k].min&&g_xkbd&&xwin_top(); }
void xwin_activate(int k){ if(k<0||k>=g_xwn)return; xw_cmd('R',g_xw[k].id); g_xw[k].min=0; g_xkbd=1; g_xz=++ztop; g_xt_gen++; extern void xover_invalidate(void); xover_invalidate(); }
void xwin_minimize(int k){ if(k<0||k>=g_xwn)return; xw_cmd('M',g_xw[k].id); g_xw[k].min=1; g_xkbd=0; g_xt_gen++; }
void xwin_close(int k){ if(k>=0&&k<g_xwn)xw_cmd('C',g_xw[k].id); }
void xwin_toggle(int k){ if(xwin_active(k))xwin_minimize(k); else xwin_activate(k); }
void xwin_clicked(void){ g_xkbd=1; xw_raise_layer(); }
void xwin_icon(int k,int x,int y,int sz){
    const char* t=xwin_title(k); u32 h=2166136261u; for(int i=0;t[i];i++){ h^=(u8)t[i]; h*=16777619u; }
    u32 c=rgb(60+(int)(h&0x7F),60+(int)((h>>8)&0x7F),90+(int)((h>>16)&0x7F));
    fill_round(x,y,sz,sz,sz/5,15,c); char l[2]={t[0]>='a'&&t[0]<='z'?(char)(t[0]-32):t[0],0}; if(!l[0])l[0]='L';
    int sc=sz>=32?2:1; dstr(x+sz/2-4*sc,y+sz/2-8*sc+(sc==2?0:4),l,rgb(255,255,255),sc); }
int xw_mask_n; int xw_mask[NWIN][4];
void xwin_mask_build(void){ xw_mask_n=0; for(int i=0;i<NWIN;i++) if(WN[i].open&&!WN[i].min&&WN[i].ws==g_ws&&WN[i].z>g_xz){ int* r=xw_mask[xw_mask_n++]; r[0]=WN[i].x-6; r[1]=WN[i].y-6; r[2]=WN[i].x+WN[i].w+6; r[3]=WN[i].y+WN[i].h+6; } }
int xwin_masked(int x,int y){ for(int k=0;k<xw_mask_n;k++){ int* r=xw_mask[k]; if(x>=r[0]&&x<r[2]&&y>=r[1]&&y<r[3])return 1; } return 0; }
int xwin_row_spans(int y,int* xs,int* xe,int max){ int n=0; for(int k=0;k<xw_mask_n&&n<max;k++){ int* r=xw_mask[k]; if(y>=r[1]&&y<r[3]){ xs[n]=r[0]; xe[n]=r[2]; n++; } } return n; }
