#ifndef NVX_CORE_FOCUS_H
#define NVX_CORE_FOCUS_H
#include "include/nvx/base.h"
#define FOCUS_MAX 12
typedef struct {
    char id[16], name[24]; int icon; u32 color; int builtin, used;
    int hide_popups, mute, hide_games, lock1, show_tb, show_lock;
    int nl, bright, bsave; char theme[16]; char allow[96];
    int sch_on, sch_from, sch_to, sch_days; u32 app_mask; int on_login, low_batt;
} focus_mode;
extern int g_focus_cur;
void focus_boot(void);
void focus_tick(void);
int focus_count(void);
focus_mode* focus_get(int i);
int focus_find(const char* s);
int focus_set(int i,int why);
int focus_allows(const char* app,const char* title);
int focus_popups_hidden(void);
int focus_hides_games(void);
int focus_create(const char* name,int icon,u32 color);
int focus_delete(int i);
void focus_save(int i);
void focus_app_started(int w);
void focus_app_closed(int w);
void focus_signed_in(void);
void focus_logout(void);
const char* focus_icon_name(int k);
const char* focus_app_preset_name(u32 mask);
u32 focus_app_preset_next(u32 mask);
const char* focus_theme_next(const char* cur);
int focus_why(void);
#endif
