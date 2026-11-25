#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include "include/nvx/base.h"
#include "include/nvx/shell/terminal.h"
#include "include/nvx/shell/vt.h"
char TCH[TRMAX][TCMAX]; u32 TCO[TRMAX][TCMAX]; u32 TBG[TRMAX][TCMAX];
int tcx,tcy,trows=40,tcols=120; volatile unsigned g_term_gen;
void term_scroll_up(void){}
void term_putc(char c){(void)c;}
void vt_reply(const char* s){(void)s;}
static int fail_n=0, fail_after=-1;
void* kmalloc(unsigned long n){ if(fail_after>=0&&fail_n++==fail_after)return 0; return malloc(n); }
static void out(const char* s){ int n=0; while(s[n])n++; vt_out(s,n); }
int main(void){
    clock_t t0=clock(); out("\033[99999S\033[99999T\033[5;10r\033[99999S\033[99999T"); double dt=(double)(clock()-t0)/CLOCKS_PER_SEC;
    printf("scroll flood: %.3fs\n",dt); if(dt>1.0){ printf("FAIL slow\n"); return 1; }
    out("\033[r\033[H");
    fail_after=1; out("\033[?1049h"); out("\033[?1049h"); out("x"); out("\033[?1049l");
    printf("alt-screen partial alloc: OK\n"); return 0; }
