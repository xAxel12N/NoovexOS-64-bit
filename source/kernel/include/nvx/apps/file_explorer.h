#ifndef NVX_APPS_FILE_EXPLORER_H
#define NVX_APPS_FILE_EXPLORER_H

#include "include/nvx/base.h"

extern int fx_view , fx_prev , fx_sort , fx_sdesc , fx_scroll;
extern int fx_seldir;
extern int g_renmode , g_rennode;
extern int gb_x,gb_y,gb_w,gb_h;

int fx_is_elf(int k);
void fx_go(int d);
void fx_open_file(int k);
void fx_delete_sel(void);
void fx_start_input(int mode,const char* init,int slot,int node);
void fx_rename_sel(void);
void fx_rename_commit(void);
void fx_do(int id,int val);
int fx_drop(int px,int py,int slot);
int fx_rclick(int px,int py);
void files_key(int c);
void files_draw(int i);
void files_click(int px,int py);
void about_draw(int i);
void welcome_draw(int i);

#endif
