#include <stdio.h>
#include <string.h>
typedef unsigned int u32; typedef unsigned char u8; typedef unsigned long long u64;
static int bad=0;
#define T(c) do{ if(!(c)){ printf("FAIL %s line %d\n",#c,__LINE__); bad++; } }while(0)
#define RUN_MAX 120
#define RUN_HIST 8
#define W_TERM 1
static int seq(const char* a,const char* b){ return !strcmp(a,b); }
static char* scopy(char* d,const char* s){ while(*s)*d++=*s++; *d=0; return d; }
static char last_cmd[256]; static int runs, opened, prompts, puts_n;
static void open_win(int w){ opened=w; }
static void term_puts(const char* s){ (void)s; puts_n++; }
static void term_putc(char c){ (void)c; }
static void term_prompt(void){ prompts++; }
static void run_cmd(char* s){ runs++; strcpy(last_cmd,s); }
static void in_mark(const char* a,int b,const char* c,int d){ (void)a;(void)b;(void)c;(void)d; }
static int g_tty_fg=0;
#include "rundlg_ext.c"
static void type(const char* s){ for(;*s;s++)run_key(*s); }
int main(void){
    run_open(); T(g_run_on==1&&run_len==0);
    type("  ls -l  "); run_key('\n'); T(g_run_on==0); T(runs==1); T(!strcmp(last_cmd,"ls -l")); T(opened==W_TERM); T(prompts==1);
    run_open(); type("abc"); run_key(17); run_key(17); run_key('X'); T(!strcmp(run_buf,"aXbc")); run_key('\b'); T(!strcmp(run_buf,"abc")); run_key(14); T(!strcmp(run_buf,"ac")); run_key(2); run_key(14); T(!strcmp(run_buf,"c")); run_key(5); run_key('\b'); T(run_len==0);
    run_key(27); T(g_run_on==0); T(runs==1);
    run_open(); run_key('\n'); T(g_run_on==0); T(runs==1);
    run_open(); type("neofetch"); run_key('\n'); T(runs==2&&!strcmp(last_cmd,"neofetch"));
    run_open(); run_key(19); T(!strcmp(run_buf,"neofetch")); run_key(19); T(!strcmp(run_buf,"ls -l")); run_key(19); T(!strcmp(run_buf,"ls -l")); run_key(20); T(!strcmp(run_buf,"neofetch")); run_key(20); T(run_len==0); run_key(27);
    run_open(); type("neofetch"); run_key('\n'); T(run_hn==2);
    for(int i=0;i<12;i++){ char b[8]; snprintf(b,sizeof b,"c%d",i); run_open(); type(b); run_key('\n'); } T(run_hn==RUN_HIST); T(!strcmp(run_hist[RUN_HIST-1],"c11")); T(!strcmp(run_hist[0],"c4"));
    run_open(); for(int i=0;i<200;i++)run_key('a'); T(run_len==RUN_MAX); run_key(27);
    printf(bad?"RUNDLG %d failures\n":"RUNDLG all ok\n",bad); return bad?1:0;
}
