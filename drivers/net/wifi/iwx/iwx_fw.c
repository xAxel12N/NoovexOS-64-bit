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
#include "include/nvx/fs/vfs/vfs.h"
#include "include/nvx/mem/heap.h"
#include "include/nvx/core/klog.h"

static u32 iwx_le32(const u8* p){ return (u32)p[0]|((u32)p[1]<<8)|((u32)p[2]<<16)|((u32)p[3]<<24); }
static char* iwx_sc(char* d,const char* s){ while(*s)*d++=*s++; *d=0; return d; }
static char* iwx_ud(char* d,u32 v){ char t[12]; int n=0; do{ t[n++]=(char)('0'+v%10); v/=10; }while(v); while(n)*d++=t[--n]; *d=0; return d; }

int iwx_fw_select(struct iwx_softc* sc){
    u32 t=sc->hw_rev&0xFFF0u, rf=(sc->hw_rf_id>>12)&0xFFFu; int jf=(rf==0x105||rf==0x106);
    sc->pnvmbase=0; sc->fwapi=77;
    switch(sc->device){
    case 0x2723: sc->fwbase="cc-a0"; sc->family=IWX_FAM_22000; sc->integrated=0; break;
    case 0x02f0: case 0x06f0: if(t!=IWX_HW_REV_TYPE_QUZ){ sc->err="unsupported AX201 revision"; return -1; } sc->fwbase="QuZ-a0-hr-b0"; sc->family=IWX_FAM_22000; sc->integrated=1; break;
    case 0xa0f0: case 0x34f0: case 0x3df0: case 0x4df0: case 0x43f0:
        sc->fwbase=t==IWX_HW_REV_TYPE_QU_C0?"Qu-c0-hr-b0":t==IWX_HW_REV_TYPE_QUZ?(jf&&sc->device==0xa0f0?"QuZ-a0-jf-b0":"QuZ-a0-hr-b0"):"Qu-b0-hr-b0";
        sc->family=IWX_FAM_22000; sc->integrated=1; break;
    case 0x2725: case 0x2726: case 0x51f0: case 0x7af0: case 0x7f70: case 0x54f0: case 0x7a70: case 0x51f1:
        sc->fwbase="so-a0-gf-a0"; sc->pnvmbase="so-a0-gf-a0"; sc->family=IWX_FAM_AX210; sc->integrated=sc->device!=0x2725&&sc->device!=0x2726; sc->fwapi=89;
        break;
    case 0x7e40: sc->fwbase="ma-b0-gf-a0"; sc->pnvmbase="ma-b0-gf-a0"; sc->family=IWX_FAM_AX210; sc->integrated=1; sc->fwapi=89; break;
    case 0x7740: sc->fwbase="bz-b0-gf-a0"; sc->pnvmbase="bz-b0-gf-a0"; sc->family=IWX_FAM_BZ; sc->integrated=1; sc->fwapi=92; break;
    default: sc->err="unknown adapter"; return -1;
    }
    sc->umac_off=sc->family>=IWX_FAM_AX210?0x300000u:0;
    return 0;
}

static void iwx_setbit(u32* a,u32 b){ if(b<128)a[b>>5]|=1u<<(b&31); }

