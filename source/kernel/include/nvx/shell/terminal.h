#ifndef NVX_SHELL_TERMINAL_H
#define NVX_SHELL_TERMINAL_H

#include "include/nvx/base.h"

#define TRMAX 72
#define TCMAX 240
#define TPAD 14
#define TLH 17
#define TSB_N 400

extern char TCH[TRMAX][TCMAX];
extern u32 TCO[TRMAX][TCMAX];
extern int trows ,tcols ,tcx ,tcy;
extern u32 tcur;
extern int g_tth , g_tfont , g_tcur , g_tstatus , g_tsb_off , g_topa , g_tblur;
extern char TSB[TSB_N][TCMAX];
extern u32 TSBC[TSB_N][TCMAX];
extern int tsb_head , tsb_n;
extern char tline[256];
extern int tlen;
extern int g_pwprompt;
extern char g_pwssid[33];
extern int g_pwbyid;
extern volatile unsigned g_term_gen;
extern char g_cap[8192];
extern int g_cap_n;
extern int g_capture;

void term_geom(void);
void term_clear(void);
void term_scroll_up(void);
void term_putc(char c);
void term_puts(const char*s);
void tcol(u32 c);
void term_prompt(void);

#endif
