#ifndef NVX_CORE_AUTH_FACEID_H
#define NVX_CORE_AUTH_FACEID_H
#include "include/nvx/base.h"
enum { FID_IDLE, FID_STARTING, FID_RUNNING, FID_SUCCESS, FID_FAILED, FID_ERROR };
enum { FID_VERIFY=1, FID_ENROLL=2 };
#define FID_TARGET 5
#define FID_MAX_FAILS 5
extern int g_fid_state, g_fid_mode, g_fid_count, g_fid_dist, g_fid_fails, g_fid_face[4], g_fid_fw, g_fid_fh, g_fid_seen;
extern char g_fid_msg[96]; extern const char* g_fid_hint;
int fid_has(const char* user);
int fid_delete(const char* user);
int fid_camera_present(void);
int fid_active(void);
int fid_begin(int mode,const char* user);
void fid_end(void);
int fid_tick(void);
void fid_scan_draw(int cx);
int fid_scan_cancel_hit(int x,int y);
const u32* fid_preview(int* w,int* h);
void fid_feed_set(u32* rgb,int w,int h);
int fid_dev_ready(void);
int fid_dev_read(char* buf,int len);
int fid_dev_write(const char* buf,int len);
#endif
