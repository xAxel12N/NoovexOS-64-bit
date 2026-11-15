/*
 * Derived from OpenBSD sys/dev/pci/if_iwx.c, if_iwxreg.h (rev 1.230 / 1.75).
 * Copyright (c) 2014, 2016 genua gmbh <info@genua.de>
 *   Author: Stefan Sperling <stsp@openbsd.org>
 * Copyright (c) 2014 Fixup Software Ltd.
 * Copyright (c) 2017, 2019, 2020 Stefan Sperling <stsp@openbsd.org>
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
 *
 * Portions: Copyright(c) 2017 Intel Deutschland GmbH, Copyright(c) 2018 - 2019
 * Intel Corporation, used under the BSD-3-Clause branch of the dual BSD/GPLv2
 * licence: Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 * Redistributions of source code must retain the above copyright notice, this
 * list of conditions and the following disclaimer. Redistributions in binary
 * form must reproduce the above copyright notice, this list of conditions and
 * the following disclaimer in the documentation and/or other materials provided
 * with the distribution. Neither the name Intel Corporation nor the names of
 * its contributors may be used to endorse or promote products derived from this
 * software without specific prior written permission. THIS SOFTWARE IS PROVIDED
 * BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND ANY EXPRESS OR IMPLIED
 * WARRANTIES ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR
 * CONTRIBUTORS BE LIABLE FOR ANY DAMAGES ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */
#include "drivers/net/wifi/iwx/iwx.h"
#include "include/nvx/drivers/bus/pci.h"
#include "include/nvx/mem/pmm_vmm.h"
#include "include/nvx/arch/x86_64/time/hrtimer.h"
#include "include/nvx/core/klog.h"

struct iwx_softc g_iwx;
static const struct { u16 dev; const char* name; } iwx_ids[]={
    {0x2723,"Intel Wi-Fi 6 AX200"},{0x02f0,"Intel Wi-Fi 6 AX201"},{0x06f0,"Intel Wi-Fi 6 AX201"},{0xa0f0,"Intel Wi-Fi 6 AX201"},
    {0x34f0,"Intel Wi-Fi 6 AX201"},{0x3df0,"Intel Wi-Fi 6 AX201"},{0x4df0,"Intel Wi-Fi 6 AX201"},{0x43f0,"Intel Wi-Fi 6 AX201"},
    {0x2725,"Intel Wi-Fi 6E AX210"},{0x2726,"Intel Wi-Fi 6E AX211"},{0x51f0,"Intel Wi-Fi 6E AX211"},{0x51f1,"Intel Wi-Fi 6E AX211"},
    {0x54f0,"Intel Wi-Fi 6E AX211"},{0x7a70,"Intel Wi-Fi 6E AX211"},{0x7af0,"Intel Wi-Fi 6E AX211"},{0x7f70,"Intel Wi-Fi 6E AX211"},
    {0x7e40,"Intel Wi-Fi 6E AX210"},{0x7740,"Intel Wi-Fi 6E AX211 (BZ)"},
};
const char* iwx_match(u16 vendor,u16 device){ if(vendor!=0x8086)return 0; for(unsigned i=0;i<sizeof(iwx_ids)/sizeof(iwx_ids[0]);i++) if(iwx_ids[i].dev==device)return iwx_ids[i].name; return 0; }

u32 iwx_r32(struct iwx_softc* sc,u32 o){ return *(volatile u32*)(unsigned long)(sc->mmio+o); }
void iwx_w32(struct iwx_softc* sc,u32 o,u32 v){ *(volatile u32*)(unsigned long)(sc->mmio+o)=v; }
static void iwx_set(struct iwx_softc* sc,u32 o,u32 b){ iwx_w32(sc,o,iwx_r32(sc,o)|b); }
static void iwx_clr(struct iwx_softc* sc,u32 o,u32 b){ iwx_w32(sc,o,iwx_r32(sc,o)&~b); }
static int iwx_poll(struct iwx_softc* sc,u32 reg,u32 bits,u32 mask,int us){ for(;;){ if((iwx_r32(sc,reg)&mask)==(bits&mask))return 1; if(us<10)return 0; us-=10; hr_delay_us(10); } }
u64 iwx_now_ms(void){ return hr_ns()/1000000ull; }
struct iwx_softc* iwx_get_sc(void){ return g_iwx.state>=IWX_ST_ALIVE?&g_iwx:0; }
int iwx_is_ax210(struct iwx_softc* sc){ return sc->family>=IWX_FAM_AX210; }
int iwx_fw_api(struct iwx_softc* sc,int b){ return b>=0&&b<128&&(sc->ucode_api[b>>5]>>(b&31)&1); }
int iwx_fw_capa(struct iwx_softc* sc,int b){ return b>=0&&b<128&&(sc->capa[b>>5]>>(b&31)&1); }

