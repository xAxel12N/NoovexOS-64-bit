#ifndef NVX_DRIVERS_WIFI_RTW_CHIP_H
#define NVX_DRIVERS_WIFI_RTW_CHIP_H

#include "include/nvx/base.h"

/* rtw88 multi-chip contract (v11.10). One rtw_chip per supported chip in drivers/net/wifi/rtw88/chips/.
   The core (bring-up, firmware download, MAC/TRX init, rings, scan, join, keys) is shared and calls the
   chip only through this table. Register access always goes through rtw_r8..rtw_w32, which use the active
   bus (PCIe MMIO today, USB vendor requests later), so chip code never touches the bus directly.
   Core bring-up order (wifi_up runs the shell steps): init: power_on. fw: firmware download (fw_start..fw_end).
   macinit: TRX/page setup from page_table, rqpn, txff_size, rxff_size, rsvd_drv_pages. allbuilt: phy_init (BB/RF
   enable), efuse read + efuse_parse, mac_init (WMAC values), tables mac/bb/agc/agc_btg (RFE 2/4)/rf per path, MAC
   address, phy_post (crystal cap, BB/RF final). rings: DMA rings incl. the H2C ring. Then coex_wifi_only, and
   calibrate before the first frame after power-on and after each join channel set. set_channel calls rfe_setup and
   set_tx_power itself. Optional (may be 0): power_off, calibrate, phy_post, rfe_setup, coex_wifi_only.
   rf_base/rf_sipi: per-path RF direct-read and SIPI write registers; 0 means the 8821C/8822B values
   {0x2800,0x2C00}/{0xC90,0xE90}. rf_mix: 1 = direct writes except RF 0x00 via SIPI (8822C).
   set_channel gets the primary channel; the chip works out the center channel for 40/80 MHz.
   tables_in_phy_init: 1 = phy_init loads bb/agc/rf itself (efuse is read and parsed before phy_init, the core
   loads no tables). cond_mode: table conditions; 8821C = package from efuse 0xCA bit 5 and RFE & 0x1F, FULL_RFE =
   package 15 and the whole RFE byte, CHIP = the chip sets g_rtw_cut/g_rtw_pkg/g_rtw_rfe itself. sys_func_en: value
   OR-ed into 0x0003 before the firmware download (0 = 0xD8). fw_rxff_size: firmware RX FIFO size (information).
   pcie_phy: PCIe PHY (MDIO) parameters, run by wifi init before power_on; 0 = written, <0 = failed, 1 or no op =
   nothing to write for this chip/cut. efuse_phy/log/prot: eFuse physical, logical and protected sizes (0 = the
   8821C 512/512/96; the maps hold up to 1024/768). */

enum { RTW_BUS_PCIE = 0, RTW_BUS_USB = 1 };
enum { RTW_WCPU_3081 = 0, RTW_WCPU_8051 = 1 };
enum { RTW_BW_20 = 0, RTW_BW_40 = 1, RTW_BW_80 = 2 };
enum { RTW_COND_8821C = 0, RTW_COND_FULL_RFE = 1, RTW_COND_CHIP = 2 };

typedef struct rtw_bus {
    const char* name;
    int kind;
    u8  (*r8)(u32 h, u32 off);
    u16 (*r16)(u32 h, u32 off);
    u32 (*r32)(u32 h, u32 off);
    void (*w8)(u32 h, u32 off, u8 v);
    void (*w16)(u32 h, u32 off, u16 v);
    void (*w32)(u32 h, u32 off, u32 v);
} rtw_bus;

typedef struct rtw_page_table { u16 hq, nq, lq, exq, gapq; } rtw_page_table;
typedef struct rtw_rqpn { u8 vo, vi, be, bk, mg, hi; } rtw_rqpn;

