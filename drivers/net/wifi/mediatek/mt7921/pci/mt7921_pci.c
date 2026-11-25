/* SPDX-License-Identifier: BSD-3-Clause-Clear */
/*
 * Derived from Linux drivers/net/wireless/mediatek/mt76 (mt7921/pci.c, mt7921/regs.h, mt792x_core.c, mt792x_dma.c,
 * mt792x_regs.h, mt7921/pci_mcu.c, mt7925/pci.c, mt7925/regs.h).
 * Copyright (C) 2020 MediaTek Inc.
 * Licence text: drivers/net/wifi/mediatek/mt7921/LICENCE.BSD-3-Clause-Clear
 */
#include "drivers/net/wifi/mediatek/mt7921/mt7921.h"
#include "drivers/net/wifi/mediatek/mt7921/pci/mt7921_regs.h"
#include "include/nvx/drivers/bus/pci.h"
#include "include/nvx/arch/x86_64/time/hrtimer.h"
#include "include/nvx/core/klog.h"

static const struct { u32 phys,maps,size; } mtp_fixed_map[]={
    {0x820d0000,0x30000,0x10000},{0x820ed000,0x24800,0x00800},{0x820e4000,0x21000,0x00400},{0x820e7000,0x21e00,0x00200},
    {0x820eb000,0x24200,0x00400},{0x820e2000,0x20800,0x00400},{0x820e3000,0x20c00,0x00400},{0x820e5000,0x21400,0x00800},
    {0x00400000,0x80000,0x10000},{0x00410000,0x90000,0x10000},{0x40000000,0x70000,0x10000},{0x54000000,0x02000,0x01000},
    {0x55000000,0x03000,0x01000},{0x58000000,0x06000,0x01000},{0x59000000,0x07000,0x01000},{0x7c000000,0xf0000,0x10000},
    {0x7c020000,0xd0000,0x10000},{0x7c060000,0xe0000,0x10000},{0x80020000,0xb0000,0x10000},{0x81020000,0xc0000,0x10000},
    {0x820c0000,0x08000,0x04000},{0x820c8000,0x0c000,0x02000},{0x820cc000,0x0e000,0x01000},{0x820cd000,0x0f000,0x01000},
    {0x74030000,0x10000,0x10000},{0x820ce000,0x21c00,0x00200},{0x820cf000,0x22000,0x01000},{0x820e0000,0x20000,0x00400},
    {0x820e1000,0x20400,0x00200},{0x820e9000,0x23400,0x00200},{0x820ea000,0x24000,0x00200},{0x820ec000,0x24600,0x00200},
    {0x820f0000,0xa0000,0x00400},{0x820f1000,0xa0600,0x00200},{0x820f2000,0xa0800,0x00400},{0x820f3000,0xa0c00,0x00400},
    {0x820f4000,0xa1000,0x00400},{0x820f5000,0xa1400,0x00800},{0x820f7000,0xa1e00,0x00200},{0x820f9000,0xa3400,0x00200},
    {0x820fa000,0xa4000,0x00200},{0x820fb000,0xa4200,0x00400},{0x820fc000,0xa4600,0x00200},{0x820fd000,0xa4800,0x00800},
};

