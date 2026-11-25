#ifndef NVX_DRIVERS_INPUT_FINGERPRINT_FPRINT_H
#define NVX_DRIVERS_INPUT_FINGERPRINT_FPRINT_H
#include "include/nvx/base.h"
enum { FPK_NONE, FPK_ELAN, FPK_SYNA_MOC, FPK_GOODIX, FPK_VALIDITY, FPK_ELAN_MOC, FPK_GOODIX_MOC, FPK_UNSUPP };
typedef struct { u16 vid, pid; u8 kind, quirk; } fp_id_t;
typedef struct { void* ctx; int (*out)(void* ctx,int ep,const u8* p,int n,int ms); int (*in)(void* ctx,int ep,u8* p,int cap,int ms); void (*sleep_ms)(int ms); } fp_xport_t;
#define FP_IMG_W 160
#define FP_IMG_HMAX 512
typedef struct { int w, h; u8 px[FP_IMG_W*FP_IMG_HMAX]; } fp_image_t;
const fp_id_t* fprint_lookup(u16 vid,u16 pid);
const char* fprint_kind_name(int kind);
int fprint_supported(int kind);
int fprint_find(u16* vid,u16* pid);
int fprint_status_text(char* out,int cap);
int elanfp_open(const fp_xport_t* x,u16 pid);
int elanfp_activate(void);
int elanfp_calibrate(void);
int elanfp_capture(fp_image_t* img,int finger_ms);
void elanfp_stop(void);
int elanfp_info(int* fw,int* w,int* h);
int fp_assemble(const u8* frames,int nf,int fw,int fh,fp_image_t* img);
int fprint_usb_capture(fp_image_t* img,int finger_ms,char* msg,int cap);
int fprint_is_moc(int kind);
enum { FPM_ENROLL=1, FPM_VERIFY, FPM_DELETE };
int fprint_usb_moc(int op,const char* uid,u8* tid,void (*prog)(int pct),char* msg,int cap);
int synafp_open(const fp_xport_t* x);
int synafp_init(void);
int synafp_complete(int id);
int synafp_result(void);
int synafp_enroll(const char* uid,int finger,void (*prog)(int pct));
int synafp_verify(const char* uid);
int synafp_delete(const char* uid,int finger);
int synafp_enroll_begin(const char* uid,int finger);
int synafp_enroll_step(int* pct);
u8 gx_crc8(const u8* d,u32 n);
u32 gx_crc32(const u8* d,u32 n);
int gx_build(u8* o,int cap,u8 c0,u8 c1,u8 seq,const u8* pl,int n);
int gx_parse(const u8* b,int g,u8* c0,const u8** body,int* blen);
int goodixfp_open(const fp_xport_t* x);
void goodixfp_set_stages(int n);
int fprint_gx_stages(u16 pid);
int goodixfp_init(void);
int goodixfp_result(void);
int goodixfp_enroll(const char* uid,void (*prog)(int pct));
int goodixfp_verify(const char* uid);
int goodixfp_delete(const char* uid,const u8* tid);
int goodixfp_delete_all(void);
const u8* goodixfp_tid(void);
int goodixfp_enroll_begin(const char* uid);
int goodixfp_enroll_step(int* pct);
int fprint_moc_enroll_begin(const char* uid,char* msg,int cap);
int fprint_moc_enroll_step(u8* tid,int* pct,char* msg,int cap);
int fprint_usb_slice(fp_image_t* img,int finger_ms,char* msg,int cap);
void fprint_usb_end(void);
#endif
