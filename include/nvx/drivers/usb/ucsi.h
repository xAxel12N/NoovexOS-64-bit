#ifndef NVX_DRIVERS_USB_UCSI_H
#define NVX_DRIVERS_USB_UCSI_H
#ifndef UCSI_HOST
#include "include/nvx/base.h"
#endif

#define UCSI_MAXC 8
#define UCSI_VERSION 0
#define UCSI_CCI 4
#define UCSI_CONTROL 8
#define UCSI_MSGIN 16
#define UCSI_CCI_CHG(c) (((c)>>1)&0x7F)
#define UCSI_CCI_LEN(c) (((c)>>8)&0xFF)
#define UCSI_CCI_NOTSUP (1u<<25)
#define UCSI_CCI_CANCEL (1u<<26)
#define UCSI_CCI_RESET (1u<<27)
#define UCSI_CCI_BUSY (1u<<28)
#define UCSI_CCI_ACK (1u<<29)
#define UCSI_CCI_ERR (1u<<30)
#define UCSI_CCI_DONE (1u<<31)
enum { UCSI_PPM_RESET=1, UCSI_ACK_CC_CI=4, UCSI_GET_CAPABILITY=6, UCSI_GET_CONNECTOR_CAPABILITY=7, UCSI_GET_ALTERNATE_MODES=0x0C, UCSI_GET_CURRENT_CAM=0x0E, UCSI_GET_PDOS=0x10, UCSI_GET_CONNECTOR_STATUS=0x12, UCSI_GET_ERROR_STATUS=0x13 };
enum { UCSI_ST_NONE=0, UCSI_ST_OK, UCSI_ST_FAILED, UCSI_ST_OFF };
#define UCSI_SVID_DP 0xFF01
#define UCSI_SVID_TBT 0x8087

typedef struct { int (*rd)(void* c,u32 off,void* b,u32 n); int (*wr)(void* c,u32 off,const void* b,u32 n); int (*dsm)(void* c,int fn); void (*delay_ms)(void* c,u32 ms); void* c; u32 poll_ms; } ucsi_ops;
typedef struct { u8 ok, connected, source, opmode, ptype, pflags, chg, provider, consumer, altcap, dp, tbt, nsvid, ncam; u16 change, svid[4], cam[4]; u32 rdo, pdo, mv, ma, mw, dp_vdo; u8 raw[20]; } ucsi_conn;
typedef struct { int state; u16 ver; u8 nconn, nalt, msgin; u32 attr; u16 pdver, tcver; u32 cmds, errs, timeouts, lastcci; ucsi_conn c[UCSI_MAXC]; char why[48]; } ucsi_t;

int ucsi_core_cmd(ucsi_t* u,const ucsi_ops* o,u64 ctl,u8* in,int inmax);
int ucsi_core_init(ucsi_t* u,const ucsi_ops* o);
int ucsi_core_conn(ucsi_t* u,const ucsi_ops* o,int n);
int ucsi_core_refresh(ucsi_t* u,const ucsi_ops* o);
int ucsi_conn_text(const ucsi_conn* c,char* t,int tn,char* d,int dn);
const char* ucsi_ptype_name(int t);

int ucsi_probe(void);
int ucsi_state(void);
int ucsi_count(void);
int ucsi_row(int i,char* t,int tn,char* d,int dn);
int ucsi_dp_active(void);
int ucsi_gpu_text(char* b,int n);
int ucsi_enabled(void);
void ucsi_set_enabled(int on);
const char* ucsi_status(void);
void ucsi_tick(int force);
void ucsi_shell(const char* arg,void (*out)(const char*));
#endif
