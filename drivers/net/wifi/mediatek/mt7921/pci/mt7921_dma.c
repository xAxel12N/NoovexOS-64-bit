/* SPDX-License-Identifier: BSD-3-Clause-Clear */
/*
 * Derived from Linux drivers/net/wireless/mediatek/mt76 (dma.c, dma.h, mt792x_dma.c, mt7921/pci.c).
 * Copyright (C) 2016 Felix Fietkau <nbd@nbd.name>
 * Copyright (C) 2020 MediaTek Inc.
 * Licence text: drivers/net/wifi/mediatek/mt7921/LICENCE.BSD-3-Clause-Clear
 */
#include "drivers/net/wifi/mediatek/mt7921/mt7921.h"
#include "drivers/net/wifi/mediatek/mt7921/pci/mt7921_regs.h"
#include "include/nvx/mem/pmm_vmm.h"
#include "include/nvx/arch/x86_64/time/hrtimer.h"

static struct mt_ring mtd_rx_wa;
static u8 mtd_tok[MT_TX_RING];
static u64 mtd_tok_t[MT_TX_RING];
static u8 mtd_cont_mcu,mtd_cont_wa,mtd_cont_data;
static const struct { int hw,n,rx; u32 bufsz,base; } mtd_qcfg[MT_NQ]={
    {MT_TXQ_HW_DATA,MT_TX_RING,0,MT_TXBUF_SIZE,MT_TX_RING_BASE},
    {MT_TXQ_HW_MCU,MT_MCU_RING,0,MT_TXBUF_SIZE,MT_TX_RING_BASE},
    {MT_TXQ_HW_FWDL,MT_FWDL_RING,0,MT_FWDL_CHUNK+MT_TXD_SIZE,MT_TX_RING_BASE},
    {MT_RXQ_HW_DATA,MT_RX_RING,1,MT_RXBUF_SIZE,MT_RX_EVENT_RING_BASE},
    {MT_RXQ_HW_MCU,MT_RX_MCU_RING,1,MT_RXBUF_SIZE,MT_RX_EVENT_RING_BASE},
};

static void mtd_mb(void){ __sync_synchronize(); }
static int mtd_dma(struct mt_dma* d,u32 bytes){
    if(!d->va||d->sz<bytes){ u32 pg=(bytes+4095)/4096; d->va=(u8*)dma_alloc_pages(pg); if(!d->va)return -1; d->pa=(u64)(unsigned long)d->va; d->sz=pg*4096; if(d->pa>>32){ d->va=0; return -1; } }
    for(u32 i=0;i<d->sz;i++)d->va[i]=0;
    return 0;
}
static struct mt_wfdma_desc* mtd_desc(struct mt_ring* r,int i){ return (struct mt_wfdma_desc*)(void*)r->desc.va+i; }
static u32 mtd_bufpa(struct mt_ring* r,int i){ return (u32)(r->buf.pa+(u64)i*r->bufsz); }

static int mtd_ring_setup(struct mt7921_softc* sc,struct mt_ring* r,int hw,int n,int rx,u32 bufsz,u32 base){
    r->n=n; r->bufsz=bufsz; r->regbase=base+(u32)hw*MT_RING_STRIDE; r->hw_idx=hw;
    if(mtd_dma(&r->desc,(u32)n*sizeof(struct mt_wfdma_desc))||mtd_dma(&r->buf,(u32)n*bufsz))return -1;
    for(int i=0;i<n;i++){ struct mt_wfdma_desc* d=mtd_desc(r,i); d->buf1=0; d->info=0;
        if(rx){ d->buf0=mtd_bufpa(r,i); d->ctrl=MT_DMA_CTL_SD_LEN0(bufsz); } else { d->buf0=0; d->ctrl=MT_DMA_CTL_DMA_DONE; } }
    mtd_mb();
    mt7921_w32(sc,r->regbase+0x8,0); mt7921_w32(sc,r->regbase+0xc,0);
    mt7921_w32(sc,r->regbase+0x4,(u32)n); mt7921_w32(sc,r->regbase,(u32)r->desc.pa);
    r->head=r->tail=0;
    if(rx){ r->head=n-1; mt7921_w32(sc,r->regbase+0x8,(u32)(n-1)); }
    return 0;
}

