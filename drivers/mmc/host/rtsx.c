/* SPDX-License-Identifier: ISC */
/*	Port of OpenBSD: rtsx.c,v 1.22 2020/02/18, rtsxreg.h,v 1.5 2020/08/24, rtsx_pci.c,v 1.16 2023/04/13	*/
/*
 * Copyright (c) 2006 Uwe Stuehler <uwe@openbsd.org>
 * Copyright (c) 2012 Stefan Sperling <stsp@openbsd.org>
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */
#include "include/nvx/base.h"
#include "include/nvx/core/klog.h"
#include "include/nvx/lib/base/fmt.h"
#include "include/nvx/drivers/bus/pci.h"
#include "include/nvx/mem/pmm_vmm.h"
#include "include/nvx/arch/x86_64/time/hrtimer.h"
#include "drivers/mmc/mmc.h"

#define RX_HCBAR 0x00
#define RX_HCBCTLR 0x04
#define RX_HDBAR 0x08
#define RX_HDBCTLR 0x0C
#define RX_HAIMR 0x10
#define RX_BIPR 0x14
#define RX_BIER 0x18
#define RX_START_CMD (1u<<31)
#define RX_HW_AUTO_RSP (1u<<30)
#define RX_STOP_CMD (1u<<28)
#define RX_TRIG_DMA (1u<<31)
#define RX_DMA_READ (1u<<29)
#define RX_STOP_DMA (1u<<28)
#define RX_ADMA_MODE (2u<<26)
#define RX_TRANS_OK (1u<<29)
#define RX_TRANS_FAIL (1u<<28)
#define RX_SD_INT (1u<<25)
#define RX_SD_WP (1u<<19)
#define RX_SD_EXIST (1u<<16)

#define R_FPDCTL 0xFC00
#define R_CLK_CTL 0xFC02
#define R_CLK_DIV 0xFC03
#define R_SSC_DIV_N_0 0xFC0F
#define R_SSC_CTL1 0xFC11
#define R_SSC_CTL2 0xFC12
#define R_RCCTL 0xFC14
#define R_OLT_LED_CTL 0xFC1E
#define R_GPIO_CTL 0xFC1F
#define R_CARD_CLK_SOURCE 0xFC2E
#define R_PPBUF_BASE2 0xFA00
#define R_CARD_PWR_CTL 0xFD50
#define R_CARD_SHARE_MODE 0xFD52
#define R_CARD_STOP 0xFD54
#define R_CARD_OE 0xFD55
#define R_CARD_GPIO_DIR 0xFD57
#define R_CARD_GPIO 0xFD58
#define R_CARD_DATA_SOURCE 0xFD5B
#define R_CARD_SELECT 0xFD5C
#define R_CARD_PULL_CTL1 0xFD60
#define R_CARD_PULL_CTL2 0xFD61
#define R_CARD_PULL_CTL3 0xFD62
#define R_CARD_CLK_EN 0xFD69
#define R_SD_CFG1 0xFDA0
#define R_SD_CFG2 0xFDA1
#define R_SD_STAT1 0xFDA3
#define R_SD_BUS_STAT 0xFDA5
#define R_SD_SAMPLE_POINT_CTL 0xFDA7
#define R_SD_PUSH_POINT_CTL 0xFDA8
#define R_SD_CMD0 0xFDA9
#define R_SD_CMD4 0xFDAD
#define R_SD_BYTE_CNT_L 0xFDAF
#define R_SD_BYTE_CNT_H 0xFDB0
#define R_SD_BLOCK_CNT_L 0xFDB1
#define R_SD_BLOCK_CNT_H 0xFDB2
#define R_SD_TRANSFER 0xFDB3
#define R_IRQSTAT0 0xFE21
#define R_DMATC0 0xFE28
#define R_DMACTL 0xFE2C
#define R_RBCTL 0xFE34
#define R_CFGADDR0 0xFE35
#define R_CFGADDR1 0xFE36
#define R_CFGDATA0 0xFE37
#define R_CFGRWCTL 0xFE3B
#define R_PHY_RWCTL 0xFE3C
#define R_PHY_DATA0 0xFE3D
#define R_PHY_DATA1 0xFE3E
#define R_PHY_ADDR 0xFE3F
#define R_PETXCFG 0xFE49
#define R_CHANGE_LINK_STATE 0xFE5B
#define R_PERST_GLITCH_WIDTH 0xFE5C
#define R_SD30_DRIVE_SEL 0xFE5E
#define R_HOST_SLEEP_STATE 0xFE60
#define R_NFTS_TX_CTRL 0xFE72
#define R_PWR_GATE_CTRL 0xFE75
#define R_LDO_PWR_SEL 0xFE78
#define R_DUMMY_REG 0xFE90
#define R_LDO_VCC_CFG1 0xFF73

