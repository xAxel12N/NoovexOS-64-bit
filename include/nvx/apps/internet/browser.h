#ifndef NVX_APPS_BROWSER_H
#define NVX_APPS_BROWSER_H

#include "include/nvx/base.h"
#include "include/nvx/drivers/video/fb/framebuffer.h"

#define PNG_RAW    (png_scratch_init(),g_png_raw)    /* 128 MiB: PNG inflate scratch (shared w/ JPEG, non-concurrent) */
#define PNG_RAW_SZ (png_scratch_init(),g_png_raw_sz)
#define WM_W 448
#define WM_H 184

extern u8 jpeg_scratch[10u*1024u*1024u];
extern u8* g_png_raw;
extern int g_png_raw_sz;
extern int g_page;
extern char g_url[2048];
extern int g_urllen , g_urlfocus;
extern int web_nl , web_scroll;
extern char backstk[16][2048];
extern int backn;
extern char g_cururl[2048];
extern int g_httplen , g_bodyoff , g_dln;
extern const unsigned char wall_cobalt_jpg[13153];
extern const int wall_cobalt_jpg_len;
extern const unsigned char wall_wire_jpg[16404];
extern const int wall_wire_jpg_len;
extern const unsigned char wall_crane_jpg[4577];
extern const int wall_crane_jpg_len;
extern const unsigned char wordmark_a[82432];
extern const int wall_porsche_jpg_len;
extern const unsigned char wall_porsche_jpg[12479];
extern const int wall_field_jpg_len;
extern const unsigned char wall_field_jpg[43908];
extern const int wall_desert_jpg_len;
extern const unsigned char wall_desert_jpg[10161];

void png_scratch_init(void);
void nvx_file_put(const char* name,const u8* data,int len);
void nvx_files_pull(void);
extern int webp_decode(const u8* data, int len, u8* out, int outcap, int* ow, int* oh);
int is_webp(const u8*d,int len);
int ci_eq(char a,char b);
int gunzip(const u8* src,int n,u8* out,int outcap);
void browser_show_html(const char* html,int len,const char* title);
void browser_go(int push);
void nvx_open_url(const char* u);
void browser_draw(int i);
void browser_click(int px,int py);
void browser_key(char c);
void browser_wheel(int sc);
int browser_tick(void);
int pz_inflate_raw(const unsigned char*src,int slen,unsigned char*out,int outcap);
int png_decode(const unsigned char*png,int len,unsigned char*raw,int rawcap,unsigned char*out,int outcap,int*ow,int*oh);
int jpeg_decode(const u8* buf,int len,u8* rgba,int rgbamax,int* ow,int* oh);

#endif
