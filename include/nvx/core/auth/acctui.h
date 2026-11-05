#ifndef NVX_CORE_AUTH_ACCTUI_H
#define NVX_CORE_AUTH_ACCTUI_H

#include "include/nvx/base.h"
#include "include/nvx/core/auth/accounts.h"

void acct_field(int x,int y,int w,const char* label,const char* val,int len,int mask,int focus);
void acct_button(int cx,int by,const char* t,int foc,int* rx,int* ry,int* rw,int* rh);
void acct_check_row(int x,int y,const char* label,int on,int focus);
int acct_dlg_active(void);
void acct_dlg_draw(void);
void acct_dlg_key(int c);
void acct_dlg_click(int x,int y);
void acct_dlg_open_pw_self(void);
void acct_dlg_open_name(void);
void acct_dlg_open_add(void);
void acct_dlg_open_del(const char* name);
void acct_dlg_open_pw_other(const char* name);
void acct_ui_lock(void);
void acct_ui_signout(void);
void acct_ui_end_session(void);
void acct_ui_unlocked(void);
void acct_ui_session_started(void);
void acct_idle_tick(void);
int acct_ui_others(acct_info* out,int max);
int acct_ui_click(int id);
int acct_switch_mode(void);
void acct_set_switch_mode(int on);

#endif
