#ifndef NVX_INIT_MAIN_H
#define NVX_INIT_MAIN_H

#include "include/nvx/base.h"

extern char __immutable_end[];
extern int g_sig_ok;
extern int g_sig_tampered;
extern int g_ticks_live;

int sig_self_check(void);
void kmain(void);

#endif
