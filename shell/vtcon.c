#include "include/nvx/base.h"
#include "include/nvx/core/klog.h"
#include "include/nvx/lib/base/fmt.h"
#include "include/nvx/mem/heap.h"
#include "include/nvx/arch/x86_64/cpu/percpu.h"
#include "include/nvx/drivers/video/fb/framebuffer.h"
#include "include/nvx/graphics/theme/theme.h"
#include "include/nvx/net/http/http.h"
#include "include/nvx/shell/terminal.h"
#include "include/nvx/shell/vt.h"
#include "include/nvx/shell/vtcon.h"
#include "include/nvx/core/proc/jobs.h"
#include "include/nvx/core/auth/accounts.h"

struct vtc_slot { int used, trows, tcols, tcx, tcy, tsb_head, tsb_n, tlen, tty_fg, dcwd, state; u32 tcur; char tline[256];
    int uid, gid, auth, lstage, ln; u64 sudo_until; char luser[33]; char lbuf[128];
    char* pend; int pn; char (*ch)[TCMAX]; u32 (*co)[TCMAX]; u32 (*bg)[TCMAX]; char (*sb)[TCMAX]; u32 (*sbc)[TCMAX]; };

int g_vtc_active=0, g_vtc_kdmode=VTC_KD_TEXT; static int g_vtc_req=-1;
static struct vtc_slot g_vs[VTC_N+1];
#define VTC_PEND 16384

