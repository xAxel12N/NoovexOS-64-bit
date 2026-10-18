#ifndef NVX_APPS_EDITOR_H
#define NVX_APPS_EDITOR_H

#include "include/nvx/base.h"

extern int ed_nl , ed_cx , ed_cy , ed_top , ed_file , ed_dirty;
extern int eb_save_x,eb_save_y,eb_new_x,eb_new_y,eb_bw ,eb_bh;
extern int eb_run_x,eb_run_y,eb_conx_x,eb_conx_y;
extern int ed_con_n , ed_show_con;
extern char g_clip[1024];
extern int g_clip_len;

void editor_run(void);
void pyide_open(void);
int pyide_installed(void);
extern int ed_py;
void editor_reset(void);
void editor_open(int fsidx);
void editor_open_buf(const char*d,int len);
void ed_load_usb(int fi);
void editor_save(void);
void editor_key(char c);
void editor_draw(int i);

#endif