static const struct { u32 phys,maps,size; } mtp_map3[]={
    {0x830c0000,0x000000,0x01000},{0x54000000,0x002000,0x01000},{0x55000000,0x003000,0x01000},{0x56000000,0x004000,0x01000},
    {0x57000000,0x005000,0x01000},{0x58000000,0x006000,0x01000},{0x59000000,0x007000,0x01000},{0x820c0000,0x008000,0x04000},
    {0x820c8000,0x00c000,0x02000},{0x820cc000,0x00e000,0x02000},{0x74030000,0x010000,0x01000},{0x820e0000,0x020000,0x00400},
    {0x820e1000,0x020400,0x00200},{0x820e2000,0x020800,0x00400},{0x820e3000,0x020c00,0x00400},{0x820e4000,0x021000,0x00400},
    {0x820e5000,0x021400,0x00800},{0x820ce000,0x021c00,0x00200},{0x820e7000,0x021e00,0x00200},{0x820cf000,0x022000,0x01000},
    {0x820e9000,0x023400,0x00200},{0x820ea000,0x024000,0x00200},{0x820eb000,0x024200,0x00400},{0x820ec000,0x024600,0x00200},
    {0x820ed000,0x024800,0x00800},{0x820ca000,0x026000,0x02000},{0x820d0000,0x030000,0x10000},{0x40000000,0x070000,0x10000},
    {0x00400000,0x080000,0x10000},{0x00410000,0x090000,0x10000},{0x820f0000,0x0a0000,0x00400},{0x820f1000,0x0a0600,0x00200},
    {0x820f2000,0x0a0800,0x00400},{0x820f3000,0x0a0c00,0x00400},{0x820f4000,0x0a1000,0x00400},{0x820f5000,0x0a1400,0x00800},
    {0x820f7000,0x0a1e00,0x00200},{0x820f9000,0x0a3400,0x00200},{0x820fa000,0x0a4000,0x00200},{0x820fb000,0x0a4200,0x00400},
    {0x820fc000,0x0a4600,0x00200},{0x820fd000,0x0a4800,0x00800},{0x820c4000,0x0a8000,0x04000},{0x820b0000,0x0ae000,0x01000},
    {0x80020000,0x0b0000,0x10000},{0x81020000,0x0c0000,0x10000},{0x7c020000,0x0d0000,0x10000},{0x7c060000,0x0e0000,0x10000},
    {0x7c000000,0x0f0000,0x10000},{0x70020000,0x1f0000,0x10000},{0x7c500000,0x060000,0x200000},
};
#define MTP_BAR(sc) ((sc)->gen==3?0x200000u:MT_BAR0_SIZE)
static u32 mtp_raw_r(struct mt7921_softc* sc,u32 o){ if(o>=MTP_BAR(sc))return 0xFFFFFFFFu; return *(volatile u32*)(unsigned long)(sc->mmio+o); }
static void mtp_raw_w(struct mt7921_softc* sc,u32 o,u32 v){ if(o<MTP_BAR(sc))*(volatile u32*)(unsigned long)(sc->mmio+o)=v; }

static u32 mtp_map_l1(struct mt7921_softc* sc,u32 addr){
    mtp_raw_w(sc,MT_HIF_REMAP_L1,(mtp_raw_r(sc,MT_HIF_REMAP_L1)&~0xffffu)|(addr>>16));
    mtp_raw_r(sc,MT_HIF_REMAP_L1);
    return MT_HIF_REMAP_BASE_L1+(addr&0xffffu);
}

static u32 mtp_addr3(struct mt7921_softc* sc,u32 addr){
    if(addr<0x200000u)return addr;
    for(unsigned i=0;i<sizeof(mtp_map3)/sizeof(mtp_map3[0]);i++){ if(addr<mtp_map3[i].phys)continue; u32 ofs=addr-mtp_map3[i].phys; if(ofs>mtp_map3[i].size)continue; return mtp_map3[i].maps+ofs; }
    if((addr>=0x18000000u&&addr<0x18c00000u)||(addr>=0x70000000u&&addr<0x78000000u)||(addr>=0x7c000000u&&addr<0x7c400000u))return mtp_map_l1(sc,addr);
    mtp_raw_w(sc,MT_HIF_REMAP_L1,(mtp_raw_r(sc,MT_HIF_REMAP_L1)&~0xffffu)|(0x18500000u>>16)); mtp_raw_w(sc,MT_HIF_REMAP_BASE_L1+0x120u,addr); mtp_raw_r(sc,MT_HIF_REMAP_L1);
    return MT_HIF_REMAP_BASE_L1;
}
u32 mt7921_reg_addr(struct mt7921_softc* sc,u32 addr){
    if(sc->gen==3)return mtp_addr3(sc,addr);
    if(addr<MT_BAR0_SIZE)return addr;
    for(unsigned i=0;i<sizeof(mtp_fixed_map)/sizeof(mtp_fixed_map[0]);i++){
        if(addr<mtp_fixed_map[i].phys)continue;
        u32 ofs=addr-mtp_fixed_map[i].phys; if(ofs>mtp_fixed_map[i].size)continue;
        return mtp_fixed_map[i].maps+ofs;
    }
    if((addr>=0x18000000u&&addr<0x18c00000u)||(addr>=0x70000000u&&addr<0x78000000u)||(addr>=0x7c000000u&&addr<0x7c400000u))return mtp_map_l1(sc,addr);
    return MT_BAR0_SIZE;
}

