/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * Derived from Linux drivers/net/wireless/realtek/rtw89/rtw8852c.c (rtw8852c_pwr_on_func) and reg.h;
 * GPL-2.0 OR BSD-3-Clause, used under BSD-3-Clause.
 * Copyright(c) 2019-2022  Realtek Corporation
 * Licence text: drivers/net/wifi/realtek/rtw89/LICENCE.BSD-3-Clause
 */
#include "include/nvx/drivers/net/wifi/realtek/rtw89/rtw89_pci.h"

#define R_AX_SYS_STATUS1 0x00F4
#define B_AX_PAD_HCI_SEL_V2_MASK 0x38u
#define MAC_AX_HCI_SEL_PCIE_USB 3u
#define R_AX_LDO_AON_CTRL0 0x0218
#define B_AX_PD_REGU_L (1u<<16)
#define R_AX_SPS_DIG_ON_CTRL0 0x0200
#define B_AX_OCP_L1_MASK 0xE000u
#define R_AX_SYS_ISO_CTRL_EXTEND 0x0080
#define B_AX_CMAC1_FEN (1u<<30)
#define B_AX_R_SYM_ISO_CMAC12PP (1u<<5)
#define R_AX_AFE_CTRL1 0x0024
#define B_AX_R_SYM_WLCMAC1_ALL_PC_EN 0x1Fu
#define R_AX_GPIO0_15_EECS_EESK_LED1_PULL_LOW_EN 0x02E4
#define B_AX_EECS_EESK_LED1_PULL_LOW 0x70000u
#define R_AX_LED1_FUNC_SEL 0x02DC
#define B_AX_PINMUX_EESK_FUNC_SEL_V1_MASK 0x0F000000u
#define RTW8852C_DMAC_FUNC_ALL 0x7FFFC000u

static u32 c_r32(struct rtw89_softc* sc,u32 o){ return sc->io->rd(sc,o,4); }
static void c_w32(struct rtw89_softc* sc,u32 o,u32 v){ sc->io->wr(sc,o,v,4); }
static void c_set32(struct rtw89_softc* sc,u32 o,u32 b){ c_w32(sc,o,c_r32(sc,o)|b); }
static void c_clr32(struct rtw89_softc* sc,u32 o,u32 b){ c_w32(sc,o,c_r32(sc,o)&~b); }
static void c_mask32(struct rtw89_softc* sc,u32 o,u32 m,u32 v){ u32 s=0; while(!((m>>s)&1))s++; c_w32(sc,o,(c_r32(sc,o)&~m)|((v<<s)&m)); }
static void c_set8(struct rtw89_softc* sc,u32 o,u8 b){ sc->io->wr(sc,o,(sc->io->rd(sc,o,1)|b)&0xFF,1); }
static void c_clr8(struct rtw89_softc* sc,u32 o,u8 b){ sc->io->wr(sc,o,sc->io->rd(sc,o,1)&(u32)~b&0xFF,1); }
static int c_poll(struct rtw89_softc* sc,u32 o,u32 m,u32 want){
    for(u32 t=0;;t+=1000){ sc->polls++; if((c_r32(sc,o)&m)==want)return 0; if(t>=20000)break; sc->io->delay(1000); }
    sc->err_reg=o; return -1; }

