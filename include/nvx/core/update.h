#ifndef NVX_CORE_UPDATE_H
#define NVX_CORE_UPDATE_H

#include "include/nvx/base.h"

enum { UPS_NONE, UPS_OK, UPS_PREP, UPS_SWAP, UPS_TRY0, UPS_TRY1, UPS_TRY2, UPS_FAIL, UPS_OLD };

#define UPD_NOTES 8

typedef struct {
    int state, installed, usb, probed, checked, avail, busy, trial, has_old, has_new, want_check, notice, sr_missing, sr_tried;
    unsigned long long pbase, pn;
    long iso_bytes, old_bytes, new_bytes;
    unsigned long long checked_at, trial_t0;
    char cur[16], latest[16], newv[16], oldv[16], published[24];
    char why[160], err[120], ntext[160];
    char notes[UPD_NOTES][120]; int nnotes;
    char iso_url[320], sha[72];
} upd_info;

extern upd_info g_upd;

int upd_vnum(const char* v);
int upd_check(void);
int upd_install(int force);
int upd_go_back(void);
int upd_use_new(void);
int upd_refresh(void);
int upd_tick(void);
void upd_server(char* out,int cap);
void upd_cmd(const char* args);
const char* upd_state_name(int st);
int upd_kcheck(const u8* k,u32 n,char* ver);
void upd_screen_begin(const char* l1);
void upd_screen(int pct,const char* detail);
void upd_screen_end(void);
int upd_repair(void);
int upd_put_sysroot_part(unsigned long long base,unsigned long long n);

#endif