typedef struct rtw_tbl { const unsigned int* t; int n; u8 sz; } rtw_tbl;
typedef struct rtw_pwr_cmd { u16 off; u8 mask, val, cmd; } rtw_pwr_cmd;
enum { RTW_PWR_WRITE = 1, RTW_PWR_POLL = 2, RTW_PWR_DELAY = 3 };

typedef struct rtw_efuse_info {
    u8  mac[6];
    u8  rfe_option;
    u8  crystal_cap;
    u8  thermal_meter;
    u8  country_code[2];
    u8  tx_pwr_idx_2g[2][14];
    u8  tx_pwr_idx_5g[2][14];
} rtw_efuse_info;

typedef struct rtw_chip {
    const char* name;
    u16 vendor, device;
    int bus;
    u8  n_paths;
    u8  wcpu;
    u32 txff_pages;
    u32 rxff_size;
    u32 txff_size;
    u16 rsvd_drv_pages;
    u8  csi_pages;
    u8  tx_desc_sz, rx_desc_sz, tx_buf_desc_sz;
    const unsigned char* fw_start;
    const unsigned char* fw_end;
    const char* fw_name;
    rtw_page_table page_table;
    rtw_rqpn rqpn;
    rtw_tbl mac_tbl, bb_tbl, agc_tbl, agc_btg_tbl, rf_tbl[2];
    int  (*power_on)(u32 m);
    void (*power_off)(u32 m);
    int  (*efuse_parse)(const u8* logmap, int len, rtw_efuse_info* out);
    int  (*mac_init)(u32 m);
    void (*phy_init)(u32 m);
    void (*set_channel)(u32 m, u8 ch, u8 bw);
    void (*set_tx_power)(u32 m, u8 ch);
    void (*calibrate)(u32 m);
    void (*coex_wifi_only)(u32 m);
    void (*rfe_setup)(u32 m, int band5);
    void (*phy_post)(u32 m);
    u32 rf_base[2], rf_sipi[2];
    u8  rf_mix;
    u8  tables_in_phy_init;
    u8  cond_mode;
    u8  sys_func_en;
    u32 fw_rxff_size;
    int  (*pcie_phy)(u32 m);
    u16 efuse_phy, efuse_log, efuse_prot;
} rtw_chip;

extern const rtw_chip* g_rtw_chip;
extern const rtw_bus* g_rtw_bus;
extern const rtw_bus rtw_bus_pcie;
extern rtw_efuse_info g_rtw_efuse;

const rtw_chip* rtw_chip_find(u16 vendor, u16 device, int bus);
int  rtw_chip_count(void);
const rtw_chip* rtw_chip_at(int i);

u32  rtw_rf_read(u32 m, int path, u32 addr, u32 mask);
void rtw_rf_write(u32 m, int path, u32 addr, u32 mask, u32 val);
void rtw_bb_write_mask(u32 m, u32 addr, u32 mask, u32 val);
u32  rtw_bb_read_mask(u32 m, u32 addr, u32 mask);
int  rtw_h2c_pkt_send(u32 m, const u8* pkt, int len);
int  rtw_h2c_cmd(u32 m, u32 w0, u32 w1);

const rtw_chip* rtw_chip_select(u16 vendor, u16 device);
int  rtw_pwr_seq_run(u32 m, const rtw_pwr_cmd* s, int n);
extern u16 g_rtw_pwr_fail_off;
extern int g_rtw_coex_ok;
int  rtw_rfe_type(void);
int  rtw_efuse_load(u32 m);
void rtw_efuse_apply(void);
extern int g_rtw_rf_path;
u16  rtw_rsvd_boundary(void);
int  rtw_h2c_ring_setup(u32 m);
int  rtw_rf_paths(u32 m);
void rtw_h2c_ring_off(void);
extern u32 g_rtw_h2c_sent, g_rtw_h2c_fail;
extern int g_rtw_iqk_en, g_rtw_iqk_last, g_rtw_iqk_dead;
void rtw_chip_calibrate(u32 m);
void rtw_chmark(const char* t);

#endif