static int mtd_dma_disable(struct mt7921_softc* sc){
    mt7921_rmw(sc,MT_WFDMA0_GLO_CFG,MT_GLO_TX_DMA_EN|MT_GLO_RX_DMA_EN|MT_GLO_DISP_BASE_PTR_CHAIN_EN|MT_GLO_OMIT_TX_INFO|MT_GLO_OMIT_RX_INFO|MT_GLO_OMIT_RX_INFO_PFET2,0);
    if(!mt7921_poll32(sc,MT_WFDMA0_GLO_CFG,MT_GLO_TX_DMA_BUSY|MT_GLO_RX_DMA_BUSY,0,100))return -1;
    mt7921_rmw(sc,MT_WFDMA0_GLO_CFG_EXT0,MT_GLO_EXT0_TX_DMASHDL_EN,0);
    mt7921_rmw(sc,MT_DMASHDL_SW_CONTROL,0,MT_DMASHDL_BYPASS);
    mt7921_rmw(sc,MT_WFDMA0_RST,MT_WFDMA0_RST_DMASHDL_ALL_RST|MT_WFDMA0_RST_LOGIC_RST,0);
    mt7921_rmw(sc,MT_WFDMA0_RST,0,MT_WFDMA0_RST_DMASHDL_ALL_RST|MT_WFDMA0_RST_LOGIC_RST);
    return 0;
}

static void mtd_dma_enable(struct mt7921_softc* sc){
    static const u8 rxp[]={0,2,3,4,5}; static const u8 txp[]={0,1,2,3,4,5,6,16,17};
    for(unsigned i=0;i<sizeof(rxp);i++)mt7921_w32(sc,MT_WFDMA0_RX_EXT(rxp[i]),MT_PREFETCH(0x40*i,4));
    for(unsigned i=0;i<sizeof(txp);i++)mt7921_w32(sc,MT_WFDMA0_TX_EXT(txp[i]),MT_PREFETCH(0x140+0x40*i+(txp[i]>=16?0x40:0),4));
    mt7921_w32(sc,MT_WFDMA0_RST_DTX_PTR,0xFFFFFFFFu);
    mt7921_w32(sc,MT_WFDMA0_PRI_DLY_INT_CFG0,0);
    mt7921_rmw(sc,MT_WFDMA0_GLO_CFG,0,MT_GLO_TX_WB_DDONE|MT_GLO_FIFO_LITTLE_ENDIAN|MT_GLO_CLK_GAT_DIS|MT_GLO_OMIT_TX_INFO|MT_GLO_DMA_SIZE(3)|
        MT_GLO_FIFO_DIS_CHECK|MT_GLO_RX_WB_DDONE|MT_GLO_DISP_BASE_PTR_CHAIN_EN|MT_GLO_OMIT_RX_INFO_PFET2);
    mt7921_rmw(sc,MT_WFDMA0_GLO_CFG,0,MT_GLO_TX_DMA_EN|MT_GLO_RX_DMA_EN);
    mt7921_rmw(sc,MT_WFDMA_DUMMY_CR,0,MT_WFDMA_NEED_REINIT);
    mt7921_w32(sc,MT_WFDMA0_HOST_INT_ENA,0);
}

int mt7921_dma_init(struct mt7921_softc* sc){
    mtd_cont_mcu=mtd_cont_wa=mtd_cont_data=0; for(int i=0;i<MT_TX_RING;i++)mtd_tok[i]=0;
    if(mtd_dma_disable(sc)){ sc->err="WFDMA did not go idle"; return -1; }
    for(int q=0;q<MT_NQ;q++) if(mtd_ring_setup(sc,&sc->q[q],(q==MT_Q_MCU&&sc->gen==3)?15:mtd_qcfg[q].hw,mtd_qcfg[q].n,mtd_qcfg[q].rx,mtd_qcfg[q].bufsz,mtd_qcfg[q].base)){ sc->err="out of DMA memory below 4 GB"; return -1; }
    if(mtd_ring_setup(sc,&mtd_rx_wa,MT_RXQ_HW_MCU_WA,MT_RX_MCU_RING,1,MT_RXBUF_SIZE,MT_RX_EVENT_RING_BASE)){ sc->err="out of DMA memory below 4 GB"; return -1; }
    mt7921_w32(sc,MT_WFDMA0_TX_EXT(0),0x4);
    mtd_dma_enable(sc);
    return 0;
}

