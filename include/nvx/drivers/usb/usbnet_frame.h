#ifndef NVX_USBNET_FRAME_H
#define NVX_USBNET_FRAME_H
#include "include/nvx/base.h"
enum { UNF_ECM=1, UNF_NCM=2, UNF_RNDIS=3, UNF_URE=4, UNF_AXEN=5, UNF_IPHETH=6 };
#define UNF_MTU_FRAME 1514
#define UNF_IPHETH_BUF 1516
typedef void (*unf_sink_t)(void* ctx,const u8* f,int n);
typedef struct { int kind, ndiv, nrem, mps; u16 seq; u32 rx_frames, rx_bad, tx_frames; } unf_ctx_t;
void unf_init(unf_ctx_t* c,int kind,int mps);
int unf_tx(unf_ctx_t* c,u8* out,int cap,const u8* f,int len);
int unf_rx(unf_ctx_t* c,const u8* b,int n,unf_sink_t sink,void* arg);
int unf_tx_overhead(int kind);
int unf_rx_bufsize(int kind);
const char* unf_kind_name(int kind);
int unf_mac_ok(const u8* mac);
void unf_mac_local(u8* mac,u64 seed);
#endif