static u32 iwx_prph_mask(struct iwx_softc* sc){ return sc->family>=IWX_FAM_AX210?0x00ffffffu:0x000fffffu; }
static u32 iwx_rprph(struct iwx_softc* sc,u32 a){ iwx_w32(sc,IWX_HBUS_TARG_PRPH_RADDR,(a&iwx_prph_mask(sc))|(3u<<24)); return iwx_r32(sc,IWX_HBUS_TARG_PRPH_RDAT); }
static void iwx_wprph(struct iwx_softc* sc,u32 a,u32 v){ iwx_w32(sc,IWX_HBUS_TARG_PRPH_WADDR,(a&iwx_prph_mask(sc))|(3u<<24)); iwx_w32(sc,IWX_HBUS_TARG_PRPH_WDAT,v); }
u32 iwx_read_prph(struct iwx_softc* sc,u32 a){ return iwx_rprph(sc,a); }
void iwx_write_prph(struct iwx_softc* sc,u32 a,u32 v){ iwx_wprph(sc,a,v); }

int iwx_nic_lock(struct iwx_softc* sc){
    if(sc->nic_locks>0){ sc->nic_locks++; return 1; }
    u32 req,rdy,mask;
    if(sc->family>=IWX_FAM_BZ){ req=IWX_GP_BZ_MAC_ACCESS_REQ; rdy=IWX_GP_MAC_STATUS; mask=IWX_GP_MAC_STATUS; }
    else { req=IWX_GP_MAC_ACCESS_REQ; rdy=IWX_GP_MAC_CLOCK_READY; mask=IWX_GP_MAC_CLOCK_READY|IWX_GP_GOING_TO_SLEEP; }
    iwx_set(sc,IWX_CSR_GP_CNTRL,req); hr_delay_us(2);
    if(iwx_poll(sc,IWX_CSR_GP_CNTRL,rdy,mask,150000)){ sc->nic_locks++; return 1; }
    sc->err="could not wake the card (MAC access)"; return 0;
}
void iwx_nic_unlock(struct iwx_softc* sc){ if(sc->nic_locks>0&&--sc->nic_locks==0)iwx_clr(sc,IWX_CSR_GP_CNTRL,sc->family>=IWX_FAM_BZ?IWX_GP_BZ_MAC_ACCESS_REQ:IWX_GP_MAC_ACCESS_REQ); }

int iwx_probe(void){
    struct iwx_softc* sc=&g_iwx; if(sc->state)return sc->state>=IWX_ST_FOUND;
    if(!pci_n)pci_scan();
    for(int k=0;k<pci_n;k++){ const char* nm=iwx_match(pci_ven[k],pci_dev[k]); if(!nm)continue;
        sc->bus=pci_bus[k]; sc->slot=pci_slot[k]; sc->fn=0; sc->vendor=pci_ven[k]; sc->device=pci_dev[k]; sc->product=nm;
        u32 bar=pci_cfg(sc->bus,sc->slot,0,0x10); if(bar&1){ sc->err="BAR0 is not memory"; sc->state=IWX_ST_HWFAIL; return 1; }
        u32 hi=(((bar>>1)&3u)==2u)?pci_cfg(sc->bus,sc->slot,0,0x14):0;
        if(hi||(bar&0xFFFFFFF0u)<0x100000u){ sc->err="card registers above 4 GB or unassigned - not supported yet"; sc->state=IWX_ST_HWFAIL; return 1; }
        sc->mmio=bar&0xFFFFFFF0u;
        u32 c=pci_cfg(sc->bus,sc->slot,0,0x04)&0xFFFFu; pci_cfgw(sc->bus,sc->slot,0,0x04,c|0x06u|0x400u);
        u32 r=iwx_r32(sc,IWX_CSR_HW_REV); if(r==0xFFFFFFFFu){ sc->err="card does not answer (registers read all ones)"; sc->state=IWX_ST_HWFAIL; return 1; }
        sc->hw_rev=r; sc->hw_rf_id=iwx_r32(sc,IWX_CSR_HW_RF_ID);
        sc->hw_rev=(sc->hw_rev&0xFFF0u)|((((sc->hw_rev<<2)&0xCu)>>2)<<2);
        if(iwx_fw_select(sc)<0){ sc->state=IWX_ST_HWFAIL; return 1; }
        sc->state=IWX_ST_FOUND; return 1;
    }
    return 0;
}