void mt7921_dma_stop(struct mt7921_softc* sc){
    if(!sc->mmio)return;
    mt7921_w32(sc,MT_WFDMA0_HOST_INT_ENA,0);
    mt7921_rmw(sc,MT_WFDMA0_GLO_CFG,MT_GLO_TX_DMA_EN|MT_GLO_RX_DMA_EN|MT_GLO_DISP_BASE_PTR_CHAIN_EN|MT_GLO_OMIT_TX_INFO|MT_GLO_OMIT_RX_INFO|MT_GLO_OMIT_RX_INFO_PFET2,0);
    mt7921_poll32(sc,MT_WFDMA0_GLO_CFG,MT_GLO_TX_DMA_BUSY|MT_GLO_RX_DMA_BUSY,0,100);
}

static void mtd_tx_reclaim(struct mt7921_softc* sc,struct mt_ring* r){
    u32 hw=mt7921_r32(sc,r->regbase+0xc);
    if(hw<(u32)r->n)r->tail=(int)hw;
}

int mt7921_tok_enable=1;
#define MT_TOK_TIMEOUT_NS 1000000000ull
int mt7921_tx_token_release(struct mt7921_softc* sc,u32 id){
    id&=0x7fffu; if(id>=MT_TX_RING)return -1;
    if(!mtd_tok[id]){ sc->tok_stale++; return -1; }
    mtd_tok[id]=0; sc->tok_freed++; return 0;
}
int mt7921_tokens_busy(struct mt7921_softc* sc){ (void)sc; int n=0; for(int i=0;i<MT_TX_RING;i++)n+=mtd_tok[i]; return n; }
static int mtd_tok_ready(struct mt7921_softc* sc,int i){
    if(!mtd_tok[i]||!mt7921_tok_enable)return 1;
    if(hr_ns()-mtd_tok_t[i]<MT_TOK_TIMEOUT_NS)return 0;
    mtd_tok[i]=0; sc->tok_forced++;
    if(sc->tok_forced>=32&&(!sc->txfree_events||(sc->tok_freed>=64&&sc->tok_forced>sc->tok_freed/4))){ mt7921_tok_enable=0; for(int k=0;k<MT_TX_RING;k++)mtd_tok[k]=0; }
    return 1;
}
int mt7921_dma_tx(struct mt7921_softc* sc,int qid,const u8* buf,u32 len){
    if(sc->hif)return sc->hif->tx(sc,qid,buf,len);
    if(qid<0||qid>MT_Q_FWDL||!buf||!len)return -1;
    struct mt_ring* r=&sc->q[qid];
    if(!r->n||len>r->bufsz)return -1;
    u64 end=hr_ns()+100000000ull;
    for(;;){ mtd_tx_reclaim(sc,r); if((r->head+1)%r->n!=r->tail&&(qid!=MT_Q_TX||r->head>=MT_TX_RING||mtd_tok_ready(sc,r->head)))break; if(hr_ns()>end)return -1; hr_delay_us(20); }
    if(qid==MT_Q_TX&&(len<=MT_TXD_SIZE||len+MT_TXP_SIZE>r->bufsz))return -1;
    int i=r->head; u8* dst=r->buf.va+(u32)i*r->bufsz;
    struct mt_wfdma_desc* d=mtd_desc(r,i);
    if(qid==MT_Q_TX){
        u32 fl=len-MT_TXD_SIZE,hl=MT_TXD_SIZE+MT_TXP_SIZE,fpa=mtd_bufpa(r,i)+hl;
        for(u32 k=0;k<MT_TXD_SIZE;k++)dst[k]=buf[k];
        u8* txp=dst+MT_TXD_SIZE; for(u32 k=0;k<MT_TXP_SIZE;k++)txp[k]=0;
        u16 id=(u16)((u32)i|0x8000u),l0=(u16)((fl&0xfffu)|0x8000u);
        txp[0]=(u8)id; txp[1]=(u8)(id>>8);
        txp[8]=(u8)fpa; txp[9]=(u8)(fpa>>8); txp[10]=(u8)(fpa>>16); txp[11]=(u8)(fpa>>24);
        txp[12]=(u8)l0; txp[13]=(u8)(l0>>8);
        for(u32 k=0;k<fl;k++)dst[hl+k]=buf[MT_TXD_SIZE+k];
        d->buf0=mtd_bufpa(r,i); d->buf1=0; d->info=0; d->ctrl=MT_DMA_CTL_SD_LEN0(hl)|MT_DMA_CTL_LAST_SEC0;
    } else {
        for(u32 k=0;k<len;k++)dst[k]=buf[k];
        d->buf0=mtd_bufpa(r,i); d->buf1=0; d->info=0; d->ctrl=MT_DMA_CTL_SD_LEN0(len)|MT_DMA_CTL_LAST_SEC0;
    }
    if(qid==MT_Q_TX&&i<MT_TX_RING&&mt7921_tok_enable){ mtd_tok[i]=1; mtd_tok_t[i]=hr_ns(); }
    r->head=(i+1)%r->n;
    mtd_mb();
    mt7921_w32(sc,r->regbase+0x8,(u32)r->head);
    if(qid==MT_Q_TX)sc->tx_pkts++;
    return 0;
}

