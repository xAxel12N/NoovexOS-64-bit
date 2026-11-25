/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * Shared contract between the rtw89 bus layer (PCIe DMA rings, owned by rtw89b) and the firmware/H2C/C2H/efuse layer (rtw89a).
 * Derived from Linux drivers/net/wireless/realtek/rtw89 (core.h, fw.h, pci.h; GPL-2.0 OR BSD-3-Clause, used under BSD-3-Clause).
 * Copyright(c) 2019-2022  Realtek Corporation
 * Licence text: drivers/net/wifi/realtek/rtw89/LICENCE.BSD-3-Clause
 */
#ifndef NVX_DRIVERS_NET_WIFI_REALTEK_RTW89_CORE_H
#define NVX_DRIVERS_NET_WIFI_REALTEK_RTW89_CORE_H
#include "drivers/net/wifi/realtek/rtw89/rtw89.h"

#define RTW89_H2C_HDR_LEN 8
#define RTW89_H2C_MAX_LEN 2048
#define RTW89_C2H_HDR_LEN 8
#define RTW89_FWDL_PART_SIZE 2020
#define RTW89_H2C_CAT_MAC 1
#define RTW89_C2H_CAT_MAC 1
#define RTW89_C2H_CL_INFO 0
#define RTW89_C2H_FUNC_REC_ACK 0
#define RTW89_C2H_FUNC_DONE_ACK 1
#define RTW89_C2H_FUNC_LOG 2

enum { RTW89_H2C_PKT_CMD=0, RTW89_H2C_PKT_FWDL=1 };

struct rtw89_hci_ops {
    int (*h2c_tx)(struct rtw89_softc* sc,const u8* pkt,u32 len,int kind);
    int (*c2h_rx)(struct rtw89_softc* sc,u8* buf,u32 cap);
    int (*pre_fwdl)(struct rtw89_softc* sc);
};

extern const struct rtw89_hci_ops* rtw89_hci;

int rtw89_efuse_read(struct rtw89_softc* sc);
int rtw89_efuse_logical(const u8* phy,u32 phy_size,u32 sec_ctrl,u8* log,u32 log_size);
int rtw89_fw_download(struct rtw89_softc* sc,const u8* img,u32 len);
int rtw89_fw_check_rdy(struct rtw89_softc* sc);
int rtw89_h2c_send(struct rtw89_softc* sc,u8 cat,u8 cls,u8 func,int rack,int dack,const u8* payload,u32 len);
int rtw89_c2h_poll(struct rtw89_softc* sc,u32 budget);
int rtw89_h2c_wait_done(struct rtw89_softc* sc,u8 cls,u8 func,u32 timeout_us);
int rtw89_c2h_handle(struct rtw89_softc* sc,const u8* c2h,u32 len);
typedef void (*rtw89_c2h_cb)(struct rtw89_softc* sc,u8 cat,u8 cls,u8 func,const u8* payload,u32 len);
extern rtw89_c2h_cb rtw89_c2h_hook;
#endif
