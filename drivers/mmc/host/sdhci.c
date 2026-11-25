/*	Derived from OpenBSD: sdhc.c,v 1.78 2024/10/19 21:10:03 hastings Exp	*/
/*
 * Copyright (c) 2006 Uwe Stuehler <uwe@openbsd.org>
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
#include "include/nvx/drivers/bus/pci.h"
#include "include/nvx/drivers/acpi/acpi_dev.h"
#include "include/nvx/arch/x86_64/time/hrtimer.h"
#include "drivers/mmc/mmc.h"
#include "drivers/mmc/host/sdhci_adma.h"
#include "include/nvx/mem/pmm_vmm.h"
#include "include/nvx/core/sysfeat.h"

#define SDHCI_DMA 0x00
#define SDHCI_BLK 0x04
#define SDHCI_ARG 0x08
#define SDHCI_XFER 0x0C
#define SDHCI_RESP 0x10
#define SDHCI_DATA 0x20
#define SDHCI_PRES 0x24
#define SDHCI_HCTL 0x28
#define SDHCI_PWR 0x29
#define SDHCI_CLK 0x2C
#define SDHCI_TMO 0x2E
#define SDHCI_SRST 0x2F
#define SDHCI_NIS 0x30
#define SDHCI_EIS 0x32
#define SDHCI_NIE 0x34
#define SDHCI_EIE 0x36
#define SDHCI_NSE 0x38
#define SDHCI_ESE 0x3A
#define SDHCI_HCTL2 0x3E
#define SDHCI_CAP 0x40
#define SDHCI_CAP2 0x44
#define SDHCI_VER 0xFE
#define SDHCI_ADMA_ADDR 0x58
#define SHC_BB 131072u
#define SHC_NDESC 8

struct sdhci_slot { struct mmc_host h; volatile u8* r; u32 cap, cap2, base_hz, clk_hz; u8 ver; int b, s, f, bar; u32* adt; u8* bb; int adma; u32 ndma, npio, nfall; };
static struct sdhci_slot g_sdhci[MMC_MAX_HOSTS];
int g_sdhci_n=0, g_sdhci_found=0, g_sdhci_fail=0;
static int g_shc_gate=0, g_shc_ev=0;
void sdhci_adma_settle(void){
    if(g_shc_ev>0&&g_shc_gate==1){ sysfeat_ok(SYSF_SD_ADMA); g_shc_gate=2; }
    else if(g_shc_ev<0){ sysfeat_fail(SYSF_SD_ADMA,"ADMA2 transfer error, PIO used instead"); g_shc_gate=0; }
    g_shc_ev=0;
    if(!g_shc_gate&&g_sdhci_n&&sysfeat_get(SYSF_SD_ADMA)&&sysfeat_state(SYSF_SD_ADMA)==SYSF_ST_ON){ int any=0; for(int i=0;i<g_sdhci_n;i++) if(g_sdhci[i].adma)any=1; if(any&&sysfeat_begin(SYSF_SD_ADMA))g_shc_gate=1; } }
static int shc_dma_ok(struct sdhci_slot* s){ return s->adma&&g_shc_gate>0&&g_shc_ev>=0&&sysfeat_get(SYSF_SD_ADMA)&&sysfeat_state(SYSF_SD_ADMA)==SYSF_ST_ON; }
int sdhci_dma_info(int i,u32* ndma,u32* npio,u32* nfall){ if(i<0||i>=g_sdhci_n)return -1; *ndma=g_sdhci[i].ndma; *npio=g_sdhci[i].npio; *nfall=g_sdhci[i].nfall; return g_sdhci[i].adma; }

static inline u8 shc_r8(struct sdhci_slot* s,u32 o){ return *(volatile u8*)(s->r+o); }
static inline u16 shc_r16(struct sdhci_slot* s,u32 o){ return *(volatile u16*)(s->r+o); }
static inline u32 shc_r32(struct sdhci_slot* s,u32 o){ return *(volatile u32*)(s->r+o); }
static inline void shc_w8(struct sdhci_slot* s,u32 o,u8 v){ *(volatile u8*)(s->r+o)=v; }
static inline void shc_w16(struct sdhci_slot* s,u32 o,u16 v){ *(volatile u16*)(s->r+o)=v; }
static inline void shc_w32(struct sdhci_slot* s,u32 o,u32 v){ *(volatile u32*)(s->r+o)=v; }

static int shc_wait16(struct sdhci_slot* s,u32 o,u16 m,u16 want,u32 us){
    for(u32 t=0;t<us;t+=10){ if((shc_r16(s,o)&m)==want)return 0; hr_delay_us(10); }
    return (shc_r16(s,o)&m)==want?0:-1; }

static int shc_srst(struct sdhci_slot* s,u8 m){
    shc_w8(s,SDHCI_SRST,m);
    for(u32 t=0;t<100000;t+=10){ if(!(shc_r8(s,SDHCI_SRST)&m))return 0; hr_delay_us(10); }
    return -1; }

static int sdhci_reset(struct mmc_host* h){
    struct sdhci_slot* s=(struct sdhci_slot*)h->priv;
    if(shc_srst(s,1))return MMC_E_TIMEOUT;
    shc_w8(s,SDHCI_TMO,0x0E);
    shc_w16(s,SDHCI_NIE,0x00FF); shc_w16(s,SDHCI_EIE,0x03FF);
    shc_w16(s,SDHCI_NSE,0); shc_w16(s,SDHCI_ESE,0);
    shc_w32(s,SDHCI_NIS,0xFFFFFFFFu);
    shc_w8(s,SDHCI_HCTL,shc_r8(s,SDHCI_HCTL)&~0x3Eu);
    s->clk_hz=0;
    return MMC_E_OK; }

static int sdhci_card_present(struct mmc_host* h){
    struct sdhci_slot* s=(struct sdhci_slot*)h->priv;
    if(h->caps&MMC_CAP_NONREM)return 1;
    u32 p=0;
    for(int t=0;t<100;t++){ p=shc_r32(s,SDHCI_PRES); if(p&(1u<<17))break; hr_delay_us(1000); }
    return (p&(1u<<16))?1:0; }

static int sdhci_set_power(struct mmc_host* h,u32 vdd){
    struct sdhci_slot* s=(struct sdhci_slot*)h->priv;
    shc_w8(s,SDHCI_PWR,0);
    if(!vdd)return MMC_E_OK;
    u8 sel=0;
    if((vdd&MMC_VDD_33)&&(s->cap&(1u<<24)))sel=7;
    else if((vdd&0x00060000u)&&(s->cap&(1u<<25)))sel=6;
    else if((vdd&MMC_VDD_18)&&(s->cap&(1u<<26)))sel=5;
    if(!sel)return MMC_E_INVAL;
    shc_w8(s,SDHCI_PWR,(u8)(sel<<1)); shc_w8(s,SDHCI_PWR,(u8)((sel<<1)|1));
    hr_delay_us(10000);
    if(!(shc_r8(s,SDHCI_PWR)&1))return MMC_E_IO;
    return MMC_E_OK; }

static int sdhci_set_clock(struct mmc_host* h,u32 hz){
    struct sdhci_slot* s=(struct sdhci_slot*)h->priv;
    shc_w16(s,SDHCI_CLK,0); s->clk_hz=0;
    if(!hz)return MMC_E_OK;
    u32 d=1, maxd=s->ver>=2?2046u:256u;
    if(hz<s->base_hz){ if(s->ver>=2){ d=(s->base_hz+hz-1)/hz; if(d&1)d++; } else { d=2; while(d<maxd&&s->base_hz/d>hz)d<<=1; } }
    if(d>maxd)d=maxd;
    u32 f=d>>1;
    u16 c=(u16)(((f&0xFF)<<8)|(((f>>8)&3)<<6)|1);
    shc_w16(s,SDHCI_CLK,c);
    if(shc_wait16(s,SDHCI_CLK,2,2,20000))return MMC_E_TIMEOUT;
    shc_w16(s,SDHCI_CLK,(u16)(c|4));
    s->clk_hz=s->base_hz/d;
    hr_delay_us(1000);
    return MMC_E_OK; }

static int sdhci_set_width(struct mmc_host* h,int bits){
    struct sdhci_slot* s=(struct sdhci_slot*)h->priv;
    u8 v=shc_r8(s,SDHCI_HCTL)&~0x22u;
    if(bits==8){ if(!(h->caps&MMC_CAP_8BIT))return MMC_E_INVAL; v|=0x20; }
    else if(bits==4)v|=0x02;
    else if(bits!=1)return MMC_E_INVAL;
    shc_w8(s,SDHCI_HCTL,v);
    return MMC_E_OK; }

static int sdhci_set_hs(struct mmc_host* h,int on){
    struct sdhci_slot* s=(struct sdhci_slot*)h->priv;
    if(on&&!(h->caps&MMC_CAP_HS))return MMC_E_INVAL;
    u8 v=shc_r8(s,SDHCI_HCTL)&~0x04u; if(on)v|=0x04;
    shc_w8(s,SDHCI_HCTL,v);
    return MMC_E_OK; }

static int shc_err(struct sdhci_slot* s,u16 e,int data){
    shc_w16(s,SDHCI_EIS,0xFFFF); shc_w16(s,SDHCI_NIS,0xFFFF);
    shc_srst(s,data?6:2);
    if(e&0x11)return MMC_E_TIMEOUT;
    if(e&0x22)return MMC_E_CRC;
    return MMC_E_IO; }

static int shc_wait_int(struct sdhci_slot* s,u16 bit,u32 us,u16* err){
    for(u32 t=0;;t+=5){ u16 n=shc_r16(s,SDHCI_NIS);
        if(n&0x8000){ *err=shc_r16(s,SDHCI_EIS); if(!*err)*err=0x8000; return -1; }
        if(n&bit){ shc_w16(s,SDHCI_NIS,bit); return 0; }
        if(t>=us){ *err=0x0001; return -1; }
        if(!(shc_r32(s,SDHCI_PRES)&(1u<<16))&&!(s->h.caps&MMC_CAP_NONREM)){ *err=0x0001; return -1; }
        hr_delay_us(5); } }

static int shc_cmd(struct sdhci_slot* s,struct mmc_cmd* c,int dma);
static int sdhci_send_cmd(struct mmc_host* h,struct mmc_cmd* c){
    struct sdhci_slot* s=(struct sdhci_slot*)h->priv;
    int data=c->data&&c->blocks&&c->blksz; u32 len=data?c->blksz*c->blocks:0;
    if(data&&len<=SHC_BB&&!(len&3)&&shc_dma_ok(s)){ int e=shc_cmd(s,c,1); if(e==MMC_E_OK){ s->ndma++; if(!g_shc_ev)g_shc_ev=1; return e; }
        if(e==MMC_E_INVAL)return e; s->nfall++; g_shc_ev=-1; }
    if(data)s->npio++;
    return shc_cmd(s,c,0); }
static int shc_cmd(struct sdhci_slot* s,struct mmc_cmd* c,int dma){
    int data=c->data&&c->blocks&&c->blksz;
    u32 inh=1u|((data||(c->flags&MMC_RSP_BUSY))?2u:0u);
    for(u32 t=0;(shc_r32(s,SDHCI_PRES)&inh);t+=10){ if(t>=100000){ shc_srst(s,6); return c->error=MMC_E_TIMEOUT; } hr_delay_us(10); }
    shc_w32(s,SDHCI_NIS,0xFFFFFFFFu);
    u16 cmd=(u16)((c->opcode&0x3F)<<8), mode=0;
    if(!(c->flags&MMC_RSP_PRESENT)){}
    else if(c->flags&MMC_RSP_136)cmd|=1;
    else if(c->flags&MMC_RSP_BUSY)cmd|=3;
    else cmd|=2;
    if(c->flags&MMC_RSP_CRC)cmd|=0x08;
    if(c->flags&MMC_RSP_IDX)cmd|=0x10;
    if(data){
        if(c->blksz>2048||c->blocks>0xFFFF)return c->error=MMC_E_INVAL;
        cmd|=0x20; mode=0x02;
        if(c->dataflags&MMC_DATA_READ)mode|=0x10;
        if(c->blocks>1)mode|=0x20;
        if(dma){ u32 len=c->blksz*c->blocks; if(sdhci_adma2_fill(s->adt,SHC_NDESC,(u64)s->bb,len)<1)return c->error=MMC_E_IO;
            if(!(c->dataflags&MMC_DATA_READ))for(u32 i=0;i<len;i++)s->bb[i]=c->data[i];
            shc_w32(s,SDHCI_ADMA_ADDR,(u32)(u64)s->adt); shc_w32(s,SDHCI_ADMA_ADDR+4,0); shc_w8(s,SDHCI_HCTL,(u8)((shc_r8(s,SDHCI_HCTL)&~0x18u)|0x10u)); mode|=0x01; }
        else shc_w8(s,SDHCI_HCTL,(u8)(shc_r8(s,SDHCI_HCTL)&~0x18u));
        shc_w32(s,SDHCI_BLK,(c->blksz&0xFFF)|(c->blocks<<16));
        shc_w8(s,SDHCI_TMO,0x0E); }
    shc_w32(s,SDHCI_ARG,c->arg);
    shc_w32(s,SDHCI_XFER,((u32)cmd<<16)|mode);
    u16 e=0;
    if(shc_wait_int(s,1,1000000,&e))return c->error=shc_err(s,e,data);
    if(c->flags&MMC_RSP_136){ u32 r[4]; for(int i=0;i<4;i++)r[i]=shc_r32(s,SDHCI_RESP+4*i);
        c->resp[0]=(r[3]<<8)|(r[2]>>24); c->resp[1]=(r[2]<<8)|(r[1]>>24); c->resp[2]=(r[1]<<8)|(r[0]>>24); c->resp[3]=r[0]<<8; }
    else { c->resp[0]=shc_r32(s,SDHCI_RESP); c->resp[1]=c->resp[2]=c->resp[3]=0; }
    if(data&&dma){
        if(shc_wait_int(s,2,3000000,&e))return c->error=shc_err(s,e,1);
        cpu_mb(); if(c->dataflags&MMC_DATA_READ){ u32 len=c->blksz*c->blocks; for(u32 i=0;i<len;i++)c->data[i]=s->bb[i]; } }
    else if(data){
        int rd=(c->dataflags&MMC_DATA_READ)!=0; u8* p=c->data;
        for(u32 b=0;b<c->blocks;b++){
            if(shc_wait_int(s,rd?0x20:0x10,1000000,&e))return c->error=shc_err(s,e,1);
            for(u32 i=0;i<c->blksz;i+=4){
                if(rd){ u32 v=shc_r32(s,SDHCI_DATA); for(u32 k=0;k<4&&i+k<c->blksz;k++)p[i+k]=(u8)(v>>(8*k)); }
                else { u32 v=0; for(u32 k=0;k<4&&i+k<c->blksz;k++)v|=(u32)p[i+k]<<(8*k); shc_w32(s,SDHCI_DATA,v); } }
            p+=c->blksz; }
        if(shc_wait_int(s,2,3000000,&e))return c->error=shc_err(s,e,1); }
    else if(c->flags&MMC_RSP_BUSY){
        if(shc_wait_int(s,2,3000000,&e)){ if(e!=0x0001||(shc_r32(s,SDHCI_PRES)&2))return c->error=shc_err(s,e,1); } }
    return c->error=MMC_E_OK; }

static const struct mmc_host_ops sdhci_ops={ sdhci_reset, sdhci_card_present, sdhci_set_power, sdhci_set_clock, sdhci_set_width, sdhci_set_hs, sdhci_send_cmd };

static volatile u8* shc_map_bar(int b,int sl,int f,int bar){
    u32 lo=pci_cfg(b,sl,f,0x10+4*bar);
    if(lo&1)return 0;
    u64 base=lo&0xFFFFFFF0u;
    if((lo&6)==4&&bar<5)base|=(u64)pci_cfg(b,sl,f,0x14+4*bar)<<32;
    if(!base)return 0;
    if(base>=0x100000000ULL){ if(!acpi_hi_map(base)||((base+0xFFFULL)>>30)!=(base>>30))return 0; }
    else if(base<0x1000u||base>=0xFEC00000u)return 0;
    return (volatile u8*)base; }

static int shc_add_slot(int b,int sl,int f,int bar){
    if(g_sdhci_n>=MMC_MAX_HOSTS)return 0;
    volatile u8* r=shc_map_bar(b,sl,f,bar); if(!r){ g_sdhci_fail=2; return 0; }
    struct sdhci_slot* s=&g_sdhci[g_sdhci_n]; for(u32 i=0;i<sizeof *s;i++)((u8*)s)[i]=0;
    s->r=r; s->b=b; s->s=sl; s->f=f; s->bar=bar;
    if(shc_r32(s,SDHCI_CAP)==0xFFFFFFFFu){ g_sdhci_fail=3; return 0; }
    s->ver=(u8)(shc_r16(s,SDHCI_VER)&0xFF);
    s->cap=shc_r32(s,SDHCI_CAP); s->cap2=s->ver>=2?shc_r32(s,SDHCI_CAP2):0;
    u32 mhz=(s->cap>>8)&(s->ver>=2?0xFFu:0x3Fu);
    if(!mhz){ g_sdhci_fail=4; return 0; }
    s->base_hz=mhz*1000000u;
    struct mmc_host* h=&s->h;
    h->ops=&sdhci_ops; h->priv=s; h->name="sdhci";
    h->caps=MMC_CAP_4BIT;
    if(s->cap&(1u<<21))h->caps|=MMC_CAP_HS;
    if(s->cap&(1u<<18))h->caps|=MMC_CAP_8BIT;
    if(s->ver>=2&&((s->cap>>30)&3)==1)h->caps|=MMC_CAP_NONREM;
    h->ocr_avail=0;
    if(s->cap&(1u<<24))h->ocr_avail|=MMC_VDD_33;
    if(s->cap&(1u<<25))h->ocr_avail|=0x00060000u;
    if(s->cap&(1u<<26))h->ocr_avail|=MMC_VDD_18;
    if(!h->ocr_avail){ g_sdhci_fail=5; return 0; }
    h->f_min=s->base_hz/(s->ver>=2?2046u:256u); if(h->f_min<100000u)h->f_min=100000u;
    h->f_max=s->base_hz; if(h->f_max>((h->caps&MMC_CAP_HS)?50000000u:25000000u))h->f_max=(h->caps&MMC_CAP_HS)?50000000u:25000000u;
    h->max_blocks=0xFFFF;
    if(s->cap&(1u<<19)){ s->adt=(u32*)dma_alloc(SHC_NDESC*8); s->bb=(u8*)dma_alloc_pages(SHC_BB/4096u);
        if(s->adt&&s->bb&&(u64)s->bb+SHC_BB<=0x100000000ULL&&(u64)s->adt+SHC_NDESC*8u<=0x100000000ULL&&!((u64)s->adt&7)&&!((u64)s->bb&3)){ s->adma=1; h->caps|=MMC_CAP_DMA; h->max_blocks=SHC_BB/512u; pci_cfgw(b,sl,f,0x04,(pci_cfg(b,sl,f,0x04)|0x406u)&0xFFFFu); } }
    if(sdhci_reset(h)){ g_sdhci_fail=6; return 0; }
    g_sdhci_n++;
    mmc_register_host(h);
    return 1; }

int sdhci_probe(void){
    if(g_sdhci_n)return g_sdhci_n;
    for(int b=0;b<256;b++) for(int sl=0;sl<32;sl++){
        u32 id0=pci_cfg(b,sl,0,0); if((id0&0xFFFF)==0xFFFF)continue;
        int nf=(pci_cfg(b,sl,0,0x0C)&0x800000)?8:1;
        for(int f=0;f<nf;f++){ u32 id=pci_cfg(b,sl,f,0); if((id&0xFFFF)==0xFFFF)continue;
            u32 cl=pci_cfg(b,sl,f,0x08); if(((cl>>24)&0xFF)!=0x08||((cl>>16)&0xFF)!=0x05)continue;
            u32 pi=(cl>>8)&0xFF; if(pi>1)continue;
            g_sdhci_found++;
            u32 cmd=pci_cfg(b,sl,f,0x04); pci_cfgw(b,sl,f,0x04,((cmd|0x402u)&~0x04u)&0xFFFFu);
            u32 si=pci_cfg(b,sl,f,0x40)&0xFF; int first=(int)(si&7), n=(int)((si>>4)&7)+1;
            if(first+n>6)n=6-first;
            for(int i=0;i<n;i++)shc_add_slot(b,sl,f,first+i); } }
    return g_sdhci_n; }
