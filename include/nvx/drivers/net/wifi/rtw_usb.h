#ifndef NVX_DRIVERS_WIFI_RTW_USB_H
#define NVX_DRIVERS_WIFI_RTW_USB_H
#include "include/nvx/base.h"
#define RTWU_H 0x5553u
#define RTWU_TXBUF 16384
#define RTWU_RXBUF 16384
#define RTWU_RXD 24u
#define RTWU_AGG_MAX 3
enum { RTWU_QS_BEACON=0x10, RTWU_QS_HIGH=0x11, RTWU_QS_MGMT=0x12, RTWU_QS_H2C=0x13 };
typedef struct rtwu_lo {
    int (*vreq)(u8 rt,u16 val,u16 idx,u8* buf,u16 len);
    int (*bulk_out)(int ep,const u8* p,int n);
    int (*bulk_in)(u8* p,int cap,int ms);
    u16 vid, pid;
    u8 nout, speed;
} rtwu_lo;
typedef struct rtwu_agg { u8* buf; int len, n, ep, cap; } rtwu_agg;
extern const rtwu_lo* g_rtwu;
const struct rtw_chip* rtwu_chip_for(u16 vid,u16 pid);
int  rtwu_attach(const rtwu_lo* lo);
void rtwu_detach(void);
int  rtwu_ep_for(int qsel);
u16  rtwu_pqmap(int nout);
int  rtwu_qsel_frame(const u8* f,int n);
int  rtwu_txdesc(u8* d,const u8* f,int n,int qsel,int rsvd);
void rtwu_csum(u8* d,int agg);
int  rtwu_agg_add(rtwu_agg* a,const u8* f,int n,int qsel);
int  rtwu_agg_flush(rtwu_agg* a);
int  rtwu_rx_parse(const u8* b,int n,void (*cb)(const u8* desc,const u8* fr,int len));
int  rtwu_power_on(u32 m);
void rtwu_intf_cfg(u32 m);
int  rtwu_bcnq_stage(u32 m,const u8* payload,u32 plen,u32 pghead);
int  rtwu_bringup(u32 m);
extern u32 g_rtwu_err, g_rtwu_rx, g_rtwu_rxdrop, g_rtwu_c2h, g_rtwu_tx, g_rtwu_txagg;
#endif