#define F_5209 0x04
#define F_5229 0x08
#define F_TYPE_C 0x10
#define F_525A 0x20

#define RX_CMDBUF 1024
#define RX_DATABUF (128u*512u)

typedef struct { struct mmc_host h; volatile u8* mm; u32* cmd; u8* data; u64* adma; int flags, b, s, f; u16 dev; char nm[16]; } rtsx_sc;
static rtsx_sc g_rtsx[2]; static int g_rtsx_n;
static u16 g_rtsx_unsup;

static char* rx_hex(char* q,u16 v){ for(int i=12;i>=0;i-=4)*q++="0123456789ABCDEF"[(v>>i)&15]; return q; }
static u32 rx_r4(rtsx_sc* c,u32 o){ return *(volatile u32*)(c->mm+o); }
static void rx_w4(rtsx_sc* c,u32 o,u32 v){ *(volatile u32*)(c->mm+o)=v; }
static void rx_us(u32 us){ if(hr_precise()){ hr_delay_us(us); return; } while(us--)io_wait(); }

static int rx_read(rtsx_sc* c,u16 a,u8* v){ rx_w4(c,RX_HAIMR,0x80000000u|((u32)(a&0x3FFF)<<16)); u32 r=0x80000000u;
    for(int t=0;t<1024&&(r&0x80000000u);t++){ r=rx_r4(c,RX_HAIMR); }
    *v=(u8)r; return (r&0x80000000u)?-1:0; }
static int rx_write(rtsx_sc* c,u16 a,u8 m,u8 v){ rx_w4(c,RX_HAIMR,0xC0000000u|((u32)(a&0x3FFF)<<16)|((u32)m<<8)|v);
    for(int t=0;t<1024;t++){ u32 r=rx_r4(c,RX_HAIMR); if(!(r&0x80000000u))return v!=(u8)r?-1:0; } return -1; }
#define RW(a,v) do{ if(rx_write(c,a,0xFF,v))return MMC_E_IO; }while(0)
#define RSET(a,v) do{ if(rx_write(c,a,v,v))return MMC_E_IO; }while(0)
#define RCLR(a,v) do{ if(rx_write(c,a,v,0))return MMC_E_IO; }while(0)

static int rx_write_phy(rtsx_sc* c,u8 a,u16 v){ RW(R_PHY_DATA0,(u8)v); RW(R_PHY_DATA1,(u8)(v>>8)); RW(R_PHY_ADDR,a); RW(R_PHY_RWCTL,0x81);
    for(int t=0;t<100000;t++){ u8 r; if(rx_read(c,R_PHY_RWCTL,&r))return MMC_E_IO; if(!(r&0x80))return 0; } return MMC_E_TIMEOUT; }
static int rx_read_cfg(rtsx_sc* c,u8 fn,u16 a,u32* v){ RW(R_CFGADDR0,(u8)a); RW(R_CFGADDR1,(u8)(a>>8)); RW(R_CFGRWCTL,0x80|((fn&3)<<4));
    int t; u8 r=0x80; for(t=0;t<1024&&(r&0x80);t++)if(rx_read(c,R_CFGRWCTL,&r))return MMC_E_IO; if(r&0x80)return MMC_E_TIMEOUT;
    u32 x=0; for(int i=3;i>=0;i--){ u8 d; if(rx_read(c,(u16)(R_CFGDATA0+i),&d))return MMC_E_IO; x=(x<<8)|d; } *v=x; return 0; }

static void rx_soft_reset(rtsx_sc* c){ rx_w4(c,RX_HCBCTLR,RX_STOP_CMD); rx_write(c,R_CARD_STOP,0x44,0x44); rx_w4(c,RX_HDBCTLR,RX_STOP_DMA); rx_write(c,R_DMACTL,0x80,0x80); rx_write(c,R_RBCTL,0x80,0x80); }

