#ifndef NVX_NET_WIFI_WIFI_AMPDU_H
#define NVX_NET_WIFI_WIFI_AMPDU_H

#include "include/nvx/base.h"

#define AMPDU_WIN 64
#define AMPDU_SLOTS 128
#define AMPDU_SLOT 1520
#define AMPDU_SESS 2
#define AMPDU_TMO 10
#define AMPDU_TX_DELAY 300
#define AMPDU_TX_WAIT 100
#define AMPDU_TX_RETRY 3000
#define AMPDU_TX_EARLY 1000
#define AMPDU_TX_MAXBAD 3
#define AMPDU_TX_STALL 500
#define AMPDU_TX_STALLN 50
#define AMPDU_TX_LOSTWIN 6000

extern int g_ampdu_on;
extern u32 g_ampdu_sess, g_ampdu_addba, g_ampdu_delba, g_ampdu_decl, g_ampdu_reord, g_ampdu_flush, g_ampdu_old;

void ampdu_reset(void);
int ampdu_addba(int tid,u16 ssn,u16 bufsz,u64 now);
void ampdu_delba(int tid);
int ampdu_active(int tid);
int ampdu_rx(int tid,u16 seq,const u8* f,int len,u64 pn,u64 now);
void ampdu_tick(u64 now);
int ampdu_pop(u8* out,int max,u64* pn,int* tid);
extern int g_ampdu_tx_on, g_ampdu_tx_auto, g_ampdu_tx_hard;
extern u32 g_ampdu_tx_req, g_ampdu_tx_ok, g_ampdu_tx_fail, g_ampdu_bar, g_ampdu_prot;
void ampdu_tx_reset(u64 now);
int ampdu_tx_want(u64 now);
u8 ampdu_tx_sent(u64 now);
int ampdu_tx_resp(u8 tok,u16 status,u16 prm,u64 now);
void ampdu_tx_tick(u64 now);
void ampdu_tx_peer_delba(u64 now);
int ampdu_tx_stop(void);
int ampdu_tx_active(void);
u16 ampdu_tx_win(void);
u16 ampdu_tx_seq(void);
void ampdu_tx_unseq(u16 s);
int ampdu_bar(int tid,u16 ssn);
int ampdu_tx_watch(u32 tx,u32 rx,u64 now);
int ampdu_tx_early(u64 now);
void ampdu_tx_bad(u64 now);

#endif
