/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * Derived from Linux drivers/net/wireless/realtek/rtw89/rtw8852b.c (rtw8852b_pwr_on_func) and mac.c (rtw89_mac_write_xtal_si_ax);
 * GPL-2.0 OR BSD-3-Clause, used under BSD-3-Clause.
 * Copyright(c) 2019-2022  Realtek Corporation
 * Licence text: drivers/net/wifi/realtek/rtw89/LICENCE.BSD-3-Clause
 */
#include "drivers/net/wifi/realtek/rtw89/rtw89.h"

static u32 r89_r32(struct rtw89_softc* sc,u32 o){ return sc->io->rd(sc,o,4); }
static void r89_w32(struct rtw89_softc* sc,u32 o,u32 v){ sc->io->wr(sc,o,v,4); }
static void r89_set32(struct rtw89_softc* sc,u32 o,u32 b){ r89_w32(sc,o,r89_r32(sc,o)|b); }
static void r89_clr32(struct rtw89_softc* sc,u32 o,u32 b){ r89_w32(sc,o,r89_r32(sc,o)&~b); }
static void r89_mask32(struct rtw89_softc* sc,u32 o,u32 m,u32 v){ u32 s=0; while(!((m>>s)&1))s++; r89_w32(sc,o,(r89_r32(sc,o)&~m)|((v<<s)&m)); }
static void r89_set8(struct rtw89_softc* sc,u32 o,u8 b){ sc->io->wr(sc,o,(sc->io->rd(sc,o,1)|b)&0xFF,1); }
static void r89_clr8(struct rtw89_softc* sc,u32 o,u8 b){ sc->io->wr(sc,o,sc->io->rd(sc,o,1)&(u32)~b&0xFF,1); }
static int r89_poll(struct rtw89_softc* sc,u32 o,u32 m,u32 want,u32 step,u32 total){
    for(u32 t=0;;t+=step){ sc->polls++; if((r89_r32(sc,o)&m)==want)return 0; if(t>=total)break; sc->io->delay(step); }
    sc->err_reg=o; return -1; }

int rtw89_write_xtal_si(struct rtw89_softc* sc,u8 off,u8 val,u8 mask){
    sc->xtal_writes++;
    r89_w32(sc,R_AX_WLAN_XTAL_SI_CTRL,(u32)off|((u32)val<<8)|((u32)mask<<16)|B_AX_WL_XTAL_SI_CMD_POLL);
    if(r89_poll(sc,R_AX_WLAN_XTAL_SI_CTRL,B_AX_WL_XTAL_SI_CMD_POLL,0,50,50000)<0){ sc->err="XTAL SI write did not complete"; return -1; }
    return 0; }

int rtw89_read_cv(struct rtw89_softc* sc){ u32 v=r89_r32(sc,R_AX_SYS_CFG1); if(v==0xFFFFFFFFu)return -1; sc->cv=(u8)((v&B_AX_CHIP_VER_MASK)>>12); return sc->cv; }

