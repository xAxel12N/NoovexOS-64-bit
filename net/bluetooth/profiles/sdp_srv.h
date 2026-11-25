#ifndef NVX_NET_BLUETOOTH_SDP_SRV_H
#define NVX_NET_BLUETOOTH_SDP_SRV_H
#include "net/bluetooth/bt_host.h"
#define SDP_PSM 0x0001
#define HFP_AG_CH 13
#define SDP_NREC 3
int sdp_de(const u8* p,int n,int* hl,int* ln);
void sdp_srv_build(u8 hfp_ch,u16 hfp_feat);
int sdp_srv_req(const u8* q,int qn,u8* o,int cap);
const u8* sdp_srv_record(int i,int* n);
int sdp_srv_trusted(const btconn_t* c);
void bt_srv_init(void);
int bt_srv_enabled(void);
void bt_srv_set_enabled(int on);
int hfp_listen(void);
int a2dp_listen(void);
#endif
