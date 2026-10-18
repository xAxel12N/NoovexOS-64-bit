#ifndef NVX_DRIVERS_WIFI_RTW_SEC_H
#define NVX_DRIVERS_WIFI_RTW_SEC_H

#include "include/nvx/base.h"
#include "include/nvx/drivers/wifi/rtw_efuse.h"
#include "include/nvx/drivers/wifi/rtw8821ce.h"

extern int g_gtk_state , g_gtk_keyid , g_gtk_len;
extern u8 fat_filebuf[65536];

const char* wpa_gtk_status(void);
int rtw_wpa2_join(u32 m,const scan_ap* ap,const char* ssid,const char* pass);
int rtw_h2c_send(u32 m,int box,u32 w0,u32 w1);
void rtw_phy_power_on(u32 m);
void rtw_wmac_init(u32 m);
void rtw_phy_post(u32 m,int have_xtal,u8 xtal);
int rtw_apply_built(u32 m,const rtw_bent* t,int n,const char* lbl,int verify);
int rtw_read_table_file(const char* name,u8* out,int cap);
void fatls_cb(const u8* ent);

#endif
