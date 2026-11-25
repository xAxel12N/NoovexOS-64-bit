#ifndef NVX_NET_BLUETOOTH_BT_HOST_H
#define NVX_NET_BLUETOOTH_BT_HOST_H
#include "include/nvx/base.h"
#define BT_MAX_CONN 4
#define BT_MAX_CHAN 12
#define BT_L2_MTU 672
#define BT_ACL_MAX 1100
#define BT_CID_SIG 0x0001
#define BT_CID_ATT 0x0004
#define BT_CID_LESIG 0x0005
#define BT_CID_SMP 0x0006
#define BT_PSM_HID_CTRL 0x0011
#define BT_PSM_HID_INTR 0x0013
enum { BTC_FREE, BTC_CONNECTING, BTC_UP, BTC_CLOSING };
enum { L2_CLOSED, L2_WAIT_CONN, L2_CONFIG, L2_OPEN, L2_WAIT_DISC };
typedef struct btconn { u8 state, le, atype, role, enc, auth, keysz, reason; u8 addr[6]; u16 handle, interval, latency, timeout, le_mtu;
    u32 cod; u8 rx[BT_ACL_MAX]; int rxn, rxwant; void* sec; void* mgr; void* att; u64 t_up; } btconn_t;
typedef struct l2chan { u8 state, ident, cfg_in, cfg_out, server, fixed; u16 psm, lcid, rcid, imtu, omtu; btconn_t* c;
    void (*rx)(struct l2chan* ch,const u8* p,int n); void (*closed)(struct l2chan* ch); void* priv; } l2chan_t;
typedef void (*bt_conn_cb)(btconn_t* c);
typedef void (*bt_disc_cb)(btconn_t* c,u8 reason);
typedef void (*bt_fixed_cb)(btconn_t* c,const u8* p,int n);
typedef int (*bt_accept_cb)(l2chan_t* ch);

int bt_hci_cmd(u16 op,const u8* p,int n,u8* rp,int rcap,int ms);
int bt_hci_send(u16 op,const u8* p,int n);
int bt_hci_wait_evt(int code,int sub,u8* out,int cap,int ms);
int bt_hci_up(void);
const u8* bt_hci_addr(void);
int bt_hci_le(void);
int bt_hci_bredr(void);
void bt_poll(void);

void bt_conn_init(void);
void bt_host_event(const u8* e,int n);
void bt_acl_rx(const u8* p,int n);
btconn_t* bt_connect(const u8* addr,int le,int atype,int ms);
int bt_disconnect(btconn_t* c,u8 reason,int ms);
btconn_t* bt_connect_start(const u8* addr,int le,int atype);
void bt_connect_abort(btconn_t* c);
void bt_conn_drop(btconn_t* c,u8 reason);
btconn_t* bt_conn_by_handle(u16 h);
btconn_t* bt_conn_by_addr(const u8* addr,int le);
btconn_t* bt_conn_at(int i);
int bt_conn_listen(bt_conn_cb up,bt_disc_cb down);
int bt_page_scan(int on);
int bt_acl_send(btconn_t* c,u16 cid,const u8* p,int n);
int bt_acl_pending(void);
int bt_conn_wait(btconn_t* c,volatile u8* flag,u8 want,int ms);

void l2cap_init(void);
void l2cap_rx(btconn_t* c,u16 cid,const u8* p,int n);
void l2cap_conn_down(btconn_t* c);
int l2cap_server(u16 psm,bt_accept_cb accept);
l2chan_t* l2cap_connect(btconn_t* c,u16 psm,void (*rx)(l2chan_t*,const u8*,int),int ms);
int l2cap_send(l2chan_t* ch,const u8* p,int n);
int l2cap_close(l2chan_t* ch,int ms);
int l2cap_fixed(u16 cid,bt_fixed_cb rx);
int l2cap_fixed_send(btconn_t* c,u16 cid,const u8* p,int n);
int l2cap_le_conn_update(btconn_t* c,u16 imin,u16 imax,u16 lat,u16 to);

int bt_sec_event(const u8* e,int n);
int bt_sec_auth(btconn_t* c,int ms);
int bt_smp_pair(btconn_t* c,int ms);
int bt_sec_auth_start(btconn_t* c);
int bt_sec_auth_poll(btconn_t* c);
void bt_sec_auth_abort(btconn_t* c);
int bt_smp_start(btconn_t* c);
int bt_smp_poll(btconn_t* c);
void bt_smp_abort(btconn_t* c);
int bt_mgr_accept(const u8* addr,u32 cod,int le);
#endif
