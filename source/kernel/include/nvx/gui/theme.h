#ifndef NVX_GUI_THEME_H
#define NVX_GUI_THEME_H

#include "include/nvx/base.h"
#include "include/nvx/drivers/video/framebuffer.h"

#define INK  rgb(38,46,66)
#define CYAN rgb(90,210,230)
#define GREEN rgb(120,230,150)
#define GREY rgb(150,160,180)
#define WHT  rgb(230,235,245)
#define RED  rgb(224,96,90)
enum { ST_SETUP, ST_LOGIN, ST_DESKTOP, ST_BOOTMENU, ST_RECOVERY, ST_DISKMGR, ST_INSTALL, ST_LIVE };

extern u32 ACC , ACC2;
extern int g_theme , g_showsec , g_cursor , g_accent , g_safemode;
extern int g_state;
extern char g_user[32];
extern char g_user_pw[48];
extern int g_has_pw;
extern int g_ctx , ctx_x , ctx_y;
extern char login_pass[32];
extern int login_pl;
extern int mx ,my;

unsigned nvx_pwhash(const char* s);
void line_bl(int x0,int y0,int x1,int y1,u32 c,int a,int th);
void radial(int cx,int cy,int r,u32 c,int amax);
void draw_logo(int cx,int top,int S,u32 color,int a);
void draw_cursor(void);

#endif