static int iwx_set_hw_ready(struct iwx_softc* sc){
    iwx_set(sc,IWX_CSR_HW_IF_CONFIG_REG,0x00400000u);
    int ok=iwx_poll(sc,IWX_CSR_HW_IF_CONFIG_REG,0x00400000u,0x00400000u,50);
    if(ok)iwx_set(sc,IWX_CSR_MBOX_SET_REG,0x20u);
    return ok;
}
static int iwx_prepare_card_hw(struct iwx_softc* sc){
    if(iwx_set_hw_ready(sc))return 0;
    iwx_set(sc,IWX_CSR_DBG_LINK_PWR_MGMT_REG,0x80000000u); hr_delay_us(1000);
    for(int n=0;n<10;n++){ iwx_set(sc,IWX_CSR_HW_IF_CONFIG_REG,0x08000000u);
        for(int t=0;t<150000;t+=200){ if(iwx_set_hw_ready(sc))return 0; hr_delay_us(200); }
        hr_delay_us(25000); }
    sc->err="card never became ready"; return -1;
}
static void iwx_sw_reset(struct iwx_softc* sc){ if(sc->family>=IWX_FAM_BZ){ iwx_set(sc,IWX_CSR_GP_CNTRL,IWX_GP_SW_RESET); hr_delay_us(20000); } else { iwx_set(sc,IWX_CSR_RESET,0x80u); hr_delay_us(5000); } }
static int iwx_apm_init(struct iwx_softc* sc){
    iwx_set(sc,IWX_CSR_GIO_CHICKEN_BITS,0x00800000u); iwx_set(sc,IWX_CSR_DBG_HPET_MEM_REG,0xFFFF0000u); iwx_set(sc,IWX_CSR_HW_IF_CONFIG_REG,0x00080000u);
    iwx_set(sc,IWX_CSR_GIO_REG,0x2u);
    u32 rdy; if(sc->family>=IWX_FAM_BZ){ iwx_set(sc,IWX_CSR_GP_CNTRL,IWX_GP_MAC_CLOCK_READY|IWX_GP_MAC_INIT); rdy=IWX_GP_MAC_STATUS; } else { iwx_set(sc,IWX_CSR_GP_CNTRL,IWX_GP_INIT_DONE); rdy=IWX_GP_MAC_CLOCK_READY; }
    if(!iwx_poll(sc,IWX_CSR_GP_CNTRL,rdy,rdy,25000)){ sc->err="clock did not stabilise"; return -1; }
    return 0;
}
static int iwx_start_hw(struct iwx_softc* sc){
    if(iwx_prepare_card_hw(sc)<0)return -1;
    if(sc->family==IWX_FAM_22000){ u32 h=iwx_rprph(sc,IWX_HPM_DEBUG); if(h!=0xa5a5a5a0u&&(h&(1u<<12))){ if(iwx_rprph(sc,IWX_PREG_PRPH_WPROT_22000)&(1u<<12)){ sc->err="cannot clear persistence bit"; return -1; } iwx_wprph(sc,IWX_HPM_DEBUG,h&~(1u<<12)); } }
    iwx_sw_reset(sc);
    if(iwx_apm_init(sc)<0)return -1;
    if(iwx_nic_lock(sc)){ iwx_wprph(sc,IWX_UREG_CHICK+sc->umac_off,IWX_UREG_CHICK_MSI_ENABLE); iwx_nic_unlock(sc); }
    sc->rfkill=!(iwx_r32(sc,IWX_CSR_GP_CNTRL)&IWX_GP_HW_RF_KILL_SW);
    return 0;
}

static int iwx_dma(struct iwx_dma* d,u32 bytes){ if(d->va&&d->sz>=bytes){ for(u32 i=0;i<d->sz;i++)d->va[i]=0; return 0; } u32 pg=(bytes+4095)/4096; d->va=(u8*)dma_alloc_pages(pg); if(!d->va)return -1; d->pa=(u64)(unsigned long)d->va; d->sz=pg*4096; return 0; }