static int rx_init(rtsx_sc* c){
    if(c->flags&F_5229){ u8 v; if(rx_read(c,R_DUMMY_REG,&v))return MMC_E_IO; v&=15; if(v==2)c->flags|=F_TYPE_C; else if(v>3)return MMC_E_INVAL; }
    RCLR(R_NFTS_TX_CTRL,0x02);
    u32 st=rx_r4(c,RX_BIPR); rx_w4(c,RX_BIPR,st);
    rx_w4(c,RX_BIER,RX_TRANS_OK|RX_TRANS_FAIL|(1u<<25));
    RCLR(R_FPDCTL,0x01); rx_us(200);
    if(rx_write_phy(c,0,(c->flags&F_5209)?0xB966:0xBA42))return MMC_E_IO;
    RSET(R_CLK_DIV,0x07);
    RCLR(R_HOST_SLEEP_STATE,0x03);
    RCLR(R_CARD_CLK_EN,0x1E);
    RCLR(R_CHANGE_LINK_STATE,0x02|0x08|0x04);
    RW(R_SD30_DRIVE_SEL,0x01);
    RW(R_SSC_CTL1,0x40|0x10); RW(R_SSC_CTL2,0x12);
    RSET(R_CHANGE_LINK_STATE,0x10); RSET(R_IRQSTAT0,0x20);
    RW(R_PERST_GLITCH_WIDTH,0x80);
    RCLR(R_RCCTL,0x01);
    RSET(R_PETXCFG,0x08);
    if(c->flags&F_5209){ RW(R_CARD_GPIO,0x03); RW(R_CARD_GPIO_DIR,0x03); }
    else { RSET(R_GPIO_CTL,0x02); RCLR(R_LDO_PWR_SEL,0x03); RSET(R_LDO_PWR_SEL,0x01); RSET(R_OLT_LED_CTL,0x02); }
    return 0; }

static int rx_stop_clock(rtsx_sc* c){ RCLR(R_CARD_CLK_EN,0x1E); RSET(R_SD_BUS_STAT,0x40); return 0; }
static int rx_power_off(rtsx_sc* c){
    if(rx_stop_clock(c))return MMC_E_IO;
    RCLR(R_CARD_OE,0x1E);
    u8 d3=0xD5; if(c->flags&F_5209)RSET(R_PWR_GATE_CTRL,0x06); else { RCLR(R_PWR_GATE_CTRL,0x06); if(c->flags&F_TYPE_C)d3=0xE5; }
    RSET(R_CARD_PWR_CTL,0x03); RCLR(R_CARD_PWR_CTL,0x10);
    RW(R_CARD_PULL_CTL1,0x55); RW(R_CARD_PULL_CTL2,0x55); RW(R_CARD_PULL_CTL3,d3); return 0; }
static int rx_power_on(rtsx_sc* c){
    if((c->flags&F_525A)&&rx_write(c,R_LDO_VCC_CFG1,0x07,0x07))return MMC_E_IO;
    RW(R_CARD_SELECT,0x02); RW(R_CARD_SHARE_MODE,0x04); RSET(R_CARD_CLK_EN,0x04);
    RW(R_CARD_PULL_CTL1,0xAA); RW(R_CARD_PULL_CTL2,0xAA); RW(R_CARD_PULL_CTL3,(c->flags&F_TYPE_C)?0xD9:0xE9);
    RSET(R_CARD_PWR_CTL,0x01); if(c->flags&F_5209)RSET(R_PWR_GATE_CTRL,0x04); else RSET(R_PWR_GATE_CTRL,0x02);
    rx_us(200);
    RCLR(R_CARD_PWR_CTL,0x03); if(c->flags&F_5209)RCLR(R_PWR_GATE_CTRL,0x06); else RSET(R_PWR_GATE_CTRL,0x04);
    RW(R_CARD_OE,0x04); return 0; }
static int rx_width(rtsx_sc* c,int w){ return rx_write(c,R_SD_CFG1,0x03,w==8?2:w==4?1:0)?MMC_E_IO:0; }