int mt7921_dma_tx_idle(struct mt7921_softc* sc,int qid,u32 ms){
    if(sc->hif)return sc->hif->tx_idle?sc->hif->tx_idle(sc,qid,ms):1;
    if(qid<0||qid>MT_Q_FWDL)return 0;
    struct mt_ring* r=&sc->q[qid]; u64 end=hr_ns()+(u64)ms*1000000ull;
    for(;;){ mtd_tx_reclaim(sc,r); if(r->tail==r->head)return 1; if(hr_ns()>end)return 0; hr_delay_us(20); }
}

static int mtd_rx_ring(struct mt7921_softc* sc,struct mt_ring* r,int data,u8* cont){
    static u8 busy[3]; int bi=r==&mtd_rx_wa?1:data?2:0,got=0;
    if(!r->n||busy[bi]){ if(r->n)sc->rx_reenter++; return 0; }
    busy[bi]=1;
    for(int budget=r->n;budget>0;budget--){
        int i=r->tail; struct mt_wfdma_desc* d=mtd_desc(r,i);
        u32 c=*(volatile u32*)&d->ctrl;
        if(!(c&MT_DMA_CTL_DMA_DONE))break;
        mtd_mb();
        u32 len=MT_DMA_CTL_LEN0_GET(c); if(len>r->bufsz)len=r->bufsz;
        const u8* b=r->buf.va+(u32)i*r->bufsz;
        int first=!*cont,last=(c&MT_DMA_CTL_LAST_SEC0)!=0; *cont=(u8)!last;
        if(len&&first&&last){ if(data){ sc->rx_pkts++; mt7921_rx_data(sc,b,len); } else { sc->mcu_events++; mt7921_mcu_rx_event(sc,b,len); } }
        d->buf0=mtd_bufpa(r,i); d->buf1=0; d->info=0; mtd_mb(); d->ctrl=MT_DMA_CTL_SD_LEN0(r->bufsz);
        r->tail=(i+1)%r->n; mtd_mb();
        mt7921_w32(sc,r->regbase+0x8,(u32)i);
        got++;
    }
    busy[bi]=0; return got;
}

void mt7921_rx_poll(struct mt7921_softc* sc){
    if(sc->hif){ sc->hif->rx_poll(sc); return; }
    if(!sc->mmio||!sc->q[MT_Q_RX_MCU].n)return;
    sc->last_int=mt7921_r32(sc,MT_WFDMA0_HOST_INT_STA);
    if(sc->last_int!=0xFFFFFFFFu)mt7921_w32(sc,MT_WFDMA0_HOST_INT_STA,sc->last_int);
    mtd_rx_ring(sc,&sc->q[MT_Q_RX_MCU],0,&mtd_cont_mcu);
    mtd_rx_ring(sc,&mtd_rx_wa,0,&mtd_cont_wa);
    mtd_rx_ring(sc,&sc->q[MT_Q_RX_DATA],1,&mtd_cont_data);
}