static int iwx_alloc(struct iwx_softc* sc){
    int ax=sc->family>=IWX_FAM_AX210;
    if(iwx_dma(&sc->rx_free,IWX_RX_RING*(ax?16:8))||iwx_dma(&sc->rx_used,IWX_RX_RING*(sc->family>=IWX_FAM_BZ?4:ax?32:4))||iwx_dma(&sc->rx_stat,64))return -1;
    for(int i=0;i<IWX_NRXBUF;i++) if(iwx_dma(&sc->rxbuf[i],IWX_RBUF_SIZE))return -1;
    for(int q=0;q<IWX_NTXQ;q++){ struct iwx_txq* t=&sc->txq[q]; t->nbuf=IWX_NCMDBUF; t->bufsz=IWX_CMDBUF_SIZE; t->cur=0;
        if(iwx_dma(&t->desc,IWX_TX_RING*sizeof(struct iwx_tfh_tfd))||iwx_dma(&t->bc,8192)||iwx_dma(&t->buf,(u32)t->nbuf*t->bufsz))return -1; }
    if(iwx_dma(&sc->ctxt,sizeof(struct iwx_ctxt_info))||iwx_dma(&sc->scratch,sizeof(struct iwx_prph_scratch))||iwx_dma(&sc->prph_info,4096))return -1;
    sc->rx_cur=0; sc->rx_widx=0;
    return 0;
}
static void iwx_rx_fill(struct iwx_softc* sc){
    for(int i=0;i<IWX_RX_RING;i++){ u64 pa=sc->rxbuf[i&(IWX_NRXBUF-1)].pa;
        if(sc->family>=IWX_FAM_AX210){ struct iwx_rx_transfer_desc* d=(struct iwx_rx_transfer_desc*)sc->rx_free.va; d[i].rbid=(u16)i; d[i].addr=pa; }
        else ((u64*)sc->rx_free.va)[i]=pa|(u64)(i&0xfff); }
}
static void iwx_rx_wptr(struct iwx_softc* sc,u32 w){ w&=~7u; sc->rx_widx=(int)w; if(sc->family>=IWX_FAM_BZ)iwx_w32(sc,IWX_HBUS_TARG_WRPTR,w|((0u+512u)<<16)); else iwx_w32(sc,IWX_RFH_Q0_FRBDCB_WIDX_TRG,w); }