u32 mt7921_r32(struct mt7921_softc* sc,u32 o){ if(sc->hif)return sc->hif->rr(sc,o); return mtp_raw_r(sc,mt7921_reg_addr(sc,o)); }
void mt7921_w32(struct mt7921_softc* sc,u32 o,u32 v){ if(sc->hif){ sc->hif->wr(sc,o,v); return; } mtp_raw_w(sc,mt7921_reg_addr(sc,o),v); }
u32 mt7921_rmw(struct mt7921_softc* sc,u32 o,u32 clr,u32 set){ if(sc->hif){ u32 v=(sc->hif->rr(sc,o)&~clr)|set; sc->hif->wr(sc,o,v); return v; } u32 a=mt7921_reg_addr(sc,o),v=(mtp_raw_r(sc,a)&~clr)|set; mtp_raw_w(sc,a,v); return v; }
u32 mt7921_l1_r32(struct mt7921_softc* sc,u32 addr){ return mtp_raw_r(sc,mtp_map_l1(sc,addr)); }
int mt7921_poll32(struct mt7921_softc* sc,u32 o,u32 mask,u32 val,u32 ms){
    u64 end=hr_ns()+(u64)ms*1000000ull;
    for(;;){ u32 v=mt7921_r32(sc,o); if(v==0xFFFFFFFFu&&mask!=0xFFFFFFFFu)return 0; if((v&mask)==val)return 1; if(hr_ns()>end)return 0; hr_delay_us(20); }
}

static int mtp_own(struct mt7921_softc* sc,u32 bit,u32 want){
    for(int i=0;i<MT_OWN_RETRY;i++){ mt7921_w32(sc,MT_CONN_ON_LPCTL,bit); if(mt7921_poll32(sc,MT_CONN_ON_LPCTL,PCIE_LPCR_HOST_OWN_SYNC,want,50))return 0; }
    return -1;
}
int mt7921_fw_own(struct mt7921_softc* sc){ return mtp_own(sc,PCIE_LPCR_HOST_SET_OWN,PCIE_LPCR_HOST_OWN_SYNC); }
int mt7921_drv_own(struct mt7921_softc* sc){ return mtp_own(sc,PCIE_LPCR_HOST_CLR_OWN,0); }

int mt7921_wfsys_reset(struct mt7921_softc* sc){
    u32 r=sc->gen==3?0x7c000140u:MT_WFSYS_SW_RST_B;
    mt7921_rmw(sc,r,WFSYS_SW_RST_B,0);
    hr_delay_ms(50);
    mt7921_rmw(sc,r,0,WFSYS_SW_RST_B);
    return mt7921_poll32(sc,r,WFSYS_SW_INIT_DONE,WFSYS_SW_INIT_DONE,500)?0:-1;
}

static int mtp_fail(struct mt7921_softc* sc,const char* e){ sc->err=e; sc->state=MT_ST_HWFAIL; klog2("mt7921: ",e); return -1; }

int mt7921_pci_init(struct mt7921_softc* sc){
    u32 bar=pci_cfg(sc->bus,sc->slot,sc->fn,0x10);
    if(bar&1)return mtp_fail(sc,"BAR0 is not memory");
    u32 hi=(((bar>>1)&3u)==2u)?pci_cfg(sc->bus,sc->slot,sc->fn,0x14):0;
    if(hi||(bar&0xFFFFFFF0u)<0x100000u)return mtp_fail(sc,"card registers above 4 GB or unassigned - not supported yet");
    sc->mmio=bar&0xFFFFFFF0u;
    u32 c=pci_cfg(sc->bus,sc->slot,sc->fn,0x04)&0xFFFFu; pci_cfgw(sc->bus,sc->slot,sc->fn,0x04,c|0x06u|0x400u);
    if(mtp_raw_r(sc,MT_HIF_REMAP_L1)==0xFFFFFFFFu)return mtp_fail(sc,"card does not answer (registers read all ones)");
    if(mt7921_fw_own(sc))return mtp_fail(sc,"firmware own handshake timed out");
    if(mt7921_drv_own(sc))return mtp_fail(sc,"driver own handshake timed out");
    u32 chip=mt7921_l1_r32(sc,MT_HW_CHIPID)&0xFFFFu;
    if(chip==MT_CHIP_7961&&(mt7921_l1_r32(sc,MT_HW_BOUND)&(1u<<7)))chip=MT_CHIP_7920;
    sc->hw_rev=(chip<<16)|(mt7921_l1_r32(sc,MT_HW_REV)&0xffu);
    if(sc->gen==3?chip!=MT_CHIP_7925:(chip!=MT_CHIP_7961&&chip!=MT_CHIP_7920&&chip!=MT_CHIP_7922))return mtp_fail(sc,"unknown MediaTek chip id");
    sc->chip=(u16)chip;
    if(mt7921_wfsys_reset(sc))return mtp_fail(sc,"Wi-Fi subsystem reset timed out");
    mt7921_w32(sc,MT_WFDMA0_HOST_INT_ENA,0);
    mt7921_w32(sc,MT_PCIE_MAC_INT_ENABLE,0);
    return 0;
}
