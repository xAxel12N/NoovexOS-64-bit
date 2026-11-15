#ifndef NVX_NET_WIFI_WIFI_AMPDU_H
#define NVX_NET_WIFI_WIFI_AMPDU_H

#include "include/nvx/base.h"

#define AMPDU_WIN 64
#define AMPDU_SLOTS 128
#define AMPDU_SLOT 1520
#define AMPDU_SESS 2
#define AMPDU_TMO 10

extern int g_ampdu_on;
extern u32 g_ampdu_sess, g_ampdu_addba, g_ampdu_delba, g_ampdu_decl, g_ampdu_reord, g_ampdu_flush, g_ampdu_old;

void ampdu_reset(void);
int ampdu_addba(int tid,u16 ssn,u16 bufsz,u64 now);
void ampdu_delba(int tid);
int ampdu_active(int tid);
int ampdu_rx(int tid,u16 seq,const u8* f,int len,u64 pn,u64 now);
void ampdu_tick(u64 now);
int ampdu_pop(u8* out,int max,u64* pn,int* tid);

#endif
