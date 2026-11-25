/* SPDX-License-Identifier: BSD-3-Clause-Clear */
/*
 * Derived from Linux drivers/net/wireless/mediatek/mt76 (mt76_connac_mcu.c, mt76_connac_mcu.h, mt792x_core.c,
 * mt792x.h, mt7921/pci_mcu.c, mt7921/mcu.c).
 * Copyright (C) 2020 MediaTek Inc.
 * Licence text: drivers/net/wifi/mediatek/mt7921/LICENCE.BSD-3-Clause-Clear
 */
#include "drivers/net/wifi/mediatek/mt7921/mt7921.h"
#include "drivers/net/wifi/mediatek/mt7921/pci/mt7921_regs.h"
#include <stddef.h>
#include "include/nvx/mem/heap.h"
#include "include/nvx/core/klog.h"

#define MT_PATCH_ADDR 0x200000u
#define MT_PATCH_ADDR2 0x900000u
#define MT_DL_ENCRY_MODE_SEL (1u<<6)
#define MT_SEC_NOT_SUPPORT 0xFFFFFFFFu
#define MT_FWDL_PIECE MT_FWDL_CHUNK

int mt7921_dma_tx_idle(struct mt7921_softc* sc,int qid,u32 ms);

static const struct { u16 chip; const char* patch; const char* ram; } mtf_fw_names[]={
    {MT_CHIP_7961,"/lib/firmware/mediatek/WIFI_MT7961_patch_mcu_1_2_hdr.bin","/lib/firmware/mediatek/WIFI_RAM_CODE_MT7961_1.bin"},
    {MT_CHIP_7920,"/lib/firmware/mediatek/WIFI_MT7961_patch_mcu_1a_2_hdr.bin","/lib/firmware/mediatek/WIFI_RAM_CODE_MT7961_1a.bin"},
    {MT_CHIP_7922,"/lib/firmware/mediatek/WIFI_MT7922_patch_mcu_1_1_hdr.bin","/lib/firmware/mediatek/WIFI_RAM_CODE_MT7922_1.bin"},
    {MT_CHIP_7925,"/lib/firmware/mediatek/mt7925/WIFI_MT7925_PATCH_MCU_1_1_hdr.bin","/lib/firmware/mediatek/mt7925/WIFI_RAM_CODE_MT7925_1_1.bin"},
};

static u32 mtf_le32(const u8* p){ return (u32)p[0]|((u32)p[1]<<8)|((u32)p[2]<<16)|((u32)p[3]<<24); }
static u32 mtf_rd_be32(const u8* p){ return ((u32)p[0]<<24)|((u32)p[1]<<16)|((u32)p[2]<<8)|(u32)p[3]; }
static void mtf_put32(u8* p,u32 v){ p[0]=(u8)v; p[1]=(u8)(v>>8); p[2]=(u8)(v>>16); p[3]=(u8)(v>>24); }
static void mtf_cpy(char* d,const char* s,int n){ int i=0; for(;i<n-1&&s[i];i++)d[i]=s[i]; d[i]=0; }

static int mtf_fail(struct mt7921_softc* sc,const char* e){ sc->err=e; klog2("mt7921: ",e); return -1; }

static int mtf_cmd_status(struct mt7921_softc* sc,u8 cmd,const void* d,u32 len){ return mt7921_mcu_send(sc,MCU_CMD(cmd),d,len,1); }

static int mtf_init_download(struct mt7921_softc* sc,u32 addr,u32 len,u32 mode){
    u8 req[12]; mtf_put32(req,addr); mtf_put32(req+4,len); mtf_put32(req+8,mode);
    u8 cmd=(addr==MT_PATCH_ADDR||addr==MT_PATCH_ADDR2)?MCU_CMD_PATCH_START_REQ:MCU_CMD_TARGET_ADDRESS_LEN_REQ;
    return mt7921_mcu_send(sc,MCU_CMD(cmd),req,sizeof(req),1)<0?-1:0;
}

static int mtf_scatter(struct mt7921_softc* sc,const u8* d,u32 len){
    while(len){ u32 n=len>MT_FWDL_PIECE?MT_FWDL_PIECE:len; if(mt7921_mcu_send_fwdl(sc,d,n)<0)return -1; d+=n; len-=n; }
    return mt7921_dma_tx_idle(sc,MT_Q_FWDL,1000)?0:-1;
}

static u32 mtf_patch_mode(u32 info){
    u32 mode=DL_MODE_NEED_RSP;
    if(info==MT_SEC_NOT_SUPPORT)return mode;
    switch(info>>24){
    case 1: mode|=DL_MODE_ENCRYPT|DL_MODE_RESET_SEC_IV|((info&0xffu)<<1&DL_MODE_KEY_IDX); break;
    case 2: mode|=DL_MODE_ENCRYPT|MT_DL_ENCRY_MODE_SEL|DL_MODE_RESET_SEC_IV; break;
    default: break;
    }
    return mode;
}

