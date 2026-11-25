/* Derived from OpenBSD sys/dev/pci/if_iwx.c, if_iwxreg.h (rev 1.230 / 1.75). SPDX-License-Identifier: ISC AND BSD-3-Clause */
/*
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
 */

/*-
 * Based on BSD-licensed source modules in the Linux iwlwifi driver,
 * which were used as the reference documentation for this implementation.
 *
 ******************************************************************************
 *
 * This file is provided under a dual BSD/GPLv2 license.  When using or
 * redistributing this file, you may do so under either license.
 *
 * GPL LICENSE SUMMARY
 *
 * Copyright(c) 2017 Intel Deutschland GmbH
 * Copyright(c) 2018 - 2019 Intel Corporation
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of version 2 of the GNU General Public License as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * BSD LICENSE
 *
 * Copyright(c) 2017 Intel Deutschland GmbH
 * Copyright(c) 2018 - 2019 Intel Corporation
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 *  * Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 *  * Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in
 *    the documentation and/or other materials provided with the
 *    distribution.
 *  * Neither the name Intel Corporation nor the names of its
 *    contributors may be used to endorse or promote products derived
 *    from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 *****************************************************************************
 */

/*-
 * Copyright (c) 2007-2010 Damien Bergamini <damien.bergamini@free.fr>
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
#include "drivers/net/wifi/intel/iwx/iwx.h"
#include "include/nvx/arch/x86_64/time/hrtimer.h"
#include "include/nvx/fs/vfs/vfs.h"
#include "include/nvx/mem/heap.h"
#include "include/nvx/mem/pmm_vmm.h"
#include "include/nvx/core/klog.h"
#define IWX_UCODE_TLV_HW_TYPE 58
#define IWX_UCODE_TLV_PNVM_VERSION 62
#define IWX_UCODE_TLV_PNVM_SKU 64
#define IWX_UCODE_TLV_CAPA_FRAGMENTED_PNVM_IMG 32
#define IWX_UREG_DOORBELL_TO_ISR6 0xa05c04
#define IWX_UREG_DOORBELL_TO_ISR6_PNVM (1u<<20)
#define IWX_PNVM_MAX_SIZE (1024u*1024u)
extern u32 iwx_sku_id[3];
extern volatile int iwx_pnvm_done;
static struct iwx_dma iwx_pnvm_tab,iwx_pnvm_seg[IWX_MAX_DRAM_ENTRY],iwx_pnvm_one;
static u32 iwx_pnvm_size; static int iwx_pnvm_nseg,iwx_pnvm_ready;
static u32 iwx_pv32(const u8* p){ return (u32)p[0]|((u32)p[1]<<8)|((u32)p[2]<<16)|((u32)p[3]<<24); }
static u16 iwx_pv16(const u8* p){ return (u16)(p[0]|(p[1]<<8)); }
static int iwx_pdma(struct iwx_dma* d,u32 n){
    if(!d->va||d->sz<n){ u32 pg=(n+4095)/4096; d->va=(u8*)dma_alloc_pages(pg); if(!d->va){ d->sz=0; return -1; } d->pa=(u64)(unsigned long)d->va; d->sz=pg*4096; }
    for(u32 i=0;i<d->sz;i++)d->va[i]=0;
    return 0;
}
static void iwx_pnvm_point(struct iwx_softc* sc){
    struct iwx_prph_scratch* p=(struct iwx_prph_scratch*)sc->scratch.va; if(!p)return;
    p->pnvm_base_addr=iwx_pnvm_nseg?iwx_pnvm_tab.pa:iwx_pnvm_one.pa; p->pnvm_size=iwx_pnvm_size;
    cpu_mb();
}
static int iwx_pnvm_section(struct iwx_softc* sc,const u8* d,u32 len){
    const u8* seg[IWX_MAX_DRAM_ENTRY]; u32 sl[IWX_MAX_DRAM_ENTRY]; int ns=0,match=0; u32 tot=0;
    u16 mt=(u16)((sc->hw_rev&0xFFF0u)>>4), rf=(u16)((sc->hw_rf_id&0x0FFF000u)>>12);
    while(len>=8){
        u32 ty=iwx_pv32(d), ln=iwx_pv32(d+4); len-=8; d+=8;
        if(len<ln)return -1;
        if(ty==IWX_UCODE_TLV_PNVM_SKU)break;
        if(ty==IWX_UCODE_TLV_HW_TYPE&&ln>=4&&!match){ if(iwx_pv16(d)==mt&&iwx_pv16(d+2)==rf)match=1; }
        else if(ty==IWX_UCODE_TLV_SEC_RT&&ln>4&&iwx_pv32(d)!=0xddddeeeeu){ if(ns>=IWX_MAX_DRAM_ENTRY)return -1; seg[ns]=d+4; sl[ns]=ln-4; tot+=ln-4; ns++; }
        u32 r=(ln+3)&~3u; if(r>len)break; len-=r; d+=r;
    }
    if(!match||!tot||tot>IWX_PNVM_MAX_SIZE)return -2;
    iwx_pnvm_nseg=0; iwx_pnvm_size=0;
    if(iwx_fw_capa(sc,IWX_UCODE_TLV_CAPA_FRAGMENTED_PNVM_IMG)){
        if(iwx_pdma(&iwx_pnvm_tab,IWX_MAX_DRAM_ENTRY*8))return -3;
        for(int i=0;i<ns;i++){ if(iwx_pdma(&iwx_pnvm_seg[i],sl[i]))return -3; for(u32 k=0;k<sl[i];k++)iwx_pnvm_seg[i].va[k]=seg[i][k];
            u64 pa=iwx_pnvm_seg[i].pa; for(int b=0;b<8;b++)iwx_pnvm_tab.va[i*8+b]=(u8)(pa>>(b*8)); }
        iwx_pnvm_nseg=ns;
    } else {
        if(iwx_pdma(&iwx_pnvm_one,tot))return -3;
        u8* w=iwx_pnvm_one.va; for(int i=0;i<ns;i++){ for(u32 k=0;k<sl[i];k++)w[k]=seg[i][k]; w+=sl[i]; }
    }
    iwx_pnvm_size=tot; iwx_pnvm_ready=1; iwx_pnvm_point(sc); return 0;
}
static int iwx_pnvm_parse(struct iwx_softc* sc,const u8* d,u32 len){
    while(len>=8){
        u32 ty=iwx_pv32(d), ln=iwx_pv32(d+4); len-=8;
        u32 r=(ln+3)&~3u; if(len<ln||r>len)return -1;
        const u8* v=d+8; d+=8+r; len-=r;
        if(ty==IWX_UCODE_TLV_PNVM_SKU&&ln>=12&&iwx_pv32(v)==iwx_sku_id[0]&&iwx_pv32(v+4)==iwx_sku_id[1]&&iwx_pv32(v+8)==iwx_sku_id[2]){
            int e=iwx_pnvm_section(sc,d,len); if(e==0)return 0; if(e==-3)return -3; }
    }
    return -2;
}
static u8* iwx_pnvm_read(const char* nm,u32* n){
    const vfs_fs_ops* fs=vfs_lookup(nm); if(!fs||!fs->size||!fs->read)return 0;
    int sz=fs->size(nm); if(sz<8||(u32)sz>IWX_PNVM_MAX_SIZE)return 0;
    u8* b=(u8*)kmalloc((unsigned long)sz); if(!b)return 0;
    int got=0; while(got<sz){ int r=fs->read(nm,(char*)b+got,got,sz-got); if(r<=0)break; got+=r; }
    if(got!=sz){ kfree(b); return 0; }
    *n=(u32)sz; return b;
}
static void iwx_pnvm_msg(const char* a,const char* b){ char m[160]; char* p=m; char* e=m+158; while(*a&&p<e)*p++=*a++; while(b&&*b&&p<e)*p++=*b++; *p=0; klog(m); }
int iwx_load_pnvm(struct iwx_softc* sc){
    if(!sc||sc->family<IWX_FAM_AX210||iwx_pnvm_done)return 0;
    if(!iwx_sku_id[0]&&!iwx_sku_id[1]&&!iwx_sku_id[2])return 0;
    if(iwx_pnvm_ready)iwx_pnvm_point(sc);
    else if(sc->pnvm_tlv&&sc->pnvm_tlv_len){ int e=iwx_pnvm_parse(sc,sc->pnvm_tlv,sc->pnvm_tlv_len); if(e==-3){ sc->err="out of DMA memory for PNVM"; return -1; } }
    else if(sc->pnvmbase){
        char nm[96]; char* p=nm; const char* a="/lib/firmware/iwlwifi-"; while(*a)*p++=*a++; a=sc->pnvmbase; while(*a&&p<nm+80)*p++=*a++; a=".pnvm"; while(*a)*p++=*a++; *p=0;
        u32 n=0; u8* b=iwx_pnvm_read(nm,&n);
        if(!b){ static int said; if(!said){ said=1; iwx_pnvm_msg("iwx: PNVM file missing, continuing without it - copy ",nm); } }
        else { int e=iwx_pnvm_parse(sc,b,n); kfree(b);
            if(e==-3){ sc->err="out of DMA memory for PNVM"; return -1; }
            if(e){ static int said2; if(!said2){ said2=1; iwx_pnvm_msg("iwx: no PNVM section for this card in ",nm); } } }
    }
    if(!iwx_nic_lock(sc))return -1;
    iwx_pnvm_done=0;
    iwx_write_prph(sc,IWX_UREG_DOORBELL_TO_ISR6+sc->umac_off,IWX_UREG_DOORBELL_TO_ISR6_PNVM);
    u64 t0=iwx_now_ms();
    for(u32 it=0;!iwx_pnvm_done&&it<200000u&&iwx_now_ms()-t0<2000;it++){ iwx_rx_poll(sc); hr_delay_us(10); }
    iwx_nic_unlock(sc);
    if(!iwx_pnvm_done){ sc->err="firmware did not confirm PNVM within 2 s"; klog("iwx: firmware did not confirm PNVM within 2 s"); return -1; }
    return 0;
}
int iwx_txq_info(struct iwx_softc* sc,int qid,u64* desc,u64* bc){
    if(!sc||qid<1||qid>=IWX_NTXQ||!sc->txq[qid].desc.va)return -1;
    *desc=sc->txq[qid].desc.pa; *bc=sc->txq[qid].bc.pa; return sc->txq[qid].cur;
}
void iwx_txq_reset(struct iwx_softc* sc,int qid){
    if(!sc||qid<1||qid>=IWX_NTXQ)return;
    struct iwx_txq* q=&sc->txq[qid];
    if(q->desc.va)for(u32 i=0;i<q->desc.sz;i++)q->desc.va[i]=0;
    if(q->bc.va)for(u32 i=0;i<q->bc.sz;i++)q->bc.va[i]=0;
    q->cur=0;
}
void iwx_set_ready(struct iwx_softc* sc,int on){ if(sc&&sc->state>=IWX_ST_ALIVE)sc->state=on?IWX_ST_READY:IWX_ST_ALIVE; }
int iwx_use_mld(struct iwx_softc* sc){ return sc&&iwx_fw_capa(sc,110)&&(((sc->hw_rev&0xFFF0u)>>4)==0x44||sc->family>=IWX_FAM_BZ); }
