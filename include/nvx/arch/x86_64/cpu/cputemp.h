#ifndef NVX_ARCH_X86_64_CPU_CPUTEMP_H
#define NVX_ARCH_X86_64_CPU_CPUTEMP_H

#include "include/nvx/base.h"

extern int g_ct_ok, g_ct_temp, g_ct_pkg, g_ct_tjmax, g_ct_hwp, g_ct_mhz;

void ctemp_init(void);
void ctemp_poll(void);
int ctemp_text(char* out,int cap,int what);
void ctemp_cmd(void);

#endif
