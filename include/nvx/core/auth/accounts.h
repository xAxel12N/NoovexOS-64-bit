#ifndef NVX_CORE_AUTH_ACCOUNTS_H
#define NVX_CORE_AUTH_ACCOUNTS_H

#include "include/nvx/base.h"

typedef struct { char name[33]; char full[64]; char home[96]; char shell[48]; int uid, gid, admin, haspw, locked, guest; } acct_info;

extern char g_fullname[64];
extern char g_home[96];
extern int g_home_dir;
extern int g_guest_on;
extern int g_lock_min;
extern int g_locked;
extern int g_logged_in;
extern int g_sudo_min;
extern char g_acct_err[96];

void acct_boot(void);
int acct_reload(void);
int acct_valid_name(const char* name);
int acct_get(const char* name,acct_info* out);
int acct_get_uid(int uid,acct_info* out);
int acct_list(acct_info* out,int max,int loginable_only);
int acct_check(const char* name,const char* pw);
int acct_has_password(const char* name);
int acct_set_password(const char* name,const char* pw);
int acct_lock_password(const char* name);
int acct_set_locked(const char* name,int on);
int acct_create(const char* name,const char* full,const char* pw,int admin);
int acct_delete(const char* name,int remove_home);
int acct_set_admin(const char* name,int on);
int acct_set_full(const char* name,const char* full);
int acct_is_admin_name(const char* name);
int acct_is_admin(int uid);
int acct_in_group(const char* name,const char* group);
int acct_groups(const char* name,char* out,int cap);
int acct_group_add(const char* name,const char* group);
int acct_group_del(const char* name,const char* group);
int acct_group_gid(const char* group);
int acct_gids(int uid,int* out,int max);
int acct_setup_primary(const char* name,const char* full,const char* pw,const char* rootpw,const char* host);
int acct_set_hostname(const char* host);
int acct_valid_host(const char* host);
int acct_login(const char* name);
int acct_login_ex(const char* name,int quiet);
void acct_logout(void);
void acct_log(const char* a,const char* b,const char* c);
void acct_wtmp(const char* kind,const char* user);
int acct_last(char* out,int cap);
int acct_admin_count(void);
const char* acct_home_of_current(void);
void acct_fmt_time(u32 t,char* out);
void acct_wipe_guest(void);
void acct_profile_name(const char* base,char* out,int cap);

#endif