static int iwx_nsec(struct iwx_softc* sc,int s){ int n=0; while(s<sc->nsect&&sc->sect[s].devoff!=IWX_CPU1_CPU2_SEPARATOR_SECTION&&sc->sect[s].devoff!=IWX_PAGING_SEPARATOR_SECTION){ s++; n++; } return n; }
static int iwx_init_fw_sec(struct iwx_softc* sc,struct iwx_ctxt_dram* cd){
    int lm=iwx_nsec(sc,0), um=iwx_nsec(sc,lm+1), pg=iwx_nsec(sc,lm+um+2);
    if(lm>IWX_MAX_DRAM_ENTRY||um>IWX_MAX_DRAM_ENTRY||pg>IWX_MAX_DRAM_ENTRY){ sc->err="too many firmware sections"; return -1; }
    int idx[3]={0,lm+1,lm+um+2}, cnt[3]={lm,um,pg}; u8* tab[3]={(u8*)cd+512,(u8*)cd,(u8*)cd+1024}; int k=0;
    for(int g=0;g<3;g++) for(int i=0;i<cnt[g];i++,k++){ const struct iwx_fw_sect* s=&sc->sect[idx[g]+i];
        if(!sc->dram_done){ if(iwx_dma(&sc->fwd[k],s->len)){ sc->err="out of DMA memory for firmware"; return -1; } for(u32 j=0;j<s->len;j++)sc->fwd[k].va[j]=s->data[j]; }
        u64 pa=sc->fwd[k].pa; for(int b=0;b<8;b++)tab[g][i*8+b]=(u8)(pa>>(b*8)); }
    sc->nfwd=k; sc->dram_done=1; return 0;
}
static int iwx_ctxt_gen2(struct iwx_softc* sc){
    struct iwx_ctxt_info* c=(struct iwx_ctxt_info*)sc->ctxt.va;
    c->mac_id=(u16)iwx_r32(sc,IWX_CSR_HW_REV); c->version=0; c->size=(u16)(sizeof(*c)/4);
    c->control_flags=(1u<<8)|(9u<<4)|(4u<<9);
    c->free_rbd_addr=sc->rx_free.pa; c->used_rbd_addr=sc->rx_used.pa; c->status_wr_ptr=sc->rx_stat.pa;
    c->cmd_queue_addr=sc->txq[IWX_CMD_QID].desc.pa; c->cmd_queue_size=5;
    if(iwx_init_fw_sec(sc,&c->dram)<0)return -1;
    iwx_w32(sc,IWX_CSR_CTXT_INFO_BA,(u32)sc->ctxt.pa); iwx_w32(sc,IWX_CSR_CTXT_INFO_BA+4,(u32)(sc->ctxt.pa>>32));
    if(!iwx_nic_lock(sc))return -1;
    iwx_wprph(sc,IWX_UREG_CPU_INIT_RUN,1); iwx_nic_unlock(sc); return 0;
}
static int iwx_ctxt_gen3(struct iwx_softc* sc){
    struct iwx_prph_scratch* p=(struct iwx_prph_scratch*)sc->scratch.va; struct iwx_ctxt_info_gen3* c=(struct iwx_ctxt_info_gen3*)sc->ctxt.va;
    if(iwx_dma(&sc->imld,sc->iml_len)){ sc->err="out of DMA memory for IML"; return -1; }
    for(u32 i=0;i<sc->iml_len;i++)sc->imld.va[i]=sc->iml[i];
    p->mac_id=(u16)(sc->family>=IWX_FAM_BZ?sc->hw_rev:iwx_r32(sc,IWX_CSR_HW_REV)); p->size=(u16)(sizeof(*p)/4);
    p->control_flags=(1u<<16)|(1u<<17)|(0xC0000u&((1u<<18)|(1u<<19)));
    p->free_rbd_addr=sc->rx_free.pa;
    if(iwx_init_fw_sec(sc,&p->dram)<0)return -1;
    c->prph_info_base_addr=sc->prph_info.pa; c->prph_scratch_base_addr=sc->scratch.pa; c->prph_scratch_size=sizeof(*p);
    c->cr_head_idx_arr_base_addr=sc->rx_stat.pa; c->tr_tail_idx_arr_base_addr=sc->prph_info.pa+2048; c->cr_tail_idx_arr_base_addr=sc->prph_info.pa+3072;
    c->mtr_base_addr=sc->txq[IWX_CMD_QID].desc.pa; c->mcr_base_addr=sc->rx_used.pa; c->mtr_size=5; c->mcr_size=9;
    iwx_w32(sc,IWX_CSR_CTXT_INFO_ADDR,(u32)sc->ctxt.pa); iwx_w32(sc,IWX_CSR_CTXT_INFO_ADDR+4,(u32)(sc->ctxt.pa>>32));
    iwx_w32(sc,IWX_CSR_IML_DATA_ADDR,(u32)sc->imld.pa); iwx_w32(sc,IWX_CSR_IML_DATA_ADDR+4,(u32)(sc->imld.pa>>32)); iwx_w32(sc,IWX_CSR_IML_SIZE_ADDR,sc->iml_len);
    iwx_set(sc,IWX_CSR_HW_IF_CONFIG_REG,(1u<<1));
    if(!iwx_nic_lock(sc))return -1;
    if(sc->family>=IWX_FAM_BZ){ iwx_w32(sc,IWX_CSR_FUNC_SCRATCH,0x01010101u); iwx_set(sc,IWX_CSR_GP_CNTRL,IWX_GP_ROM_START); }
    else iwx_wprph(sc,IWX_UREG_CPU_INIT_RUN+sc->umac_off,1);
    iwx_nic_unlock(sc); return 0;
}

