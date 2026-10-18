#ifndef NVX_GUI_WM_H
#define NVX_GUI_WM_H

#include "include/nvx/base.h"

enum { W_WELCOME, W_TERM, W_FILES, W_EDITOR, W_CALC, W_BROWSER, W_CLOCK, W_SYSMON, W_CALENDAR, W_SNAKE, W_FLAPPY, W_NEOFETCH, W_PAINT, W_DOOM, W_SETTINGS, W_ABOUT, W_CHATGPT, W_PPTX, W_LOGS, W_TFIND, W_VIDEO, W_XDISP, W_CAMERA, W_BENCH, W_RECORDER, W_WRITER, W_QUAKE, W_GPU3D, NWIN };
typedef struct{ int x,y,w,h,open,z,min,max,sx,sy,sw,sh,ws; } Win;
#define NWS 4
typedef struct{ char title[40]; char body[72]; u64 t; int shown; } Notif;
#define NOTIF_MAX 32
#define TITLEH 30

extern Win WN[NWIN];
extern int g_ws;
extern Notif g_notif[NOTIF_MAX];
extern int g_notif_n;
extern u64 g_toast_until;
extern int g_toast_cur;
extern int g_notifpanel;
extern int focus , ztop , drag , dox , doy;
extern int g_ctxkind , g_ctxwin;
extern int g_tctx_type , g_tctx_val;
extern int g_dctx_item;
extern int g_prop , g_prop_app , g_prop_slot;
extern int g_submenu , g_sub_x , g_sub_y , g_subn;
extern int g_fdrag , g_fdrag_slot , g_fdrag_x , g_fdrag_y;
extern const char* g_subit[8];
extern char g_subparent[16];
extern int g_snap , g_resize , r_sw0 , r_sh0 , r_mx0 , r_my0 , r_sx0 , r_sy0 , g_rflags , g_rz_hover;
int win_edge_hit(int px,int py,int* win);
extern int g_launcher , g_alttab , g_altsel , g_altlist[NWIN], g_altn , alt_prev;
extern int g_wallmode , wall_custom_slot , g_clockpop , g_qs;
extern int g_rename , g_renslot , g_renlen;
extern char g_renbuf[64];
extern int g_clockx , g_qsx;
extern int g_marq , marq_x0 , marq_y0;
extern char toast_msg[80];
extern u64 toast_t;
extern const char *APPNAME[NWIN];

void notify(const char* title,const char* body);
void toast(const char*m);
void open_win(int i);
void close_win(int i);
void focus_win(int i);
void win_min(int i);
void win_max(int i);
void apply_snap(int i,int s);
int win_ctl_hit(int i,int px,int py);
void win_frame(int i,const char*title);
int in_rect(int px,int py,int x,int y,int w,int h);

#endif
