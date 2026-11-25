#ifndef NVX_NET_BLUETOOTH_A2DP_H
#define NVX_NET_BLUETOOTH_A2DP_H
#include "net/bluetooth/bt_host.h"
#include "net/bluetooth/sbc/sbc.h"
int a2dp_connect(btconn_t* c,int ms);
void a2dp_disconnect(void);
int a2dp_active(void);
int a2dp_routed(void);
int a2dp_route(int bt);
int a2dp_enabled(void);
void a2dp_set_enabled(int on);
const char* a2dp_reason(void);
int a2dp_text(char* p,char* e);
int a2dp_media_packet(sbcenc_t* e,const u8* pcm,int frames,u16 seq,u32 ts,int vol,u8* out,int cap);
int a2dp_frames_fit(int framelen,int omtu);
int a2dp_pick_config(const u8* sbc,int* rate,u8* cfg);
int a2dp_parse_sep(const u8* d,int n,u8* acp);
int a2dp_find_sbc(const u8* d,int n,const u8** sbc);
typedef struct { u8 st, intseid, cfg[4]; int rate; } a2acp_t;
int a2dp_acp_cmd(a2acp_t* s,u8 id,const u8* d,int n,u8* r,int* rn);
#endif