int rtw8852c_pwr_on(struct rtw89_softc* sc){
    static const u8 xs[][3]={
        {XTAL_SI_ANAPAR_WL,XTAL_SI_SHDN_WL,XTAL_SI_SHDN_WL},{XTAL_SI_ANAPAR_WL,XTAL_SI_OFF_WEI,XTAL_SI_OFF_WEI},{XTAL_SI_ANAPAR_WL,XTAL_SI_OFF_EI,XTAL_SI_OFF_EI},
        {XTAL_SI_ANAPAR_WL,0,XTAL_SI_RFC2RF},{XTAL_SI_ANAPAR_WL,XTAL_SI_PON_WEI,XTAL_SI_PON_WEI},{XTAL_SI_ANAPAR_WL,XTAL_SI_PON_EI,XTAL_SI_PON_EI},
        {XTAL_SI_ANAPAR_WL,0,XTAL_SI_SRAM2RFC},{XTAL_SI_XTAL_XMD_2,0x10,XTAL_SI_LDO_LPS},{XTAL_SI_XTAL_XMD_4,0,XTAL_SI_LPS_CAP} };
    sc->err=0;
    if(((c_r32(sc,R_AX_SYS_STATUS1)&B_AX_PAD_HCI_SEL_V2_MASK)>>3)==MAC_AX_HCI_SEL_PCIE_USB)c_set32(sc,R_AX_LDO_AON_CTRL0,B_AX_PD_REGU_L);
    c_clr32(sc,R_AX_SYS_PW_CTRL,B_AX_AFSM_WLSUS_EN|B_AX_AFSM_PCIE_SUS_EN);
    c_set32(sc,R_AX_SYS_PW_CTRL,B_AX_DIS_WLBT_PDNSUSEN_SOPC);
    c_set32(sc,R_AX_WLLPS_CTRL,B_AX_DIS_WLBT_LPSEN_LOPC);
    c_clr32(sc,R_AX_SYS_PW_CTRL,B_AX_APDM_HPDN);
    c_clr32(sc,R_AX_SYS_PW_CTRL,B_AX_APFM_SWLPS);
    c_mask32(sc,R_AX_SPS_DIG_ON_CTRL0,B_AX_OCP_L1_MASK,7);
    if(c_poll(sc,R_AX_SYS_PW_CTRL,B_AX_RDY_SYSPWR,B_AX_RDY_SYSPWR)<0){ sc->err="power-on: system power not ready"; return -1; }
    c_set32(sc,R_AX_SYS_PW_CTRL,B_AX_EN_WLON);
    c_set32(sc,R_AX_SYS_PW_CTRL,B_AX_APFN_ONMAC);
    if(c_poll(sc,R_AX_SYS_PW_CTRL,B_AX_APFN_ONMAC,0)<0){ sc->err="power-on: MAC power state machine stuck"; return -1; }
    for(int i=0;i<2;i++){ c_set8(sc,R_AX_PLATFORM_ENABLE,B_AX_PLATFORM_EN); c_clr8(sc,R_AX_PLATFORM_ENABLE,B_AX_PLATFORM_EN); }
    c_set8(sc,R_AX_PLATFORM_ENABLE,B_AX_PLATFORM_EN);
    c_clr32(sc,R_AX_SYS_SDIO_CTRL,B_AX_PCIE_CALIB_EN_V1);
    c_clr32(sc,R_AX_SYS_ISO_CTRL_EXTEND,B_AX_CMAC1_FEN);
    c_set32(sc,R_AX_SYS_ISO_CTRL_EXTEND,B_AX_R_SYM_ISO_CMAC12PP);
    c_clr32(sc,R_AX_AFE_CTRL1,B_AX_R_SYM_WLCMAC1_ALL_PC_EN);
    c_set32(sc,R_AX_SYS_ADIE_PAD_PWR_CTRL,B_AX_SYM_PADPDN_WL_PTA_1P3);
    if(rtw89_write_xtal_si(sc,XTAL_SI_ANAPAR_WL,XTAL_SI_GND_SHDN_WL,XTAL_SI_GND_SHDN_WL)<0)return -1;
    c_set32(sc,R_AX_SYS_ADIE_PAD_PWR_CTRL,B_AX_SYM_PADPDN_WL_RFC_1P3);
    for(unsigned i=0;i<sizeof xs/sizeof xs[0];i++) if(rtw89_write_xtal_si(sc,xs[i][0],xs[i][1],xs[i][2])<0)return -1;
    c_set32(sc,R_AX_PMC_DBG_CTRL2,B_AX_SYSON_DIS_PMCR_AX_WRMSK);
    c_set32(sc,R_AX_SYS_ISO_CTRL,B_AX_ISO_EB2CORE);
    c_clr32(sc,R_AX_SYS_ISO_CTRL,B_AX_PWC_EV2EF_B15);
    sc->io->delay(1000);
    c_clr32(sc,R_AX_SYS_ISO_CTRL,B_AX_PWC_EV2EF_B14);
    c_clr32(sc,R_AX_PMC_DBG_CTRL2,B_AX_SYSON_DIS_PMCR_AX_WRMSK);
    c_set32(sc,R_AX_GPIO0_15_EECS_EESK_LED1_PULL_LOW_EN,B_AX_EECS_EESK_LED1_PULL_LOW);
    c_set32(sc,R_AX_DMAC_FUNC_EN,RTW8852C_DMAC_FUNC_ALL);
    c_set32(sc,R_AX_CMAC_FUNC_EN,RTW89_CMAC_FUNC_ALL);
    c_mask32(sc,R_AX_LED1_FUNC_SEL,B_AX_PINMUX_EESK_FUNC_SEL_V1_MASK,PINMUX_EESK_FUNC_SEL_BT_LOG);
    return 0; }