int iwx_fw_parse(struct iwx_softc* sc,const u8* d,u32 n){
    sc->nsect=0; sc->iml=0; sc->iml_len=0; sc->n_cmd_versions=0; sc->pnvm_tlv=0; sc->pnvm_tlv_len=0; sc->n_scan_channels=40;
    for(int i=0;i<4;i++){ sc->ucode_api[i]=0; sc->capa[i]=0; }
    if(n<88||iwx_le32(d)!=0||iwx_le32(d+4)!=IWX_TLV_UCODE_MAGIC){ sc->err="not an Intel TLV firmware file"; return -1; }
    u32 ver=iwx_le32(d+72); char* p=iwx_ud(sc->fwver,ver>>24); *p++='.'; p=iwx_ud(p,(ver>>16)&0xFF); *p++='.'; iwx_ud(p,(ver>>8)&0xFF);
    u32 off=88; int sep=0;
    while(n-off>=8){
        u32 ty=iwx_le32(d+off), ln=iwx_le32(d+off+4); off+=8;
        if(ln>n-off){ sc->err="firmware file truncated"; return -1; }
        const u8* v=d+off;
        switch(ty){
        case IWX_UCODE_TLV_SEC_RT:
            if(ln<4||sc->nsect>=IWX_UCODE_SECT_MAX){ sc->err="bad firmware section"; return -1; }
            sc->sect[sc->nsect].devoff=iwx_le32(v); sc->sect[sc->nsect].data=v+4; sc->sect[sc->nsect].len=ln-4;
            if(sc->sect[sc->nsect].devoff==IWX_CPU1_CPU2_SEPARATOR_SECTION||sc->sect[sc->nsect].devoff==IWX_PAGING_SEPARATOR_SECTION)sep++;
            sc->nsect++; break;
        case IWX_UCODE_TLV_API_CHANGES_SET: if(ln==8){ u32 ix=iwx_le32(v), f=iwx_le32(v+4); if(ix<4)for(u32 b=0;b<32;b++) if(f&(1u<<b))iwx_setbit(sc->ucode_api,ix*32+b); } break;
        case IWX_UCODE_TLV_ENABLED_CAPABILITIES: if(ln==8){ u32 ix=iwx_le32(v), f=iwx_le32(v+4); if(ix<4)for(u32 b=0;b<32;b++) if(f&(1u<<b))iwx_setbit(sc->capa,ix*32+b); } break;
        case IWX_UCODE_TLV_N_SCAN_CHANNELS: if(ln==4)sc->n_scan_channels=iwx_le32(v); break;
        case IWX_UCODE_TLV_PHY_SKU: if(ln==4)sc->phy_config=iwx_le32(v); break;
        case IWX_UCODE_TLV_FW_VERSION: if(ln==12){ char* q=iwx_ud(sc->fwver,iwx_le32(v)); *q++='.'; q=iwx_ud(q,iwx_le32(v+4)); *q++='.'; iwx_ud(q,iwx_le32(v+8)); } break;
        case IWX_UCODE_TLV_IML: sc->iml=v; sc->iml_len=ln; break;
        case IWX_UCODE_TLV_PNVM_DATA: if(!sc->pnvm_tlv){ sc->pnvm_tlv=v; sc->pnvm_tlv_len=ln; } break;
        case IWX_UCODE_TLV_CMD_VERSIONS: { u32 k=ln/4; if(k>128)k=128; for(u32 i=0;i<k;i++){ sc->cmd_versions[i].cmd=v[i*4]; sc->cmd_versions[i].group=v[i*4+1]; sc->cmd_versions[i].cmd_ver=v[i*4+2]; sc->cmd_versions[i].notif_ver=v[i*4+3]; } sc->n_cmd_versions=(int)k; } break;
        default: break;
        }
        u32 r=(ln+3)&~3u; if(r>n-off)break; off+=r;
    }
    if(!sc->nsect||sep<1){ sc->err="firmware has no runtime image"; return -1; }
    if(sc->family>=IWX_FAM_AX210&&!sc->iml){ sc->err="firmware has no image loader (IML)"; return -1; }
    return 0;
}

int iwx_cmd_version(struct iwx_softc* sc,u8 grp,u8 cmd){ for(int i=0;i<sc->n_cmd_versions;i++) if(sc->cmd_versions[i].group==grp&&sc->cmd_versions[i].cmd==cmd)return sc->cmd_versions[i].cmd_ver; return 0; }
int iwx_notif_version(struct iwx_softc* sc,u8 grp,u8 cmd){ for(int i=0;i<sc->n_cmd_versions;i++) if(sc->cmd_versions[i].group==grp&&sc->cmd_versions[i].cmd==cmd)return sc->cmd_versions[i].notif_ver; return 0; }

static void iwx_fw_name(char* o,const char* base,u32 api){ char* p=iwx_sc(o,"/lib/firmware/iwlwifi-"); p=iwx_sc(p,base); *p++='-'; p=iwx_ud(p,api); iwx_sc(p,".ucode"); }

int iwx_fw_load_file(struct iwx_softc* sc){
    if(sc->fwraw)return iwx_fw_parse(sc,sc->fwraw,sc->fwsize);
    u32 hi=sc->fwapi+12, lo=sc->family>=IWX_FAM_AX210?59:50;
    const char* bases[2]={ sc->fwbase, sc->family==IWX_FAM_AX210&&sc->device==0x2725?"ty-a0-gf-a0":0 };
    for(int bi=0;bi<2;bi++) if(bases[bi]) for(u32 api=hi;api>=lo;api--){
        char nm[80]; iwx_fw_name(nm,bases[bi],api);
        const vfs_fs_ops* fs=vfs_lookup(nm); if(!fs||!fs->size||!fs->read)continue;
        int sz=fs->size(nm); if(sz<88||sz>8*1024*1024)continue;
        u8* b=(u8*)kmalloc((unsigned long)sz); if(!b){ sc->err="out of memory for firmware"; return -1; }
        int got=0; while(got<sz){ int r=fs->read(nm,(char*)b+got,got,sz-got); if(r<=0)break; got+=r; }
        if(got!=sz){ kfree(b); continue; }
        iwx_sc(sc->fwfile,nm);
        if(iwx_fw_parse(sc,b,(u32)sz)<0){ kfree(b); continue; }
        sc->fwraw=b; sc->fwsize=(u32)sz; if(bi)sc->pnvmbase=bases[bi]; return 0;
    }
    iwx_fw_name(sc->fwfile,sc->fwbase,sc->fwapi);
    { char m[160]; char* p=iwx_sc(m,"iwx: firmware missing - copy "); p=iwx_sc(p,sc->fwfile); iwx_sc(p," (linux-firmware, any API version) to the disk"); klog(m); }
    sc->err="firmware file missing"; return -1;
}