static int rx_card_present(struct mmc_host* h){ rtsx_sc* c=(rtsx_sc*)h->priv; u32 s=rx_r4(c,RX_BIPR); if(s==0xFFFFFFFFu)return 0; if(s&RX_SD_INT)rx_w4(c,RX_BIPR,RX_SD_INT); return (s&RX_SD_EXIST)?1:0; }
static int rx_reset(struct mmc_host* h){ rtsx_sc* c=(rtsx_sc*)h->priv; if(rx_card_present(h))rx_soft_reset(c); return rx_init(c); }
static int rx_set_power(struct mmc_host* h,u32 ocr){ rtsx_sc* c=(rtsx_sc*)h->priv;
    if(rx_power_off(c))return MMC_E_IO;
    rx_us(200);
    if(!ocr){ rx_init(c); return 0; } if(!(ocr&MMC_VDD_33))return MMC_E_INVAL;
    if(rx_power_on(c))return MMC_E_IO;
    return rx_width(c,1); }
static int rx_set_clock(struct mmc_host* h,u32 hz){ rtsx_sc* c=(rtsx_sc*)h->priv;
    if(!hz)return rx_stop_clock(c);
    u8 n=100; int div;
    if(hz>=50000000u)div=2; else if(hz>=25000000u)div=3; else { n=80; div=4; }
    if(div==4)RSET(R_SD_CFG1,0x80); else RCLR(R_SD_CFG1,0xC0);
    RCLR(R_SD_CFG1,0x0C); RSET(R_CLK_CTL,0x01);
    RW(R_CARD_CLK_SOURCE,0x00|0x04|0x20); RCLR(R_SD_SAMPLE_POINT_CTL,0x08); RW(R_SD_PUSH_POINT_CTL,0x00);
    RW(R_CLK_DIV,(u8)((div<<4)|7)); RCLR(R_SSC_CTL1,0x80); RCLR(R_SSC_CTL2,0x07); RW(R_SSC_DIV_N_0,n); RSET(R_SSC_CTL1,0x80);
    rx_us(100); RCLR(R_CLK_CTL,0x01); return 0; }
static int rx_set_width(struct mmc_host* h,int w){ return rx_width((rtsx_sc*)h->priv,w); }
static int rx_set_hs(struct mmc_host* h,int on){ (void)h; (void)on; return 0; }

static void rx_hc(u32* b,int* n,u32 cmd,u16 reg,u8 m,u8 d){ if(*n<RX_CMDBUF/4)b[(*n)++]=((cmd&3)<<30)|((u32)(reg&0x3FFF)<<16)|((u32)m<<8)|d; }
static void rx_hc_send(rtsx_sc* c,int n){ rx_w4(c,RX_HCBAR,(u32)(u64)c->cmd); rx_w4(c,RX_HCBCTLR,((u32)(n*4)&0xFFFFFFu)|RX_START_CMD|RX_HW_AUTO_RSP); }
static int rx_wait(rtsx_sc* c,u32 ms){
    for(u32 t=0;t<ms*20u;t++){ u32 s=rx_r4(c,RX_BIPR); if(s==0xFFFFFFFFu)return MMC_E_NOCARD;
        if(s&(RX_TRANS_OK|RX_TRANS_FAIL)){ rx_w4(c,RX_BIPR,s&(RX_TRANS_OK|RX_TRANS_FAIL)); if(!(s&RX_SD_EXIST))return MMC_E_NOCARD; return (s&RX_TRANS_FAIL)?MMC_E_IO:0; }
        rx_us(50); }
    rx_soft_reset(c); return MMC_E_TIMEOUT; }
static u8 rx_rsp_type(u32 f){ if(!(f&MMC_RSP_PRESENT))return 0x04; if(f&MMC_RSP_136)return 0x02; if(f&MMC_RSP_BUSY)return 0x09; if(!(f&MMC_RSP_CRC))return 0x05; return 0x01; }
static u32 rx_be(const u8* p){ return ((u32)p[0]<<24)|((u32)p[1]<<16)|((u32)p[2]<<8)|p[3]; }

