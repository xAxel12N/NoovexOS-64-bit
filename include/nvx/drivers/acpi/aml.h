#ifndef NVX_DRIVERS_ACPI_AML_H
#define NVX_DRIVERS_ACPI_AML_H

#include "include/nvx/base.h"

enum { AT_NONE=0, AT_INT, AT_STR, AT_BUF, AT_PKG, AT_REF, AT_NAMESTR };
enum { RK_NODE=0, RK_PKG, RK_BUF, RK_SLOT };
typedef struct aobj { u8 type, rk, own; u32 len; u64 i; u8* p; struct aobj* e; struct anode* node; struct aobj* cont; u32 idx; } aobj;
enum { K_SCOPE=1, K_DEVICE, K_NAME, K_METHOD, K_REGION, K_FIELD, K_INDEXFIELD, K_BANKFIELD, K_BUFFIELD, K_MUTEX, K_EVENT, K_PROCESSOR, K_POWER, K_THERMAL, K_ALIAS, K_NATIVE };
typedef struct anode {
    char nm[4]; struct anode *parent, *child, *next; u8 kind, temp;
    aobj v;
    const u8* code; u32 clen; u8 argc, mflags; int (*native)(aobj* args,int n,aobj* ret);
    u8 space; u64 base, rlen;
    struct anode *region, *idxf, *dataf, *bankf, *target; u64 bankval; u32 bitoff, bitlen; u8 acc, lock, upd, cfld;
    aobj* bsrc; struct anode* bnode;
} anode;

extern anode* g_aroot;
extern int g_aml_rev , g_aml_ok , g_aml_err;
extern u32 g_aml_nodes , g_aml_loaded_bytes;
extern char g_aml_lasterr[64];
extern int (*aml_rd_hook)(u8 space,u64 addr,int width,u64* val,anode* region);
extern int (*aml_wr_hook)(u8 space,u64 addr,int width,u64 val,anode* region);
extern void (*aml_sleep_hook)(u32 ms);
extern u64 (*aml_timer_hook)(void);
extern u64 (*aml_now_hook)(void);
extern void (*aml_stall_hook)(u32 us);
extern int (*aml_gl_hook)(int acquire);
extern u64 g_aml_deadline , g_aml_hard_deadline;
extern u32 g_aml_time_ms , g_aml_sleep_pool , g_aml_stall_us;

int aml_gl(int acq);
void aobj_free(aobj* o);
void aobj_int(aobj* o,u64 v);
u64 aobj_to_int(const aobj* o);
int aml_deref(aobj* r,aobj* out);
anode* aml_find(const char* path);
anode* aml_child(anode* n,const char* seg4);
int aml_eval(anode* n,aobj* args,int nargs,aobj* out);
int aml_eval_int(anode* n,u64* v);
int aml_init_ns(void);
int aml_load_table(const u8* aml,u32 len);
int aml_sta(anode* dev);
void aml_run_ini(anode* n,int depth);
void aml_run_reg(anode* n,int depth,u32 spaces);
int aml_notify_pop(anode** n,u32* v);
int aml_hid_is(anode* dev,const char* id);
int aml_find_devices(anode* n,const char* id,anode** out,int max,int cnt,int depth);
void aml_path(anode* n,char* out,int cap);

#endif
