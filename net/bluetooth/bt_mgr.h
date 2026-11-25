#ifndef NVX_NET_BLUETOOTH_BT_MGR_H
#define NVX_NET_BLUETOOTH_BT_MGR_H
#include "include/nvx/base.h"
#define BTM_MAX 24
typedef struct { u8 addr[6]; u8 le, atype, bonded, conn, seen; int rssi; u32 cod; u16 appear; char name[49]; } btmdev_t;
int btm_parse_addr(const char* s,u8* a);
void btm_sync(void);
int btm_count(void);
const btmdev_t* btm_get(int i);
const char* btm_kind(const btmdev_t* d);
const char* btm_last(void);
int btm_pair(const char* s);
int btm_connect(const char* s);
int btm_disconnect(const char* s);
int btm_forget(const char* s);
int btm_autoconnect(void);
#define BTM_BOOT_TICKS 800
#define BTM_RB_MAX 4
#define BTM_RB_PAGE_MS 6000
#define BTM_RB_LE_MS 5000
#define BTM_RB_SEC_MS 10000
#define BTM_RB_DROP_MS 3000
#define BTM_RB_PROFILE_MS 5000
int btm_boot_busy(void);
int btm_boot_tick(void);
int btm_boot_enabled(void);
void btm_boot_enable(int on);
const char* btm_boot_why(void);
int btm_devices_text(char* out,int cap);
#endif