static int rx_xfer(rtsx_sc* c,struct mmc_cmd* m){
    u32 len=m->blksz*m->blocks; int rd=(m->dataflags&MMC_DATA_READ)!=0;
    if(!len||len>RX_DATABUF)return MMC_E_INVAL;
    if(!rd)for(u32 i=0;i<len;i++)c->data[i]=m->data[i];
    u8 cfg2=0x20|(rd?0:(0x80|0x04)); int n=0; u32* b=c->cmd; for(int i=0;i<RX_CMDBUF/4;i++)b[i]=0;
    rx_hc(b,&n,1,R_SD_CFG2,0xFF,cfg2);
    rx_hc(b,&n,1,R_SD_BYTE_CNT_L,0xFF,(u8)m->blksz); rx_hc(b,&n,1,R_SD_BYTE_CNT_H,0xFF,(u8)(m->blksz>>8));
    rx_hc(b,&n,1,R_SD_BLOCK_CNT_L,0xFF,(u8)m->blocks); rx_hc(b,&n,1,R_SD_BLOCK_CNT_H,0xFF,(u8)(m->blocks>>8));
    rx_hc(b,&n,1,R_CARD_DATA_SOURCE,0x01,0x00);
    rx_hc(b,&n,1,R_IRQSTAT0,0x80,0x80);
    for(int i=3;i>=0;i--)rx_hc(b,&n,1,(u16)(R_DMATC0+i),0xFF,(u8)(len>>(8*i)));
    rx_hc(b,&n,1,R_DMACTL,0x03|0x30,(rd?0x02:0x00)|0x01|0x20);
    rx_hc(b,&n,1,R_SD_TRANSFER,0xFF,(rd?0x05:0x01)|0x80);
    rx_hc(b,&n,2,R_SD_TRANSFER,0x40,0x40);
    cpu_mb();
    rx_hc_send(c,n);
    c->adma[0]=((u64)(u32)(u64)c->data<<32)|((u64)(len&0xFFFFFFu)<<12)|0x20|0x02|0x01;
    cpu_mb();
    rx_w4(c,RX_HDBAR,(u32)(u64)c->adma); rx_w4(c,RX_HDBCTLR,RX_ADMA_MODE|RX_TRIG_DMA|(rd?RX_DMA_READ:0));
    int e=rx_wait(c,2000+m->blocks*20u);
    if(e){ u8 s1; if(!rx_read(c,R_SD_STAT1,&s1)&&(s1&0xE0))e=MMC_E_CRC; return e; }
    if(rd){ cpu_mb(); for(u32 i=0;i<len;i++)m->data[i]=c->data[i]; }
    return 0; }

static int rx_send_cmd(struct mmc_host* h,struct mmc_cmd* m){ rtsx_sc* c=(rtsx_sc*)h->priv;
    m->error=0; for(int i=0;i<4;i++)m->resp[i]=0;
    if(m->opcode==5){ m->error=MMC_E_INVAL; return MMC_E_INVAL; }
    u8 rt=rx_rsp_type(m->flags); u32* b=c->cmd; int n=0; for(int i=0;i<RX_CMDBUF/4;i++)b[i]=0;
    rx_hc(b,&n,1,R_SD_CMD0,0xFF,(u8)(0x40|m->opcode));
    for(int i=0;i<4;i++)rx_hc(b,&n,1,(u16)(R_SD_CMD0+1+i),0xFF,(u8)(m->arg>>(24-8*i)));
    rx_hc(b,&n,1,R_SD_CFG2,0xFF,rt);
    rx_hc(b,&n,1,R_CARD_DATA_SOURCE,0x01,0x01);
    rx_hc(b,&n,1,R_SD_TRANSFER,0xFF,0x08|0x80);
    rx_hc(b,&n,2,R_SD_TRANSFER,0x60,0x60);
    if(rt==0x02){ for(u16 r=0;r<=16;r++)rx_hc(b,&n,0,(u16)(R_PPBUF_BASE2+r),0,0); }
    else if(rt!=0x04){ for(u16 r=R_SD_CMD0;r<=R_SD_CMD4;r++)rx_hc(b,&n,0,r,0,0); }
    cpu_mb();
    rx_hc_send(c,n);
    int e=rx_wait(c,(m->flags&MMC_RSP_BUSY)?3000:200);
    if(e){ m->error=e; return e; }
    cpu_mb();
    const u8* p=(const u8*)b;
    if(rt==0x02){ for(int i=0;i<4;i++)m->resp[i]=rx_be(p+2+4*i); }
    else if(rt!=0x04)m->resp[0]=rx_be(p+2);
    if(m->data&&m->blocks){ e=rx_xfer(c,m); if(e){ m->error=e; return e; } }
    return 0; }

