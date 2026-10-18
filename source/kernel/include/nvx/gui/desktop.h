#ifndef NVX_GUI_DESKTOP_H
#define NVX_GUI_DESKTOP_H

#include "include/nvx/base.h"
#include "include/nvx/drivers/video/framebuffer.h"
#include "include/nvx/fs/ramfs.h"

#define DOCK_IC 48
#define DOCK_GAP 14
#define DOCK_PAD 14
#define DGX0 24
#define DGY0 44
#define DGW  92
#define DGH  94
#define CURBB 128

extern int IC_APP[12];
extern int DOCK_x,DOCK_y,DOCK_w,DOCK_h;
extern int g_bellx ,g_wsx;
extern int g_tby,g_tbh,g_lbx_end;
extern int g_dstyle;
void classic_taskbtn_click(int px);
int classic_qs_click(int mx0,int my0);
int classic_tray_click(int px);
extern int g_dnd,g_nightlight,g_tray_x0;
extern int bat_present , bat_pct , bat_charging , bat_via_ec;
extern int g_apx[5], g_apy[5], g_apinit;
extern int g_fpx[FS_N], g_fpy[FS_N];
extern u8 g_fpl[FS_N];
extern int g_ddrag , g_ddx , g_ddy , g_ddsx , g_ddsy;
extern char g_lq[40];
extern int g_lqn;
extern u32* SCENE_NC;
extern int cxlast , cylast;

void wall_thumbs_build(void);
void build_bg(void);
int ec_read(u8 off,u8* out);
int ec_detect(void);
void battery_init(void);
void battery_tick(void);
void wifi_gui_connect(int idx);
int nic_index_wifi(void);
u32 rtw_find_mmio(void);
void wifi_gui_scan(int full);
void ws_switch(int w);
void desk_place_file(int slot);
void launch_icon(int w,int x,int y);
void prop_draw(void);
int desk_hit(int px,int py);
void desk_open(int it);
void launcher_search(void);
int launcher_click(int px,int py);
int launcher_key(char c);
void blit_region(int x,int y,int w,int h);
void nc_restore(int x,int y,int w,int h);
void compose_present(void);
void present_topbar(void);
void present_cursor(void);
void ui_pump(void);
void win_anim(int i,int opening);

#endif
