#ifndef NVX_APPS_SETTINGS_H
#define NVX_APPS_SETTINGS_H

#include "include/nvx/base.h"

#define NWALL 6
extern const char *THN[NWALL];
extern u32 wall_thumb[NWALL][64*40];
extern int wall_thumb_ready;
extern const u32 THSW[NWALL];
extern const u32 ACV[5];
extern int g_refresh_hz , g_panel_oled , g_mouse_accel , g_fps_overlay;
extern int g_setcat;
extern int g_wifipop , g_wifix;
extern u32 g_wifi_mmio;
extern int g_wifi_sel;
extern char g_wifi_pw[64];
extern int g_wifi_pwlen;
extern int g_wifi_entry;
extern char g_wifi_conn[33];
extern int g_wifi_scanning;
extern char g_wifi_status[48];

void nvx_settings_save(void);
void nvx_settings_load(void);
void settings_draw(int i);
void settings_click(int px,int py);

#endif