static u32 mtf_ram_mode(u8 fs){
    u32 m=DL_MODE_NEED_RSP|((u32)fs&FW_FEATURE_SET_KEY_IDX);
    if(fs&FW_FEATURE_SET_ENCRYPT)m|=DL_MODE_ENCRYPT|DL_MODE_RESET_SEC_IV;
    if(fs&FW_FEATURE_ENCRY_MODE)m|=MT_DL_ENCRY_MODE_SEL;
    return m;
}

static int mtf_read(struct mt7921_softc* sc,const char* path,u8** b,u32* n){
    *b=0; *n=0;
    if(!mt7921_firmware_read||mt7921_firmware_read(path,b,n)<0||!*b){ *b=0; klog2("mt7921: firmware missing - copy from linux-firmware: ",path); sc->state=MT_ST_NOFW; sc->err="firmware file missing"; return -1; }
    return 0;
}

int mt7921_patch_check(const u8* d,u32 n){
    if(!d||n<sizeof(struct mt_patch_hdr))return -1;
    u32 nr=mtf_rd_be32(d+offsetof(struct mt_patch_hdr,desc.n_region));
    if(nr==0||nr>64||sizeof(struct mt_patch_hdr)+(u64)nr*sizeof(struct mt_patch_sec)>n)return -1;
    for(u32 i=0;i<nr;i++){ const u8* s=d+sizeof(struct mt_patch_hdr)+i*sizeof(struct mt_patch_sec);
        if((mtf_rd_be32(s)&PATCH_SEC_TYPE_MASK)!=PATCH_SEC_TYPE_INFO)return -1;
        u32 offs=mtf_rd_be32(s+4),len=mtf_rd_be32(s+offsetof(struct mt_patch_sec,info.len));
        if((u64)offs+len>n)return -1; }
    return (int)nr;
}

static int mtf_load_patch(struct mt7921_softc* sc,const char* path){
    u8 req[4]={PATCH_SEM_GET,0,0,0};
    int sem=mtf_cmd_status(sc,MCU_CMD_PATCH_SEM_CONTROL,req,4);
    if(sem==PATCH_IS_DL){ mtf_cpy(sc->patchver,"already loaded",sizeof(sc->patchver)); return 0; }
    if(sem!=PATCH_NOT_DL_SEM_SUCCESS)return mtf_fail(sc,"could not get the patch semaphore");
    u8* d; u32 n; int ret=-1;
    if(mtf_read(sc,path,&d,&n)==0){
        int nr=mt7921_patch_check(d,n);
        if(nr<0)mtf_fail(sc,"patch file is damaged");
        else {
            mtf_cpy(sc->builddate,(const char*)d,17);
            u32 v=mtf_rd_be32(d+offsetof(struct mt_patch_hdr,hw_sw_ver)); char* p=sc->patchver; *p++='0'; *p++='x';
            for(int k=7;k>=0;k--){ *p++="0123456789abcdef"[(v>>(k*4))&15]; } *p=0;
            ret=0;
            for(int i=0;i<nr&&!ret;i++){ const u8* s=d+sizeof(struct mt_patch_hdr)+(u32)i*sizeof(struct mt_patch_sec);
                u32 addr=mtf_rd_be32(s+offsetof(struct mt_patch_sec,info.addr)),len=mtf_rd_be32(s+offsetof(struct mt_patch_sec,info.len));
                u32 info=mtf_rd_be32(s+offsetof(struct mt_patch_sec,info.sec_key_idx)),offs=mtf_rd_be32(s+4);
                if(mtf_init_download(sc,addr,len,mtf_patch_mode(info)))ret=mtf_fail(sc,"patch download request failed");
                else if(mtf_scatter(sc,d+offs,len))ret=mtf_fail(sc,"sending the patch timed out"); }
            if(!ret){ u8 fin[4]={0,0,0,0}; if(mtf_cmd_status(sc,MCU_CMD_PATCH_FINISH_REQ,fin,4)!=0)ret=mtf_fail(sc,"patch start failed"); }
        }
        kfree(d);
    }
    req[0]=PATCH_SEM_REL;
    if(mtf_cmd_status(sc,MCU_CMD_PATCH_SEM_CONTROL,req,4)!=PATCH_REL_SEM_SUCCESS&&!ret)ret=mtf_fail(sc,"could not release the patch semaphore");
    return ret;
}

int mt7921_ram_check(const u8* d,u32 n){
    if(!d||n<sizeof(struct mt_fw_trailer))return -1;
    const struct mt_fw_trailer* t=(const struct mt_fw_trailer*)(const void*)(d+n-sizeof(*t));
    u32 nr=t->n_region; if(!nr||(u64)nr*sizeof(struct mt_fw_region)+sizeof(*t)>n)return -1;
    const u8* reg0=d+n-sizeof(*t)-nr*sizeof(struct mt_fw_region); u64 off=0;
    for(u32 i=0;i<nr;i++){ off+=mtf_le32(reg0+i*sizeof(struct mt_fw_region)+offsetof(struct mt_fw_region,len)); if(off>n)return -1; }
    return (int)nr;
}

