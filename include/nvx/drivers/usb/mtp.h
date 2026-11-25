#ifndef NVX_DRIVERS_USB_MTP_H
#define NVX_DRIVERS_USB_MTP_H
#include "include/nvx/base.h"
typedef struct { int (*out)(const u8* b,int n,int ms); int (*in)(u8* b,int n,int ms); int (*reset)(void); int max; } mtp_tr_t;
typedef struct { int iface, cls, in_ep, in_mps, out_ep, out_mps, int_ep, istr; } mtp_if_t;
typedef struct { u32 id; u64 cap, freeb; int ro; char desc[40]; } mtp_store_t;
enum { MTP_E_IO=-1, MTP_E_TIMEOUT=-2, MTP_E_PROTO=-3, MTP_E_NOENT=-4, MTP_E_LOCKED=-5, MTP_E_RO=-6, MTP_E_NOTEMPTY=-7, MTP_E_ARG=-8, MTP_E_NODEV=-9, MTP_E_EXIST=-10, MTP_E_BIG=-11 };
enum { MTP_K_NONE=0, MTP_K_PHONE=1, MTP_K_CAMERA=2 };
#define MTP_MAXST 4
#define MTP_MAXOPS 96
typedef struct { int up, kind, locked, nst, nops, fails, dead; u16 vid, pid, ops[MTP_MAXOPS]; u32 tid, gen, errs, timeouts, lastrc; char model[40], maker[32]; mtp_store_t st[MTP_MAXST]; } mtp_dev_t;
typedef void (*mtp_lscb)(const char* name,int isdir,unsigned long long size,void* ctx);
extern mtp_dev_t g_mtp;
int mtp_match(const u8* cfg,int n,mtp_if_t* o);
int mtp_str_is_mtp(const u8* s,int n);
int mtp_android_vid(u16 vid);
int mtp_open(const mtp_tr_t* tr,u16 vid,u16 pid);
void mtp_close(void);
int mtp_refresh(void);
int mtp_has_op(u16 op);
int mtp_ls(int s,const char* path,mtp_lscb cb,void* ctx);
long mtp_size(int s,const char* path);
long mtp_read(int s,const char* path,char* buf,long cap);
long mtp_write(int s,const char* path,const char* buf,long len);
int mtp_mkdir(int s,const char* path);
int mtp_delete(int s,const char* path);
int mtp_rename(int s,const char* from,const char* to);
const char* mtp_kind_name(void);
const char* mtp_why(int e);
int mtp_info(char* out,int cap);
#endif