static void iwx_handle_pkt(struct iwx_softc* sc,const u8* pk,u32 len){
    const struct iwx_rx_packet* p=(const struct iwx_rx_packet*)pk;
    if(p->hdr.code==IWX_ALIVE&&p->hdr.flags==0&&len>=10){ u16 st=(u16)(p->data[0]|(p->data[1]<<8)); sc->alive_status=st; sc->uc_ok=st==IWX_ALIVE_STATUS_OK?1:2; }
    iwx_rx_pkt(sc,pk,len);
}
void iwx_rx_poll(struct iwx_softc* sc){
    if(sc->state<IWX_ST_FOUND||!sc->rx_stat.va)return;
    u16 hw=(u16)((*(volatile u16*)sc->rx_stat.va)&0xfff)&(IWX_RX_RING-1); int n=0;
    while(sc->rx_cur!=hw&&n<IWX_NRXBUF){
        u8* b=sc->rxbuf[sc->rx_cur&(IWX_NRXBUF-1)].va; u32 off=0;
        while(off+8<IWX_RBUF_SIZE){ const struct iwx_rx_packet* p=(const struct iwx_rx_packet*)(b+off);
            u32 lf=p->len_n_flags; if(lf==0x55550000u||(p->hdr.qid==0&&p->hdr.idx==0&&p->hdr.code==0))break;
            u32 ln=(lf&0x3FFFu)+4; if(ln<8||ln>IWX_RBUF_SIZE-off)break;
            iwx_handle_pkt(sc,b+off,ln); sc->rx_pkts++; off+=(ln+0x3Fu)&~0x3Fu; }
        sc->rx_cur=(sc->rx_cur+1)&(IWX_RX_RING-1); n++;
    }
    if(n)iwx_rx_wptr(sc,(u32)((hw+IWX_NRXBUF-8)&(IWX_RX_RING-1)));
}

int iwx_tx_hcmd(struct iwx_softc* sc,u32 id,const void* d,u32 len){
    struct iwx_txq* q=&sc->txq[IWX_CMD_QID]; if(sc->state<IWX_ST_ALIVE)return -1;
    u32 tot=sizeof(struct iwx_cmd_header_wide)+len; if(tot>q->bufsz)return -1;
    int idx=q->cur&(IWX_TX_RING-1); u8* buf=q->buf.va+(u32)(idx&(q->nbuf-1))*q->bufsz; u64 pa=q->buf.pa+(u32)(idx&(q->nbuf-1))*q->bufsz;
    struct iwx_cmd_header_wide* h=(struct iwx_cmd_header_wide*)buf; h->opcode=(u8)id; h->group_id=(u8)(id>>8)?(u8)(id>>8):1; h->idx=(u8)idx; h->qid=IWX_CMD_QID; h->length=(u16)len; h->reserved=0; h->version=0;
    const u8* s=(const u8*)d; for(u32 i=0;i<len;i++)buf[sizeof(*h)+i]=s?s[i]:0;
    struct iwx_tfh_tfd* t=&((struct iwx_tfh_tfd*)q->desc.va)[idx]; u8* tz=(u8*)t; for(u32 i=0;i<sizeof(*t);i++)tz[i]=0;
    t->tbs[0].tb_len=(u16)(tot<IWX_FIRST_TB_SIZE?tot:IWX_FIRST_TB_SIZE); t->tbs[0].addr=pa; t->num_tbs=1;
    if(tot>IWX_FIRST_TB_SIZE){ t->tbs[1].tb_len=(u16)(tot-IWX_FIRST_TB_SIZE); t->tbs[1].addr=pa+IWX_FIRST_TB_SIZE; t->num_tbs=2; }
    cpu_mb();
    q->cur++; iwx_w32(sc,IWX_HBUS_TARG_WRPTR,((u32)IWX_CMD_QID<<16)|(u32)(q->cur&(sc->family>=IWX_FAM_AX210?0xFFFF:(IWX_TX_RING-1))));
    return idx;
}
int iwx_tx_data(struct iwx_softc* sc,int qid,const void* txcmd,u32 tclen,const void* frame,u32 flen){
    if(qid<1||qid>=IWX_NTXQ||sc->state<IWX_ST_READY)return -1;
    struct iwx_txq* q=&sc->txq[qid]; u32 tot=4+tclen+flen; if(tot>q->bufsz)return -1;
    int idx=q->cur&(IWX_TX_RING-1); u8* buf=q->buf.va+(u32)(idx&(q->nbuf-1))*q->bufsz; u64 pa=q->buf.pa+(u32)(idx&(q->nbuf-1))*q->bufsz;
    buf[0]=IWX_TX_CMD; buf[1]=0; buf[2]=(u8)idx; buf[3]=(u8)qid;
    const u8* a=(const u8*)txcmd; for(u32 i=0;i<tclen;i++)buf[4+i]=a[i]; const u8* f=(const u8*)frame; for(u32 i=0;i<flen;i++)buf[4+tclen+i]=f[i];
    struct iwx_tfh_tfd* t=&((struct iwx_tfh_tfd*)q->desc.va)[idx]; u8* tz=(u8*)t; for(u32 i=0;i<sizeof(*t);i++)tz[i]=0;
    t->tbs[0].tb_len=IWX_FIRST_TB_SIZE; t->tbs[0].addr=pa; t->tbs[1].tb_len=(u16)(tot-IWX_FIRST_TB_SIZE); t->tbs[1].addr=pa+IWX_FIRST_TB_SIZE; t->num_tbs=2;
    u32 chunks=(2+2*10+63)/64-1;
    if(sc->family>=IWX_FAM_AX210)((u16*)q->bc.va)[idx]=(u16)((tclen+flen)|(chunks<<14)); else ((u16*)q->bc.va)[idx]=(u16)(((tclen+flen+3)/4)|(chunks<<12));
    cpu_mb();
    q->cur++; iwx_w32(sc,IWX_HBUS_TARG_WRPTR,((u32)qid<<16)|(u32)(q->cur&(sc->family>=IWX_FAM_AX210?0xFFFF:(IWX_TX_RING-1))));
    return idx;
}

