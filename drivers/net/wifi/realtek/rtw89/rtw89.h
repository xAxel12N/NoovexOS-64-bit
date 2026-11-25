/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * Derived from Linux drivers/net/wireless/realtek/rtw89 (reg.h, mac.h, fw.h, core.h; GPL-2.0 OR BSD-3-Clause, used under BSD-3-Clause).
 * Copyright(c) 2019-2022  Realtek Corporation
 * Licence text: drivers/net/wifi/realtek/rtw89/LICENCE.BSD-3-Clause
 */
#ifndef NVX_DRIVERS_NET_WIFI_REALTEK_RTW89_H
#define NVX_DRIVERS_NET_WIFI_REALTEK_RTW89_H
#include "include/nvx/base.h"

#define R_AX_SYS_ISO_CTRL 0x0000
#define R_AX_SYS_PW_CTRL 0x0004
#define R_AX_SYS_ADIE_PAD_PWR_CTRL 0x0018
#define R_AX_AFE_LDO_CTRL 0x0020
#define R_AX_SPS_ANA_ON_CTRL2 0x0228
#define R_AX_SYS_SDIO_CTRL 0x0070
#define R_AX_PLATFORM_ENABLE 0x0088
#define R_AX_WLLPS_CTRL 0x0090
#define R_AX_PMC_DBG_CTRL2 0x00CC
#define R_AX_SYS_CFG1 0x00F0
#define R_AX_WLAN_XTAL_SI_CTRL 0x0270
#define R_AX_EECS_EESK_FUNC_SEL 0x02D8
#define R_AX_SPS_DIG_OFF_CTRL0 0x0400
#define R_AX_DMAC_FUNC_EN 0x8400
#define R_AX_CMAC_FUNC_EN 0xC000

#define B_AX_ISO_EB2CORE (1u<<8)
#define B_AX_PWC_EV2EF_B14 (1u<<14)
#define B_AX_PWC_EV2EF_B15 (1u<<15)
#define B_AX_APFN_ONMAC (1u<<8)
#define B_AX_APFM_SWLPS (1u<<10)
#define B_AX_AFSM_WLSUS_EN (1u<<11)
#define B_AX_AFSM_PCIE_SUS_EN (1u<<12)
#define B_AX_APDM_HPDN (1u<<15)
#define B_AX_EN_WLON (1u<<16)
#define B_AX_RDY_SYSPWR (1u<<17)
#define B_AX_DIS_WLBT_PDNSUSEN_SOPC (1u<<18)
#define B_AX_SYM_PADPDN_WL_RFC_1P3 (1u<<5)
#define B_AX_SYM_PADPDN_WL_PTA_1P3 (1u<<6)
#define B_AX_AON_OFF_PC_EN (1u<<23)
#define B_AX_PCIE_CALIB_EN_V1 (1u<<12)
#define B_AX_PLATFORM_EN 1u
#define B_AX_DIS_WLBT_LPSEN_LOPC (1u<<1)
#define B_AX_SYSON_DIS_PMCR_AX_WRMSK (1u<<2)
#define B_AX_CHIP_VER_MASK 0xF000u
#define B_AX_PINMUX_EESK_FUNC_SEL_MASK 0xF0u
#define PINMUX_EESK_FUNC_SEL_BT_LOG 1u
#define B_AX_C1_L1_MASK 0x03u
#define B_AX_C3_L1_MASK 0x30u
#define B_AX_WL_XTAL_SI_CMD_POLL (1u<<31)
#define RTW89_DMAC_FUNC_ALL 0x7FFF8000u
#define RTW89_CMAC_FUNC_ALL 0x7000803Fu
#define RTL8852B_DEFAULT_SPS_ANA 0x4F42

