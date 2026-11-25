#ifndef NVX_SHELL_VTCON_H
#define NVX_SHELL_VTCON_H

#include "include/nvx/base.h"

#define VTC_N 6
#define VTC_KD_TEXT 0
#define VTC_KD_GRAPHICS 1

extern int g_vtc_active;
extern int g_vtc_kdmode;

int vtc_switch(int n);
int vtc_tick(void);
int vtc_ioctl(u64 req,u64 arg,u8* out,int* outn);
int vtc_dev_tty(const char* d);
int vtc_divert(const char* b,int n);
void vtc_cmd(const char* cmd,const char* arg);
int vtc_login_key(char c);
void sudo_forget(void);
u64 sudo_until_get(void);
void sudo_until_set(u64 v);

#endif