int rtw8852b_pwr_on(struct rtw89_softc* sc){
    static const u8 xs[][3]={
        {XTAL_SI_ANAPAR_WL,XTAL_SI_SHDN_WL,XTAL_SI_SHDN_WL},{XTAL_SI_ANAPAR_WL,XTAL_SI_OFF_WEI,XTAL_SI_OFF_WEI},{XTAL_SI_ANAPAR_WL,XTAL_SI_OFF_EI,XTAL_SI_OFF_EI},
        {XTAL_SI_ANAPAR_WL,0,XTAL_SI_RFC2RF},{XTAL_SI_ANAPAR_WL,XTAL_SI_PON_WEI,XTAL_SI_PON_WEI},{XTAL_SI_ANAPAR_WL,XTAL_SI_PON_EI,XTAL_SI_PON_EI},
        {XTAL_SI_ANAPAR_WL,0,XTAL_SI_SRAM2RFC},{XTAL_SI_SRAM_CTRL,0,XTAL_SI_SRAM_DIS},{XTAL_SI_XTAL_XMD_2,0,XTAL_SI_LDO_LPS},{XTAL_SI_XTAL_XMD_4,0,XTAL_SI_LPS_CAP} };
    sc->err=0;
    sc->io->wr(sc,R_AX_SPS_ANA_ON_CTRL2,RTL8852B_DEFAULT_SPS_ANA,2);
    r89_clr32(sc,R_AX_SYS_PW_CTRL,B_AX_AFSM_WLSUS_EN|B_AX_AFSM_PCIE_SUS_EN);
    r89_set32(sc,R_AX_SYS_PW_CTRL,B_AX_DIS_WLBT_PDNSUSEN_SOPC);
    r89_set32(sc,R_AX_WLLPS_CTRL,B_AX_DIS_WLBT_LPSEN_LOPC);
    r89_clr32(sc,R_AX_SYS_PW_CTRL,B_AX_APDM_HPDN);
    r89_clr32(sc,R_AX_SYS_PW_CTRL,B_AX_APFM_SWLPS);
    if(r89_poll(sc,R_AX_SYS_PW_CTRL,B_AX_RDY_SYSPWR,B_AX_RDY_SYSPWR,1000,20000)<0){ sc->err="power-on: system power not ready"; return -1; }
    r89_set32(sc,R_AX_AFE_LDO_CTRL,B_AX_AON_OFF_PC_EN);
    if(r89_poll(sc,R_AX_AFE_LDO_CTRL,B_AX_AON_OFF_PC_EN,B_AX_AON_OFF_PC_EN,1000,20000)<0){ sc->err="power-on: AON power cut did not latch"; return -1; }
    r89_mask32(sc,R_AX_SPS_DIG_OFF_CTRL0,B_AX_C1_L1_MASK,1);
    r89_mask32(sc,R_AX_SPS_DIG_OFF_CTRL0,B_AX_C3_L1_MASK,3);
    r89_set32(sc,R_AX_SYS_PW_CTRL,B_AX_EN_WLON);
    r89_set32(sc,R_AX_SYS_PW_CTRL,B_AX_APFN_ONMAC);
    if(r89_poll(sc,R_AX_SYS_PW_CTRL,B_AX_APFN_ONMAC,0,1000,20000)<0){ sc->err="power-on: MAC power state machine stuck"; return -1; }
    for(int i=0;i<2;i++){ r89_set8(sc,R_AX_PLATFORM_ENABLE,B_AX_PLATFORM_EN); r89_clr8(sc,R_AX_PLATFORM_ENABLE,B_AX_PLATFORM_EN); }
    r89_set8(sc,R_AX_PLATFORM_ENABLE,B_AX_PLATFORM_EN);
    r89_clr32(sc,R_AX_SYS_SDIO_CTRL,B_AX_PCIE_CALIB_EN_V1);
    r89_set32(sc,R_AX_SYS_ADIE_PAD_PWR_CTRL,B_AX_SYM_PADPDN_WL_PTA_1P3);
    if(rtw89_write_xtal_si(sc,XTAL_SI_ANAPAR_WL,XTAL_SI_GND_SHDN_WL,XTAL_SI_GND_SHDN_WL)<0)return -1;
    r89_set32(sc,R_AX_SYS_ADIE_PAD_PWR_CTRL,B_AX_SYM_PADPDN_WL_RFC_1P3);
    for(unsigned i=0;i<sizeof xs/sizeof xs[0];i++) if(rtw89_write_xtal_si(sc,xs[i][0],xs[i][1],xs[i][2])<0)return -1;
    r89_set32(sc,R_AX_PMC_DBG_CTRL2,B_AX_SYSON_DIS_PMCR_AX_WRMSK);
    r89_set32(sc,R_AX_SYS_ISO_CTRL,B_AX_ISO_EB2CORE);
    r89_clr32(sc,R_AX_SYS_ISO_CTRL,B_AX_PWC_EV2EF_B15);
    sc->io->delay(1000);
    r89_clr32(sc,R_AX_SYS_ISO_CTRL,B_AX_PWC_EV2EF_B14);
    r89_clr32(sc,R_AX_PMC_DBG_CTRL2,B_AX_SYSON_DIS_PMCR_AX_WRMSK);
    r89_set32(sc,R_AX_DMAC_FUNC_EN,RTW89_DMAC_FUNC_ALL);
    r89_set32(sc,R_AX_CMAC_FUNC_EN,RTW89_CMAC_FUNC_ALL);
    r89_mask32(sc,R_AX_EECS_EESK_FUNC_SEL,B_AX_PINMUX_EESK_FUNC_SEL_MASK,PINMUX_EESK_FUNC_SEL_BT_LOG);
    return 0; }