#define XTAL_SI_XTAL_XMD_2 0x24
#define XTAL_SI_XTAL_XMD_4 0x26
#define XTAL_SI_ANAPAR_WL 0x90
#define XTAL_SI_SRAM_CTRL 0xA1
#define XTAL_SI_PON_WEI 0x01
#define XTAL_SI_PON_EI 0x02
#define XTAL_SI_OFF_WEI 0x04
#define XTAL_SI_OFF_EI 0x08
#define XTAL_SI_RFC2RF 0x10
#define XTAL_SI_SHDN_WL 0x20
#define XTAL_SI_GND_SHDN_WL 0x40
#define XTAL_SI_SRAM2RFC 0x80
#define XTAL_SI_SRAM_DIS 0x02
#define XTAL_SI_LDO_LPS 0x70
#define XTAL_SI_LPS_CAP 0x0F

#define RTW89_MFW_SIG 0xFF
#define RTW89_FW_NORMAL 1
#define RTW89_FW_WOWLAN 3
#define RTW89_FW_NORMAL_CE 5
#define FWDL_SECTION_MAX_NUM 10
#define FWDL_SECTION_CHKSUM_LEN 8
#define FWDL_SECURITY_SECTION_TYPE 9
#define FWDL_SECURITY_SIGLEN 512

enum { RTW89_CHIP_8852B=1, RTW89_CHIP_8852C=2 };
enum { RTW89_ST_NONE=0, RTW89_ST_FOUND, RTW89_ST_NOFW, RTW89_ST_HWFAIL, RTW89_ST_PWRON, RTW89_ST_FWOK, RTW89_ST_READY };

struct rtw89_pci_id { u16 vendor,device; u8 chip; const char* name; const char* fw; };
struct rtw89_fw_section { u32 dladdr,len,mssc; u8 type,redl; u32 off; };
struct rtw89_fw_info {
    u8 major,minor,sub,idx,hdr_ver,cv,type,dyn; u32 commit,hdr_len,off,size; u16 year; u8 month,day,hour,min;
    int nsec; struct rtw89_fw_section sec[FWDL_SECTION_MAX_NUM];
};
struct rtw89_softc;
struct rtw89_io { u32 (*rd)(struct rtw89_softc*,u32 off,int w); void (*wr)(struct rtw89_softc*,u32 off,u32 v,int w); void (*delay)(u32 us); };
struct rtw89_softc {
    int state; u8 bus,slot,fn,chip,cv,rfe_type; u16 vendor,device; u32 mmio; const struct rtw89_pci_id* id; const struct rtw89_io* io;
    const char* err; u32 err_reg; u8 mac[6]; struct rtw89_fw_info fw; u32 fwlen; u32 polls,xtal_writes;
    u8 efuse_valid,efuse_ok,xtal_cap,country[2]; u32 efuse_reads;
    u8 h2c_seq,rec_seq,fw_rdy,fwdl_sts; u32 h2c_cnt,c2h_cnt,fw_ctrl_dump,boot_dbg_dump,fwdl_tries;
    u8 done_cat,done_cls,done_func,done_ret,done_seq; u32 done_cnt; char fwlog[64];
    u32 mmio_size; u8 phy_ok; u32 bacam_to;
};

extern const struct rtw89_pci_id rtw89_ids[];
const struct rtw89_pci_id* rtw89_match(u16 vendor,u16 device);
int rtw89_mfw_pick(const u8* d,u32 len,u8 cv,u8 type,u32* off,u32* size,u8* got_cv);
int rtw89_fw_hdr_parse(const u8* d,u32 len,struct rtw89_fw_info* fi);
int rtw89_fw_parse(const u8* d,u32 len,u8 cv,struct rtw89_fw_info* fi);
int rtw89_write_xtal_si(struct rtw89_softc* sc,u8 off,u8 val,u8 mask);
int rtw8852b_pwr_on(struct rtw89_softc* sc);
int rtw89_read_cv(struct rtw89_softc* sc);
int rtw89_probe(void);
int rtw89_start(void);
void rtw89_status(char* out,int cap);
void rtw89_up(char* out,int cap);
void rtw89_wifi_hw_probe(void);
extern struct rtw89_softc g_rtw89;
extern int (*rtw89_firmware_read)(const char* path,u8** out,u32* len);
#endif