void iwx_hw_mac(struct iwx_softc* sc,u8* mac){
    u32 base=sc->family>=IWX_FAM_BZ?0x30u:0x380u; for(int i=0;i<6;i++)mac[i]=0; if(sc->state<IWX_ST_FOUND||!iwx_nic_lock(sc))return;
    for(int pass=0;pass<2;pass++){ u32 a0=iwx_r32(sc,base+(pass?0u:8u)), a1=iwx_r32(sc,base+(pass?4u:12u));
        mac[0]=(u8)(a0>>24); mac[1]=(u8)(a0>>16); mac[2]=(u8)(a0>>8); mac[3]=(u8)a0; mac[4]=(u8)(a1>>8); mac[5]=(u8)a1;
        int z=1,ff=1; for(int i=0;i<6;i++){ if(mac[i])z=0; if(mac[i]!=0xFF)ff=0; }
        if(!z&&!ff&&!(mac[0]&1)&&!(mac[0]==0x02&&mac[1]==0xcc&&mac[2]==0xaa))break; }
    iwx_nic_unlock(sc);
}

int iwx_start(void){
    struct iwx_softc* sc=&g_iwx;
    if(!iwx_probe())return -1;
    if(sc->state==IWX_ST_HWFAIL)return -1;
    if(sc->state>=IWX_ST_ALIVE)return 0;
    sc->err=0;
    if(iwx_fw_load_file(sc)<0){ sc->state=IWX_ST_NOFW; return -1; }
    if(iwx_start_hw(sc)<0||iwx_alloc(sc)<0){ if(!sc->err)sc->err="out of DMA memory"; sc->state=IWX_ST_HWFAIL; return -1; }
    iwx_w32(sc,IWX_CSR_INT,~0u); iwx_w32(sc,IWX_CSR_INT_MASK,0);
    iwx_w32(sc,IWX_CSR_UCODE_DRV_GP1_CLR,0x2u); iwx_w32(sc,IWX_CSR_UCODE_DRV_GP1_CLR,0x4u); iwx_w32(sc,IWX_CSR_INT,~0u);
    if(iwx_apm_init(sc)<0){ sc->state=IWX_ST_HWFAIL; return -1; }
    *(volatile u8*)(unsigned long)(sc->mmio+IWX_CSR_INT_COALESCING)=0x40;
    iwx_set(sc,IWX_CSR_MAC_SHADOW_REG_CTRL,0x800fffffu);
    iwx_w32(sc,IWX_CSR_INT_MASK,IWX_INT_ALIVE|IWX_INT_FH_RX);
    iwx_rx_fill(sc); sc->uc_ok=0; sc->rx_cur=0; sc->txq[0].cur=0;
    if((sc->family>=IWX_FAM_AX210?iwx_ctxt_gen3(sc):iwx_ctxt_gen2(sc))<0){ if(!sc->err)sc->err="context info setup failed"; sc->state=IWX_ST_HWFAIL; return -1; }
    u64 t0=iwx_now_ms(); int kicked=0;
    while(iwx_now_ms()-t0<2000){
        u32 i=iwx_r32(sc,IWX_CSR_INT), hw=iwx_r32(sc,IWX_CSR_MSIX_HW_INT_CAUSES_AD); sc->last_int=i;
        if(i==0xFFFFFFFFu){ sc->err="card fell off the bus"; break; }
        if(i&(IWX_INT_SW_ERR|IWX_INT_HW_ERR)){ sc->err=i&IWX_INT_SW_ERR?"firmware error during boot":"hardware DMA error during boot"; break; }
        if(i)iwx_w32(sc,IWX_CSR_INT,i);
        if(hw!=0xFFFFFFFFu&&(hw&1u))iwx_w32(sc,IWX_CSR_MSIX_HW_INT_CAUSES_AD,1u);
        if(!kicked&&((i&IWX_INT_ALIVE)||(hw!=0xFFFFFFFFu&&(hw&1u))||iwx_now_ms()-t0>400)){ iwx_rx_fill(sc); iwx_rx_wptr(sc,8); kicked=1; }
        if(kicked)iwx_rx_poll(sc);
        if(sc->uc_ok)break;
        hr_delay_us(200);
    }
    iwx_w32(sc,IWX_CSR_INT_MASK,0);
    if(sc->uc_ok==1){ sc->state=IWX_ST_ALIVE; klog("iwx: firmware is alive"); return 0; }
    if(!sc->err)sc->err=sc->uc_ok==2?"firmware ALIVE with bad status":"no ALIVE from firmware within 2 s";
    sc->state=IWX_ST_HWFAIL; { char m[96]; char* p=m; const char* a="iwx: "; while(*a)*p++=*a++; a=sc->err; while(*a&&p<m+94)*p++=*a++; *p=0; klog(m); }
    return -1;
}

