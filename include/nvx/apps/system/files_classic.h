#ifndef NVX_APPS_FILES_CLASSIC_H
#define NVX_APPS_FILES_CLASSIC_H

#include "include/nvx/base.h"
#include "include/nvx/fs/vfs/ramfs.h"

struct nvx_FTREE_t { const char* name; short parent; };
#define FTREE_N ((int)(sizeof(FTREE)/sizeof(FTREE[0])))
struct nvx_FFILE_t { short node; const char* name; const char* text; };
#define FFILE_N ((int)(sizeof(FFILE)/sizeof(FFILE[0])))
#define ZIP_MAXE 128
struct nvx_zent_t { char name[64]; u32 usize, csize, off; int method; };

extern int fl_y[FS_N],fl_idx[FS_N],fl_n ,fl_x,fl_w;
extern int g_fsel;
extern int fpv_edx,fpv_edy,fpv_edw,fpv_edh;
extern int g_filesnav;
extern const struct nvx_FTREE_t FTREE[20];
extern int g_files_tree , g_cwd;
extern int ftrow_y[24], ftrow_node[24], ftrow_isfile[24], ftrow_n;
extern const struct nvx_FFILE_t FFILE[25];
extern u8 viewbuf[6*1024*1024];
extern int view_w , view_h , view_slot;
extern int g_imgview;
extern struct nvx_zent_t zent[ZIP_MAXE];
extern int znent , zip_slot , zsel , ze_y[ZIP_MAXE], ze_n;
extern u32 g_ram_mb;
extern int g_drv_x[3],g_drv_y[3],g_drv_w,g_drv_tgt[3],g_drv_n;

void mini_doc(int x,int y);
int files_show(int folder);
int fext_eq(const char*name,const char*e);
void zip_open(int slot);
int zip_extract(int slot,int idx,u8* out,int cap);
void pptx_load(int slot);
void pptx_draw(int i);
void pptx_key(int c);
void pptx_click(int px,int py);
const char* file_type_name(const char*name);
int file_kind(const char*name);
int fkind(int slot);
int ensure_img(int slot);
void blit_img_fit(int ox,int oy,int aw,int ah);
void img_viewer_draw(void);
u32 detect_ram_mb(void);
char* fmt_size(char* p,long b);
void draw_drive_tile(int x,int y,int w,int h,const char* name,long used,long total,int cloud,int hov);

#endif
