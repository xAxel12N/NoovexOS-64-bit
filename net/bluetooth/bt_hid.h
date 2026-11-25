#ifndef NVX_NET_BLUETOOTH_BT_HID_H
#define NVX_NET_BLUETOOTH_BT_HID_H
#include "net/bluetooth/bt_host.h"
#define HIDR_MAXL 8
enum { HIDK_NONE, HIDK_KBD, HIDK_MOUSE, HIDK_CC };
typedef struct { u8 id, kind; short mod, arr, arr_n, arr_sz, btn, btn_n, x, xs, y, ys, w, ws, pan, ps, bm, bm_n, bm_min, cc, cc_n, cc_sz; } hidl_t;
typedef struct { hidl_t l[HIDR_MAXL]; int nl, hasid; u8 kprev[8], bprev; u16 ccprev; int rep_c; u8 rep_u; u64 rep_t0, rep_t; } hidr_t;
int hidr_parse(hidr_t* r,const u8* d,int n);
void hidr_boot(hidr_t* r,int kbd,int mouse);
int hidr_input(hidr_t* r,int id,const u8* p,int n);
void hidr_release(hidr_t* r);
void hidr_tick(hidr_t* r);

typedef void (*att_ntf_cb)(btconn_t* c,u16 h,const u8* p,int n);
void att_init(void);
void att_conn_down(btconn_t* c);
int att_req(btconn_t* c,const u8* req,int n,u8* rsp,int cap,int ms);
int att_cmd(btconn_t* c,const u8* p,int n);
int att_mtu(btconn_t* c);
void att_set_mtu(btconn_t* c,u16 m);
void att_set_ntf(att_ntf_cb cb);

typedef struct { u16 decl, vh, end, uuid; u8 props; } gchr_t;
int gatt_exchange_mtu(btconn_t* c,u16 want,int ms);
int gatt_find_svc(btconn_t* c,u16 uuid,u16* start,u16* end,int ms);
int gatt_chars(btconn_t* c,u16 start,u16 end,gchr_t* out,int max,int ms);
int gatt_find_desc(btconn_t* c,u16 from,u16 to,u16 uuid,int ms);
int gatt_read(btconn_t* c,u16 h,u8* out,int cap,int ms);
int gatt_write(btconn_t* c,u16 h,const u8* p,int n,int ms);
int gatt_write_cmd(btconn_t* c,u16 h,const u8* p,int n);
int gatt_err(void);

void hidh_init(void);
int hid_host_connect(btconn_t* c,int ms);
void hidh_conn_down(btconn_t* c);
int hidh_active(btconn_t* c);
int hogp_connect(btconn_t* c,int ms);
void hogp_conn_down(btconn_t* c);
int hogp_active(btconn_t* c);
void bt_hid_poll(void);
int bt_hid_status_text(char* out,int cap);
#endif