static const struct mmc_host_ops rx_ops={ rx_reset, rx_card_present, rx_set_power, rx_set_clock, rx_set_width, rx_set_hs, rx_send_cmd };

static int rx_chip_flags(u16 d){ switch(d){ case 0x5209:return F_5209; case 0x5229: case 0x5249:return F_5229; case 0x525A:return F_525A;
    case 0x5227: case 0x522A: case 0x5286: case 0x5287: case 0x5289:return 0; default:return -1; } }
static int rx_is_reader(u16 d){ switch(d){ case 0x5209: case 0x5227: case 0x5228: case 0x5229: case 0x522A: case 0x5249: case 0x524A: case 0x525A: case 0x5260: case 0x5261: case 0x5264: case 0x5286: case 0x5287: case 0x5289:return 1; default:return 0; } }

static void rx_attach(int bus,int sl,int fn,u16 dev){
    int fl=rx_chip_flags(dev); if(fl<0){ g_rtsx_unsup=dev; char m[80],*q=scopy(m,"rtsx: Realtek card reader 10EC:"); q=rx_hex(q,dev); q=scopy(q," found, no init sequence yet"); *q=0; klog(m); return; }
    if(g_rtsx_n>=2)return;
    int bar=(dev==0x525A)?0x14:0x10; u32 lo=pci_cfg(bus,sl,fn,bar); if(lo&1)return;
    u64 base=lo&0xFFFFFFF0u; if((lo&6)==4)base|=(u64)pci_cfg(bus,sl,fn,bar+4)<<32; if(!base)return;
    if(base>=0x100000000ULL){ extern u64 acpi_hi_map(u64); if(!acpi_hi_map(base))return; }
    rtsx_sc* c=&g_rtsx[g_rtsx_n];
    c->cmd=(u32*)dma_alloc(RX_CMDBUF); c->adma=(u64*)dma_alloc(64); c->data=(u8*)dma_alloc_pages(RX_DATABUF/4096u);
    if(!c->cmd||!c->adma||!c->data)return;
    if((u64)c->data+RX_DATABUF>0x100000000ULL||(u64)c->cmd>0xFFFFFFFFULL)return;
    u32 cmd=pci_cfg(bus,sl,fn,4); pci_cfgw(bus,sl,fn,4,(cmd&0xFFFFu)|0x406u);
    pci_aspm_off(bus,sl,fn);
    c->mm=(volatile u8*)base; c->b=bus; c->s=sl; c->f=fn; c->dev=dev; c->flags=fl;
    if(rx_r4(c,RX_BIPR)==0xFFFFFFFFu){ klog("rtsx: register window reads all-ones, skipped"); return; }
    if(rx_init(c)){ klog("rtsx: chip init failed"); rx_w4(c,RX_BIER,0); return; }
    u32 sc=0; (void)rx_read_cfg(c,0,0x724,&sc);
    char* q=scopy(c->nm,"rtsx"); q=rx_hex(q,dev); *q=0;
    c->h.ops=&rx_ops; c->h.priv=c; c->h.name=c->nm; c->h.caps=MMC_CAP_4BIT|MMC_CAP_DMA; c->h.ocr_avail=MMC_VDD_33;
    c->h.f_min=400000; c->h.f_max=50000000; c->h.max_blocks=RX_DATABUF/512u;
    g_rtsx_n++;
    char m[96]; q=scopy(m,"rtsx: Realtek RTS"); q=rx_hex(q,dev); q=scopy(q," card reader ready"); if(rx_card_present(&c->h))q=scopy(q,", card inserted"); *q=0; klog(m);
    mmc_register_host(&c->h); }

int rtsx_probe(void){
    for(int bus=0;bus<256;bus++)for(int sl=0;sl<32;sl++)for(int fn=0;fn<8;fn++){
        u32 id=pci_cfg(bus,sl,fn,0); if((id&0xFFFF)==0xFFFF){ if(!fn)break; continue; }
        if((id&0xFFFF)==0x10EC&&rx_is_reader((u16)(id>>16)))rx_attach(bus,sl,fn,(u16)(id>>16));
        if(!fn&&!(pci_cfg(bus,sl,0,0x0C)&0x00800000u))break; }
    return g_rtsx_n; }
int rtsx_count(void){ return g_rtsx_n; }
u16 rtsx_unsupported(void){ return g_rtsx_unsup; }
