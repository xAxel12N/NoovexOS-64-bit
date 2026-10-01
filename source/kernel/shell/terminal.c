/* =========================================================================
   Terminal + shell
   ========================================================================= */
#define TRMAX 40
#define TCMAX 120
static char TCH[TRMAX][TCMAX]; static u32 TCO[TRMAX][TCMAX];
static int trows=0,tcols=0,tcx=0,tcy=0; static u32 tcur=0;
#define TPAD 14
#define TLH 17
#define TSB_N 400
static int g_tth=5, g_tfont=1, g_tcur=0, g_tstatus=1, g_tsb_off=0, g_topa=80, g_tblur=1;
static char TSB[TSB_N][TCMAX]; static u32 TSBC[TSB_N][TCMAX]; static int tsb_head=0, tsb_n=0;
static char tline[128]; static int tlen=0;
static int g_pwprompt=0; static char g_pwssid[33]; static int g_pwbyid=0;
static void wpa_connect_saved(u32 m,int idx,const char* pw);
static void editor_open(int fsidx);   /* fwd */
static void term_geom(void){ int sb=g_tstatus?26:0; tcols=(WN[W_TERM].w-2*TPAD-12)/8; if(tcols>TCMAX)tcols=TCMAX; if(tcols<10)tcols=10;
    trows=(WN[W_TERM].h-TITLEH-2*TPAD-sb+4)/TLH; if(trows>TRMAX)trows=TRMAX; if(trows<3)trows=3; }
static void term_clear(void){ for(int r=0;r<TRMAX;r++)for(int c=0;c<TCMAX;c++){TCH[r][c]=' ';TCO[r][c]=WHT;} tcx=tcy=0; tsb_n=0; tsb_head=0; g_tsb_off=0; }
static void term_scroll(void){ for(int c=0;c<TCMAX;c++){ TSB[tsb_head][c]=(c<tcols)?TCH[0][c]:' '; TSBC[tsb_head][c]=TCO[0][c]; } tsb_head=(tsb_head+1)%TSB_N; if(tsb_n<TSB_N)tsb_n++; if(g_tsb_off>0&&g_tsb_off<tsb_n)g_tsb_off++;
    for(int r=1;r<trows;r++)for(int c=0;c<tcols;c++){TCH[r-1][c]=TCH[r][c];TCO[r-1][c]=TCO[r][c];}
    for(int c=0;c<tcols;c++){TCH[trows-1][c]=' ';TCO[trows-1][c]=WHT;} }
static void term_nl(void){ tcx=0; if(++tcy>=trows){term_scroll();tcy=trows-1;} }
static char g_cap[8192]; static int g_cap_n=0; static int g_capture=0;
static void term_putc(char c){
    if(g_capture){ if(g_cap_n<8191)g_cap[g_cap_n++]=c; g_cap[g_cap_n]=0; return; }
    if(c=='\n'){term_nl();return;} if(c=='\b'){if(tcx>0){tcx--;TCH[tcy][tcx]=' ';}return;}
    if(tcx>=tcols)term_nl(); TCH[tcy][tcx]=c; TCO[tcy][tcx]=tcur; tcx++; }
static void term_puts(const char*s){ while(*s)term_putc(*s++); }

static void tcol(u32 c){ tcur=c; }
static char g_shcwd[128]="/home";
static void dt_init(void); static void dt_path(int node,char* out); extern int g_dcwd;
static void term_prompt(void){ char pb[192]; dt_init(); dt_path(g_dcwd,pb); tcol(GREEN);term_puts(cur_user());term_putc('@');term_puts(g_hostname);tcol(GREY);term_puts(":");tcol(rgb(120,200,255));term_puts(pb);tcol(GREY);term_puts(g_uid==0?"# ":"$ ");tcol(WHT); }
static const char *LOGO[7]={"##    ##","###   ##","####  ##","## ## ##","##  ####","##   ###","##    ##"};
static void neofetch(void){
    char info[9][80]; int n=0; char*p;
    p=info[n++];p=scopy(p,"noovex@noovex64");*p=0;
    p=info[n++];p=scopy(p,"---------------");*p=0;
    p=info[n++];p=scopy(p,"OS: NoovexOS 64 v8.13");*p=0;
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