static void cpy(void* d,const void* s,u64 n){ u8* a=(u8*)d; const u8* b=(const u8*)s; for(u64 i=0;i<n;i++)a[i]=b[i]; }
static int vs_alloc(struct vtc_slot* s){
    if(s->ch)return 1;
    s->ch=kmalloc(sizeof TCH); s->co=kmalloc(sizeof TCO); s->bg=kmalloc(sizeof TBG); s->sb=kmalloc(sizeof TSB); s->sbc=kmalloc(sizeof TSBC);
    if(s->ch&&s->co&&s->bg&&s->sb&&s->sbc)return 1;
    kfree(s->ch); kfree(s->co); kfree(s->bg); kfree(s->sb); kfree(s->sbc); s->ch=0; s->co=0; s->bg=0; s->sb=0; s->sbc=0; return 0;
}
static void vs_save(struct vtc_slot* s){
    cpy(s->ch,TCH,sizeof TCH); cpy(s->co,TCO,sizeof TCO); cpy(s->bg,TBG,sizeof TBG); cpy(s->sb,TSB,sizeof TSB); cpy(s->sbc,TSBC,sizeof TSBC);
    s->trows=trows; s->tcols=tcols; s->tcx=tcx; s->tcy=tcy; s->tsb_head=tsb_head; s->tsb_n=tsb_n; s->tlen=tlen; s->tcur=tcur; cpy(s->tline,tline,sizeof tline);
    s->tty_fg=g_tty_fg; s->dcwd=g_dcwd; s->state=g_state; s->used=1; s->uid=g_uid; s->gid=g_gid; s->sudo_until=sudo_until_get();
}
static void vs_load(struct vtc_slot* s){
    cpy(TCH,s->ch,sizeof TCH); cpy(TCO,s->co,sizeof TCO); cpy(TBG,s->bg,sizeof TBG); cpy(TSB,s->sb,sizeof TSB); cpy(TSBC,s->sbc,sizeof TSBC);
    trows=s->trows; tcols=s->tcols; tcx=s->tcx; tcy=s->tcy; tsb_head=s->tsb_head; tsb_n=s->tsb_n; tlen=s->tlen; tcur=s->tcur; cpy(tline,s->tline,sizeof tline);
    g_tty_fg=s->tty_fg; g_dcwd=s->dcwd; g_tsb_off=0; g_term_gen++; g_uid=s->uid; g_gid=s->gid; sudo_until_set(s->sudo_until);
    if(s->pend&&s->pn){ int n=s->pn; s->pn=0; vt_out(s->pend,n); }
}
static char* dec(char* p,int v){ char t[12]; int n=0; if(v<0){ *p++='-'; v=-v; } do{ t[n++]=(char)('0'+v%10); v/=10; }while(v&&n<11); while(n)*p++=t[--n]; *p=0; return p; }
static void vtc_wipe(struct vtc_slot* s){ volatile char* v=s->lbuf; for(u32 i=0;i<sizeof s->lbuf;i++)v[i]=0; s->ln=0; }
static void vs_fresh(int n){
    extern char g_hostname[]; extern char g_user[]; char b[64],*p=b;
    { acct_info a; g_uid=-1; g_gid=-1; if(acct_get(g_user,&a)==0&&a.uid>0){ g_uid=a.uid; g_gid=a.gid; } sudo_forget(); }
    { struct vtc_slot* s=&g_vs[n]; s->auth=0; s->lstage=0; s->luser[0]=0; vtc_wipe(s); }
    tcols=((int)W-28)/8; if(tcols>TCMAX)tcols=TCMAX; if(tcols<10)tcols=10; trows=((int)H-28)/TLH; if(trows>TRMAX)trows=TRMAX; if(trows<3)trows=3;
    term_clear(); tlen=0; tline[0]=0; g_tty_fg=0;
    p=scopy(p,"NoovexOS 64 "); p=scopy(p,g_hostname); p=scopy(p," tty"); p=dec(p,n); *p++='\n'; *p++='\n'; *p=0;
    tcol(GREY); term_puts(b); tcol(WHT); term_puts("login: ");
}
int vtc_login_key(char c){
    if(g_vtc_active<=0||g_vtc_active>VTC_N)return 0;
    struct vtc_slot* s=&g_vs[g_vtc_active]; if(s->auth)return 0;
    if(c=='\b'){ if(s->ln>0){ s->ln--; if(!s->lstage)term_putc('\b'); } return 1; }
    if(c!='\n'){ if(c>=32&&c<127&&s->ln<(int)sizeof s->lbuf-1){ s->lbuf[s->ln++]=c; if(!s->lstage)term_putc(c); } return 1; }
    term_putc('\n'); s->lbuf[s->ln]=0;
    acct_info a;
    if(!s->lstage){ int k=0; for(;s->lbuf[k]&&k<32;k++)s->luser[k]=s->lbuf[k]; s->luser[k]=0; vtc_wipe(s); if(!k){ term_puts("login: "); return 1; }
        { extern int lg_installed(void); if(!lg_installed()&&acct_get(s->luser,&a)==0&&!a.locked&&!a.haspw){ s->lstage=0; g_uid=a.uid; g_gid=a.gid; s->uid=a.uid; s->gid=a.gid; s->auth=1; sudo_forget(); klog("vt: console login (live session, no password)"); term_prompt(); return 1; } }
        s->lstage=1; term_puts("Password: "); return 1; }
    int ok=acct_get(s->luser,&a)==0&&!a.locked&&!(a.uid==0&&!a.haspw)&&acct_check(s->luser,s->lbuf)==1;
    vtc_wipe(s); s->lstage=0;
    if(!ok){ int w=acct_wait_secs(s->luser); s->luser[0]=0; klog("vt: failed console login"); term_puts(w>0?"Login incorrect - too many failed attempts, wait before trying again\n\nlogin: ":"Login incorrect\n\nlogin: "); return 1; }
    g_uid=a.uid; g_gid=a.gid; s->uid=a.uid; s->gid=a.gid; s->auth=1; sudo_forget(); klog("vt: console login"); term_prompt(); return 1;
}
static int vtc_allowed(void){ extern int g_locked; if(g_locked)return 0; return g_state==ST_DESKTOP||g_state==ST_LIVE; }
int vtc_switch(int n){
    if(n<0||n>VTC_N)return -1;
    if(n==g_vtc_active)return 0;
    if(!vtc_allowed()||(n==0&&!g_vs[0].used))return -1;
    struct vtc_slot* cur=&g_vs[g_vtc_active]; struct vtc_slot* nx=&g_vs[n];
    if(!vs_alloc(cur)||!vs_alloc(nx)){ klog("vt: out of memory for a virtual terminal"); return -1; }
    int st=g_state; vs_save(cur);
    if(g_vtc_active)cur->state=ST_LIVE;
    if(nx->used)vs_load(nx); else { nx->dcwd=g_dcwd; vs_fresh(n); vs_save(nx); }
    g_vtc_active=n; g_vtc_kdmode=VTC_KD_TEXT;
    g_state=n?ST_LIVE:g_vs[0].state;
    if(!n&&g_state!=ST_LIVE)term_geom();
    { extern void live_invalidate(void); if(g_state==ST_LIVE)live_invalidate(); }
    (void)st; klog(n?"vt: switched to a text console":"vt: back to the desktop");
    return 0;
}
int vtc_tick(void){
    static int held=0;
    if(g_vtc_active&&g_state==ST_DESKTOP){ int st=g_state; if(vtc_switch(0)==0){ g_state=st; term_geom(); return 1; } }
    if(g_vtc_req>=0){ int r=g_vtc_req; g_vtc_req=-1; return vtc_switch(r)==0; }
    if(!(kdown[0x1D]&&kdown[0x38])){ held=0; return 0; }
    int k=-1; for(int i=0;i<7;i++) if(kdown[0x3B+i]){ k=i; break; }
    if(k<0){ held=0; return 0; }
    if(held==k+1)return 0;
    held=k+1;
    int n=k==6?0:k+1;
    if(n==0&&!g_vtc_active)return 0;
    return vtc_switch(n)==0;
}
int vtc_divert(const char* b,int n){
    if(!g_vtc_active&&!g_vs[0].used)return 0;
    if(!g_js_running||g_jcur<0||g_jcur>=JMAX||n<=0)return 0;
    int pg=g_jp[g_jcur].pgid;
    for(int i=0;i<=VTC_N;i++){ struct vtc_slot* s=&g_vs[i]; if(i==g_vtc_active||!s->used||!s->tty_fg||s->tty_fg!=pg)continue;
        if(!s->pend&&!(s->pend=kmalloc(VTC_PEND)))return 1;
        if(n>VTC_PEND){ b+=n-VTC_PEND; n=VTC_PEND; }
        if(s->pn+n>VTC_PEND){ int d=s->pn+n-VTC_PEND; cpy(s->pend,s->pend+d,(u64)(s->pn-d)); s->pn-=d; }
        cpy(s->pend+s->pn,b,(u64)n); s->pn+=n; return 1; }
    return 0;
}
int vtc_dev_tty(const char* d){
    if(d[0]!='t'||d[1]!='t'||d[2]!='y')return 0;
    if(d[3]>='0'&&d[3]<='0'+VTC_N&&!d[4])return 1;
    return 0;
}
static int vtc_caller_ok(void){ if(g_jcur<0||g_jcur>=JMAX)return 0; int u=g_jp[g_jcur].uid; return u==0||u==g_uid; }
int vtc_ioctl(u64 req,u64 arg,u8* out,int* outn){
    *outn=0;
    if((req==0x5606||req==0x4B3A)&&!vtc_caller_ok())return -1;
    switch(req){
    case 0x5600: { int f=-1; for(int i=1;i<=VTC_N;i++) if(!g_vs[i].used&&i!=g_vtc_active){ f=i; break; } if(f<0)return -16; cpy(out,&f,4); *outn=4; return 0; }
    case 0x5603: { u16 v[3]; v[0]=(u16)(g_vtc_active?g_vtc_active:VTC_N+1); v[1]=0; v[2]=0; for(int i=0;i<=VTC_N;i++) if(g_vs[i].used||i==g_vtc_active)v[2]|=(u16)(1u<<(i?i:VTC_N+1)); cpy(out,v,6); *outn=6; return 0; }
    case 0x5606: { int n=(int)arg; if(n==VTC_N+1)n=0; if(n<0||n>VTC_N)return -6; if(!vtc_allowed()||(n==0&&!g_vs[0].used&&!g_vtc_active))return -1; g_vtc_req=n; return 0; }
    case 0x5607: { int n=(int)arg; if(n==VTC_N+1)n=0; return n==g_vtc_active?0:-11; }
    case 0x4B3A: if(arg!=VTC_KD_TEXT&&arg!=VTC_KD_GRAPHICS)return -22; g_vtc_kdmode=(int)arg; { extern void live_invalidate(void); if(arg==VTC_KD_TEXT)live_invalidate(); } return 0;
    case 0x4B3B: { int m=g_vtc_kdmode; cpy(out,&m,4); *outn=4; return 0; }
    }
    return -25;
}
int vtc_slot_info(int n,char* user,int cap){ if(cap>0)user[0]=0; if(n<1||n>VTC_N||!g_vs[n].used)return 0; if(!g_vs[n].auth)return 1; int i=0; for(;g_vs[n].luser[i]&&i<cap-1&&i<32;i++)user[i]=g_vs[n].luser[i]; if(cap>0)user[i]=0; return 2; }
void vtc_cmd(const char* cmd,const char* arg){
    char b[48],*p=b;
    if(cmd[0]=='t'){ if(g_vtc_active){ p=scopy(p,"/dev/tty"); p=dec(p,g_vtc_active); } else p=scopy(p,"/dev/console"); *p++='\n'; *p=0; term_puts(b); return; }
    int n=0,k=0; while(arg[k]>='0'&&arg[k]<='9'&&k<3){ n=n*10+(arg[k]-'0'); k++; }
    if(!k||arg[k]||n<1||n>VTC_N+1){ term_puts("usage: chvt N   (1-6 text consoles, 7 = desktop)\n"); return; }
    n=n==VTC_N+1?0:n; if(!vtc_allowed()||(n==0&&!g_vs[0].used&&g_vtc_active==0)){ term_puts("chvt: cannot switch now\n"); return; } g_vtc_req=n;
}
