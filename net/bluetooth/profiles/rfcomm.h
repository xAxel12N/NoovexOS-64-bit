#ifndef NVX_NET_BLUETOOTH_RFCOMM_H
#define NVX_NET_BLUETOOTH_RFCOMM_H
#include "net/bluetooth/bt_host.h"
#define RFC_PSM 0x0003
#define RFC_SABM 0x2F
#define RFC_UA 0x63
#define RFC_DM 0x0F
#define RFC_DISC 0x43
#define RFC_UIH 0xEF
#define RFC_PF 0x10
#define RFC_MTU 127
#define RFC_MAXCR 255
typedef struct { u8 dlci, cr, type, pf, credits, hascr; const u8* info; int len; } rfcframe_t;
u8 rfc_fcs(const u8* p,int n);
int rfc_build(u8 dlci,int cr,u8 ctl,int credits,const u8* info,int n,u8* out,int cap);
int rfc_parse(const u8* p,int n,int cfc,rfcframe_t* f);
typedef void (*rfc_rx_cb)(const u8* p,int n);
int rfcomm_open(btconn_t* c,u8 ch,rfc_rx_cb rx,int ms);
int rfcomm_send(const u8* p,int n);
void rfcomm_close(void);
int rfcomm_up(void);
btconn_t* rfcomm_conn(void);
int rfcomm_listen(u8 ch,int (*ok)(btconn_t*),rfc_rx_cb rx,void (*up)(btconn_t*));
int rfcomm_incoming(void);
#endif
