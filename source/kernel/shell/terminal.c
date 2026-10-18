#include "include/nvx/base.h"
#include "include/nvx/arch/x86_64/percpu.h"
#include "include/nvx/lib/fmt.h"
#include "include/nvx/drivers/video/framebuffer.h"
#include "include/nvx/arch/x86_64/cpuinfo.h"
#include "include/nvx/net/http.h"
#include "include/nvx/gui/theme.h"
#include "include/nvx/gui/wm.h"
#include "include/nvx/shell/terminal.h"
#include "include/nvx/fs/dirtree.h"
#include "include/nvx/core/syscall.h"
#include "include/nvx/fs/unixfs.h"
/* =========================================================================
   Terminal + shell
   ========================================================================= */

char TCH[TRMAX][TCMAX]; u32 TCO[TRMAX][TCMAX]; u32 TBG[TRMAX][TCMAX];
int trows=0,tcols=0,tcx=0,tcy=0; u32 tcur=0;

int g_tth=5, g_tfont=1, g_tcur=0, g_tstatus=1, g_tsb_off=0, g_topa=80, g_tblur=1;
char TSB[TSB_N][TCMAX]; u32 TSBC[TSB_N][TCMAX]; int tsb_head=0, tsb_n=0;
char tline[256]; int tlen=0;
int g_pwprompt=0; char g_pwssid[33]; int g_pwbyid=0;

   /* fwd */
void term_geom(void){ int sb=g_tstatus?26:0; tcols=(WN[W_TERM].w-2*TPAD-12)/8; if(tcols>TCMAX)tcols=TCMAX; if(tcols<10)tcols=10;
    trows=(WN[W_TERM].h-TITLEH-2*TPAD-sb+4)/TLH; if(trows>TRMAX)trows=TRMAX; if(trows<3)trows=3; }
volatile unsigned g_term_gen=0;
void term_clear(void){ g_term_gen++; for(int r=0;r<TRMAX;r++)for(int c=0;c<TCMAX;c++){TCH[r][c]=' ';TCO[r][c]=WHT;TBG[r][c]=0;} tcx=tcy=0; tsb_n=0; tsb_head=0; g_tsb_off=0; }
void term_scroll_up(void){ for(int c=0;c<TCMAX;c++){ TSB[tsb_head][c]=(c<tcols)?TCH[0][c]:' '; TSBC[tsb_head][c]=TCO[0][c]; } tsb_head=(tsb_head+1)%TSB_N; if(tsb_n<TSB_N)tsb_n++; if(g_tsb_off>0&&g_tsb_off<tsb_n)g_tsb_off++;
    for(int r=1;r<trows;r++)for(int c=0;c<TCMAX;c++){TCH[r-1][c]=TCH[r][c];TCO[r-1][c]=TCO[r][c];TBG[r-1][c]=TBG[r][c];}
    for(int c=0;c<TCMAX;c++){TCH[trows-1][c]=' ';TCO[trows-1][c]=WHT;TBG[trows-1][c]=0;} }
static void term_nl(void){ g_term_gen++; tcx=0; if(++tcy>=trows){term_scroll_up();tcy=trows-1;} }
char g_cap[8192]; int g_cap_n=0; int g_capture=0;
void term_putc(char c){
    if(g_capture){ if(g_cap_n<8191)g_cap[g_cap_n++]=c; g_cap[g_cap_n]=0; return; }
    g_term_gen++;
    if(tcy>=trows)tcy=trows-1;
    if(c=='\n'){term_nl();return;} if(c=='\b'){if(tcx>=tcols)tcx=tcols; if(tcx>0){tcx--;TCH[tcy][tcx]=' ';TBG[tcy][tcx]=0;}return;}
    if(tcx>=tcols)term_nl(); TCH[tcy][tcx]=c; TCO[tcy][tcx]=tcur; TBG[tcy][tcx]=0; tcx++; }
void term_puts(const char*s){ while(*s)term_putc(*s++); }

void tcol(u32 c){ tcur=c; }
static char g_shcwd[128]="/home";

void term_prompt(void){ char pb[192]; dt_init(); dt_path(g_dcwd,pb); tcol(GREEN);term_puts(cur_user());term_putc('@');term_puts(g_hostname);tcol(GREY);term_puts(":");tcol(rgb(120,200,255));term_puts(pb);tcol(GREY);term_puts(g_uid==0?"# ":"$ ");tcol(WHT); }
static const char *LOGO[7]={"##    ##","###   ##","####  ##","## ## ##","##  ####","##   ###","##    ##"};
static void neofetch(void){
    char info[9][80]; int n=0; char*p;
    p=info[n++];p=scopy(p,"noovex@noovex64");*p=0;
    p=info[n++];p=scopy(p,"---------------");*p=0;
    p=info[n++];p=scopy(p,"OS: NoovexOS 64 v9.82");*p=0;
    p=info[n++];p=scopy(p,"Kernel: x86-64 long mode");*p=0;
    p=info[n++];p=scopy(p,"CPU: ");p=scopy(p,g_brand);*p=0;
    p=info[n++];p=scopy(p,"Display: ");p=udec(p,W);*p++='x';p=udec(p,H);p=scopy(p," @ ");p=udec(p,BPP);p=scopy(p,"bpp");*p=0;
    p=info[n++];p=scopy(p,"WM: Noovex desktop (dock)");*p=0;
    p=info[n++];p=scopy(p,"Uptime: ");p=udec(p,(u32)(g_ticks/100));*p++='s';*p=0;
    p=info[n++];p=scopy(p,"Shell: nsh");*p=0;
    int rows=n>7?n:7;
    for(int r=0;r<rows;r++){ tcol(ACC); term_puts(r<7?LOGO[r]:"        "); term_puts("  ");
        if(r<n){ char*s=info[r]; if(r>=2){ tcol(CYAN); while(*s&&*s!=':')term_putc(*s++); tcol(WHT); while(*s)term_putc(*s++);} else { tcol(GREEN); term_puts(s);} }
        term_putc('\n'); }
    tcol(WHT);
}