static char* iwx_s(char* p,char* e,const char* s){ while(*s&&p<e)*p++=*s++; *p=0; return p; }
static char* iwx_x(char* p,char* e,u32 v,int n){ static const char* H="0123456789abcdef"; for(int i=n-1;i>=0&&p<e;i--)*p++=H[(v>>(i*4))&15]; *p=0; return p; }
void iwx_status(char* out,int cap){
    struct iwx_softc* sc=&g_iwx; char* p=out; char* e=out+cap-1; iwx_probe();
    if(sc->state==IWX_ST_NONE){ iwx_s(p,e,"iwx: no Intel AX200/AX201/AX210/AX211 card found\n"); return; }
    static const char* ST[]={"none","found","firmware missing","failed","firmware alive","ready"};
    p=iwx_s(p,e,"Adapter="); p=iwx_s(p,e,sc->product); p=iwx_s(p,e," [8086:"); p=iwx_x(p,e,sc->device,4); p=iwx_s(p,e,"]\nState="); p=iwx_s(p,e,ST[sc->state]);
    p=iwx_s(p,e,"\nHW_REV=0x"); p=iwx_x(p,e,sc->hw_rev,8); p=iwx_s(p,e," RF_ID=0x"); p=iwx_x(p,e,sc->hw_rf_id,8);
    p=iwx_s(p,e,"\nFamily="); p=iwx_s(p,e,sc->family==IWX_FAM_22000?"22000":sc->family==IWX_FAM_AX210?"AX210":"BZ");
    if(sc->fwfile[0]){ p=iwx_s(p,e,"\nFirmware="); p=iwx_s(p,e,sc->fwfile); }
    if(sc->fwver[0]){ p=iwx_s(p,e," version "); p=iwx_s(p,e,sc->fwver); }
    if(sc->pnvmbase){ p=iwx_s(p,e,"\nPNVM=/lib/firmware/iwlwifi-"); p=iwx_s(p,e,sc->pnvmbase); p=iwx_s(p,e,".pnvm (not loaded yet)"); }
    if(sc->rfkill)p=iwx_s(p,e,"\nRF-kill switch is ON");
    if(sc->err){ p=iwx_s(p,e,"\nLast error: "); p=iwx_s(p,e,sc->err); }
    p=iwx_s(p,e,"\nRX packets="); { char t[12]; int n=0; u32 v=sc->rx_pkts; do{ t[n++]=(char)('0'+v%10); v/=10; }while(v); while(n&&p<e)*p++=t[--n]; }
    iwx_s(p,e,"\n");
}