static int mtf_load_ram(struct mt7921_softc* sc,const char* path){
    u8* d; u32 n;
    if(mtf_read(sc,path,&d,&n))return -1;
    int nr=mt7921_ram_check(d,n),ret=0;
    if(nr<0){ kfree(d); return mtf_fail(sc,"RAM code file is damaged"); }
    const struct mt_fw_trailer* t=(const struct mt_fw_trailer*)(const void*)(d+n-sizeof(*t));
    { int k=0; for(;k<10&&k<(int)sizeof(sc->fwver)-1&&t->fw_ver[k];k++)sc->fwver[k]=t->fw_ver[k]; sc->fwver[k]=0; }
    { int k=0; for(;k<15&&k<(int)sizeof(sc->builddate)-1&&t->build_date[k];k++)sc->builddate[k]=t->build_date[k]; sc->builddate[k]=0; }
    const u8* reg0=d+n-sizeof(*t)-(u32)nr*sizeof(struct mt_fw_region);
    u32 off=0,override=0;
    for(int i=0;i<nr&&!ret;i++){ const u8* r=reg0+(u32)i*sizeof(struct mt_fw_region);
        u32 addr=mtf_le32(r+offsetof(struct mt_fw_region,addr)),len=mtf_le32(r+offsetof(struct mt_fw_region,len)); u8 fs=r[offsetof(struct mt_fw_region,feature_set)];
        if(!(fs&FW_FEATURE_NON_DL)){
            if(fs&FW_FEATURE_OVERRIDE_ADDR)override=addr;
            if(mtf_init_download(sc,addr,len,mtf_ram_mode(fs)))ret=mtf_fail(sc,"RAM download request failed");
            else if(mtf_scatter(sc,d+off,len))ret=mtf_fail(sc,"sending the RAM code timed out");
        }
        off+=len; }
    kfree(d);
    if(ret)return ret;
    u8 st[8]; mtf_put32(st,override?FW_START_OVERRIDE:0); mtf_put32(st+4,override);
    if(mt7921_mcu_send(sc,MCU_CMD(MCU_CMD_FW_START_REQ),st,8,1)<0)return mtf_fail(sc,"firmware start command failed");
    return 0;
}

int mt7921_fw_load(struct mt7921_softc* sc){
    const char *patch=0,*ram=0;
    for(unsigned i=0;i<sizeof(mtf_fw_names)/sizeof(mtf_fw_names[0]);i++) if(mtf_fw_names[i].chip==sc->chip){ patch=mtf_fw_names[i].patch; ram=mtf_fw_names[i].ram; }
    if(!patch)return mtf_fail(sc,"no firmware known for this chip");
    if(sc->hif&&sc->hif->fw_pre){ if(sc->hif->fw_pre(sc)<0)return mtf_fail(sc,"MCU power-on failed"); }
    else {
    mt7921_w32(sc,MT_TOP_LPCR_HOST_BAND0,MT_TOP_LPCR_HOST_DRV_OWN);
    if(!mt7921_poll32(sc,MT_TOP_LPCR_HOST_BAND0,MT_TOP_LPCR_HOST_FW_OWN,0,500))return mtf_fail(sc,"driver own (LPCR) timed out");
    mt7921_rmw(sc,MT_PCIE_MAC_PM,0,MT_PCIE_MAC_PM_L0S_DIS);
    { u8 pw[4]={1,0,0,0}; mt7921_mcu_send(sc,MCU_CMD(MCU_CMD_NIC_POWER_CTRL),pw,4,0); }
    if(!mt7921_poll32(sc,MT_CONN_ON_MISC,MT_TOP_MISC_FW_STATE,MT_TOP_MISC2_FW_PWR_ON,1000))klog("mt7921: MCU is not ready for firmware download, trying anyway");
    }
    if(mtf_load_patch(sc,patch)||mtf_load_ram(sc,ram)){ if(sc->hif&&sc->hif->fw_post)sc->hif->fw_post(sc); return -1; }
    if(!mt7921_poll32(sc,MT_CONN_ON_MISC,MT_TOP_MISC2_FW_N9_RDY,MT_TOP_MISC2_FW_N9_RDY,1500)){ if(sc->hif&&sc->hif->fw_post)sc->hif->fw_post(sc); return mtf_fail(sc,"firmware did not report ready"); }
    if(sc->hif&&sc->hif->fw_post)sc->hif->fw_post(sc);
    sc->state=MT_ST_FWOK;
    return 0;
}
