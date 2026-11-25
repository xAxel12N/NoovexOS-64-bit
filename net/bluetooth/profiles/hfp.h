#ifndef NVX_NET_BLUETOOTH_HFP_H
#define NVX_NET_BLUETOOTH_HFP_H
#include "net/bluetooth/bt_host.h"
#include "net/bluetooth/sbc/sbc.h"
#define HFP_AG_CODEC_NEG 0x200
#define HFP_HF_CODEC_NEG 0x80
enum { HAG_BRSF, HAG_CINDT, HAG_CINDR, HAG_CMER, HAG_SLC };
typedef struct { u8 st, cmer, vgs, vgm, bcs, bac, codec; u32 hff, agf; } hfpag_t;
void hfp_ag_reset(hfpag_t* s);
int hfp_ag_line(hfpag_t* s,const char* l,int n,char* out,int cap);
int hfp_sdp_channel(const u8* p,int n);
int hfp_connect(btconn_t* c,int ms);
void hfp_disconnect(void);
int hfp_slc_up(void);
int hfp_mic_start(void);
void hfp_mic_stop(void);
int hfp_sco_peer(const u8* addr);
int hfp_enabled(void);
void hfp_set_enabled(int on);
int hfp_text(char* p,char* e);
#define BT_SCO_MTU 48
typedef struct { u8 b[3+255]; int n, bad, max; } scoreasm_t;
int bt_sco_reasm(scoreasm_t* r,const u8* p,int n,u16 h,void (*out)(const u8*,int));
void bt_sco_rx_stream(const u8* p,int n);
int bt_sco_tx_stream(u8* o,int n);
int bt_sco_read(u8* o,int n);
int btusb_sco_start(int alt);
void btusb_sco_stop(void);
int bt_sco_conn_req(const u8* p,int pl);
void bt_sco_event(const u8* e,int n);
int bt_sco_setup(btconn_t* c);
void bt_sco_close(void);
int bt_sco_up(void);
int bt_sco_audio(void);
const char* bt_sco_text(void);
int bt_sco_pktlen(int mps,int mtu);
int bt_iso_refill(u32 enq,u32 done,int valid,u8 cc,int target);
int msbc_h2_frame(sbcenc_t* e,int seq,const int16_t* pcm,u8* o);
int msbc_h2_rx(msbcdec_t* d,u8* f,int* fn,const u8* p,int n,void (*out)(const u8*,int));
int bt_sco_codec(void);
int bt_sco_rate(void);
int btusb_sco_mps(void);
int bt_hci_sco_mtu(void);
int hfp_codec(void);
void hfp_codec_fallback(void);
int hfp_mic_avail(void);
int hfp_mic_selected(void);
void hfp_mic_select(int headset);
void hfp_mic_keep(void);
int hfp_msbc_enabled(void);
void hfp_set_msbc(int on);
#endif
