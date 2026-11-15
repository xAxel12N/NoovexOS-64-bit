#include "include/nvx/base.h"
#include "include/nvx/mem/pmm_vmm.h"
#include "include/nvx/lib/js/js.h"
#include "include/nvx/shell/terminal.h"
#include "include/nvx/fs/fat/fat_ata.h"
#include "include/nvx/drivers/net/wifi/rtw_efuse.h"
#include "include/nvx/drivers/net/wifi/rtw8821ce.h"
#include "include/nvx/drivers/net/wifi/rtw_chip.h"
#include "include/nvx/net/wifi/wifi_hw.h"
#include "include/nvx/graphics/ui/desktop.h"
#include "include/nvx/core/init/splash.h"
int g_rtw_rf_path=0;
int rtw_apply_one(u32 m,u32 off,u8 sz,u32 v){
    if(sz==5){
        if(off==0xFFEu){ rtw_delay_us(50000); return 1; }
        if(off==0xFEu){ rtw_delay_us(105); return 1; }
        rtw_rf_write(m,g_rtw_rf_path,off,0x000FFFFFu,v); return 1;
    }
    if(off==0xFEu){ rtw_delay_us(50000); return 1; }
    if(off==0xFDu){ rtw_delay_us(5000); return 1; }
    if(off==0xFCu){ rtw_delay_us(1000); return 1; }
    if(off==0xFBu){ rtw_delay_us(55); return 1; }
    if(off==0xFAu){ rtw_delay_us(5); return 1; }
    if(off==0xF9u){ rtw_delay_us(1); return 1; }
    if(sz==1)rtw_w8(m,(u16)off,(u8)v);
    else if(sz==2)rtw_w16(m,(u16)off,(u16)v);
    else rtw_w32(m,(u16)off,v);
    return 0;
}

static int rtw_hw_ready(u32 m,u32 reg,u32 mask,u32 target){
    for(int c=0;c<1000;c++){ u32 v=rtw_r32(m,reg); if((v&mask)==(target&mask))return 1; rtw_delay_us(10); }
    return 0;
}
static int rtw_iddma_wait(u32 m){ return rtw_hw_ready(m,0x1208,(1u<<31),0); }
int rtw_iddma_block(u32 m,u32 src,u32 dst,u32 len,int first){
    if(!rtw_iddma_wait(m))return -1;
    u32 ctrl=(1u<<29)|(1u<<31)|(len & 0x3FFFFu);
    if(!first)ctrl|=(1u<<24);
    rtw_w32(m,0x1200,src);
    rtw_w32(m,0x1204,dst);
    rtw_w32(m,0x1208,ctrl);
    if(!rtw_iddma_wait(m))return -1;
    return 0;
}
static int rtw_fw_checksum_ok(u32 m,u32 dst){
    u8 fw=rtw_r8(m,0x0080);
    if(rtw_r32(m,0x1208)&(1u<<27)){
        if(dst<0x00200000u){ fw|=(1u<<3); fw&=~(1u<<4); rtw_w8(m,0x0080,fw); }
        else { fw|=(1u<<5); fw&=~(1u<<6); rtw_w8(m,0x0080,fw); }
        return 0;
    }
    if(dst<0x00200000u){ fw|=(1u<<3)|(1u<<4); rtw_w8(m,0x0080,fw); }
    else { fw|=(1u<<5)|(1u<<6); rtw_w8(m,0x0080,fw); }
    return 1;
}
static int rtw_bcnq_stage_pg(u32 m,const u8* payload,u32 plen,u32 pghead);
int rtw_bcnq_stage(u32 m,const u8* payload,u32 plen){ return rtw_bcnq_stage_pg(m,payload,plen,rtw_rsvd_boundary()); }
static int rtw_fw_last_ck=0, rtw_fw_last_stage_fail=0, rtw_fw_last_iddma_fail=0;
static int rtw_fw_to_mem(u32 m,const u8* data,u32 dst,u32 size){
    u32 v=rtw_r32(m,0x1208); v|=(1u<<25); rtw_w32(m,0x1208,v);
    u32 off=0,first=1; rtw_fw_last_stage_fail=0; rtw_fw_last_iddma_fail=0;
    while(size){
        u32 pk=(size>=0x1000)?0x1000:size;
        if(rtw_bcnq_stage_pg(m,data+off,pk,0)<0){ rtw_fw_last_stage_fail=1; }
        cpu_wmb();
        if(rtw_iddma_block(m,0x18780000u+48u,dst+off,pk,first)<0){ rtw_fw_last_iddma_fail=1; }
        first=0; off+=pk; size-=pk;
    }
    rtw_fw_last_ck=rtw_fw_checksum_ok(m,dst);
    return 0;
}
int g_dmem_ck=0,g_imem_ck=0,g_emem_ck=2,g_dmem_sf=0,g_dmem_if=0,g_imem_sf=0,g_imem_if=0;
static int g_rtw_in_fwdl=0;
static void rtw_bcnq_invalidate(void);
int rtw_mdio_write(u32 m,u8 addr,u16 data,int g1){
    rtw_w16(m,0x03F4,data);
    u8 page=(u8)((addr<32?0:1)+(g1?0:2));
    rtw_w8(m,0x03F8,(u8)(addr&0x1Fu));
    rtw_w8(m,0x03FB,page);
    rtw_w32(m,0x03F8,rtw_r32(m,0x03F8)|(1u<<5));
    for(int c=0;c<20;c++){ if(!(rtw_r32(m,0x03F8)&(1u<<5)))return 0; rtw_delay_us(10); }
    return -1;
}
static void rtw_sys_cfg(u32 m){
    rtw_w32(m,0x1080,rtw_r32(m,0x1080)|(1u<<16)|(1u<<8));
    rtw_w8(m,0x0003,(u8)(rtw_r8(m,0x0003)|(g_rtw_chip->sys_func_en?g_rtw_chip->sys_func_en:0xD8u)));
    rtw_w8(m,0x1103,(u8)((rtw_r8(m,0x1103)&0xF0u)|0x0Cu));
    u32 t=rtw_r32(m,0x0080);
    if(t&(1u<<20)){ rtw_w32(m,0x0080,t&~(1u<<20)); rtw_w32(m,0x0040,rtw_r32(m,0x0040)&~(1u<<19)); }
}
int rtw_fw_download_iddma(u32 m,const u8* fw,long fwsz){
    if(fwsz<64)return -10;
    g_rtw_coex_done=0;
    rtw_sys_cfg(m);
    rtw_bcnq_invalidate();
    rtw_w8(m,0x0303,(u8)(rtw_r8(m,0x0303)|0xF7u));
    rtw_w32(m,0x039C,0xFFFFFFFFu); g_beidx=0; g_mgidx=0;
    rtw_w32(m,0x0300,(rtw_r32(m,0x0300)&~((1u<<0)|(1u<<16)))|(1u<<20)|(1u<<15));
    u32 lte38=rtw_lte_read(m,0x38);
    g_rtw_in_fwdl=1;
    u32 dmem_addr=fr32(fw+0x20), dmem_size=fr32(fw+0x24);
    u32 imem_size=fr32(fw+0x30), emem_size=fr32(fw+0x34), emem_addr=fr32(fw+0x38), imem_addr=fr32(fw+0x3C);
    u8 mem_usage=fw[0x18];
    u32 em=(mem_usage&(1u<<4))?emem_size:0;
    dmem_size+=8; imem_size+=8; if(em)em+=8;

    rtw_w8(m,0x001D,(u8)(rtw_r8(m,0x001D)&~1u));
    rtw_w8(m,0x0003,(u8)(rtw_r8(m,0x0003)&~(1u<<2)));

    rtw_w8(m,0x1082,(u8)(rtw_r8(m,0x1082)&~( (1u<<16)>>16 )));
    rtw_w8(m,0x0009,(u8)(rtw_r8(m,0x0009)&~( (1u<<14)>>8 )));
    rtw_w8(m,0x1082,(u8)(rtw_r8(m,0x1082)|( (1u<<16)>>16 )));
    rtw_w8(m,0x0009,(u8)(rtw_r8(m,0x0009)|( (1u<<14)>>8 )));

    u16 c=(u16)(rtw_r16(m,0x0080)&0x3800); c|=1u; rtw_w16(m,0x0080,c);

    u8  bk_pq=rtw_r8(m,0x010D), bk_cr=rtw_r8(m,0x0100), bk_bcn=rtw_r8(m,0x0550);
    u16 bk_fp1=rtw_r16(m,0x0230);
    u32 bk_rq=rtw_r32(m,0x022C)|(1u<<31);
    rtw_w8(m,0x010D,(u8)(3u<<6));
    rtw_w8(m,0x0100,(u8)((1u<<0)|(1u<<2)));
    rtw_w32(m,0x1330,(1u<<31));
    rtw_w16(m,0x0230,0x200);
    rtw_w32(m,0x022C,rtw_r32(m,0x022C)|(1u<<31));
    { u8 bcn=rtw_r8(m,0x0550); rtw_w8(m,0x0550,(u8)((bcn&~(1u<<3))|(1u<<4))); }
    rtw_delay_us(50);

    const u8* cur=fw+64;
    rtw_fw_to_mem(m,cur,dmem_addr&~(1u<<31),dmem_size); g_dmem_ck=rtw_fw_last_ck; g_dmem_sf=rtw_fw_last_stage_fail; g_dmem_if=rtw_fw_last_iddma_fail;
    cur=fw+64+dmem_size;
    rtw_fw_to_mem(m,cur,imem_addr&~(1u<<31),imem_size); g_imem_ck=rtw_fw_last_ck; g_imem_sf=rtw_fw_last_stage_fail; g_imem_if=rtw_fw_last_iddma_fail;
    if(em){ cur=fw+64+dmem_size+imem_size; rtw_fw_to_mem(m,cur,emem_addr&~(1u<<31),em); g_emem_ck=rtw_fw_last_ck; } else g_emem_ck=2;

    rtw_w8(m,0x010D,bk_pq);
    rtw_w8(m,0x0100,(u8)(bk_cr & ~((1u<<1)|(1u<<3))));
    rtw_w32(m,0x1330,(1u<<31));
    rtw_w16(m,0x0230,bk_fp1);
    rtw_w32(m,0x022C,bk_rq);
    rtw_w8(m,0x0550,bk_bcn);
    g_rtw_in_fwdl=0;

    rtw_w32(m,0x0210,(1u<<2));
    u16 fc=rtw_r16(m,0x0080);
    if((fc&((1u<<4)|(1u<<6)))==((1u<<4)|(1u<<6))){ fc=(u16)((fc|(1u<<14))&~1u); rtw_w16(m,0x0080,fc); }

    rtw_w8(m,0x001D,(u8)(rtw_r8(m,0x001D)|1u));
    rtw_w8(m,0x0003,(u8)(rtw_r8(m,0x0003)|(1u<<2)));

    u32 fwready=(1u<<15)|(1u<<14)|(1u<<3)|(1u<<5)|((1u<<4)|(1u<<6));
    u32 fwmask=0xFFFFu & ~((1u<<12)|(1u<<13));
    if(lte38!=0xFFFFFFFFu)rtw_lte_write(m,0x38,lte38);
    if(rtw_hw_ready(m,0x0080,fwmask,fwready)){ extern int g_h2c_box; g_h2c_box=0; return 0; }
    rtw_w8(m,0x0080,(u8)(rtw_r8(m,0x0080)&~1u));
    rtw_w8(m,0x0003,(u8)(rtw_r8(m,0x0003)|(1u<<2)));
    return 1;
}

void rtw_desc_csum(u8* d){
    d[28]=0; d[29]=0;
    u16 ck=0; for(int i=0;i<16;i++) ck^=(u16)(d[i*2]|(d[i*2+1]<<8));
    d[28]=(u8)(ck&0xFF); d[29]=(u8)(ck>>8);
}
static int rtw_bcnq_prep_done=0;
static void rtw_bcnq_invalidate(void){ rtw_bcnq_prep_done=0; }
static u8* rtw_bcn_bd=0;

int rtw_mac_trx_init(u32 m){
    const rtw_chip* ci=g_rtw_chip; const rtw_rqpn* q=&ci->rqpn;
    u16 pqmap=(u16)(((u32)q->vo<<4)|((u32)q->vi<<6)|((u32)q->be<<8)|((u32)q->bk<<10)|((u32)q->mg<<12)|((u32)q->hi<<14));
    rtw_w16(m,0x010C,pqmap);
    rtw_w8(m,0x0100,0);
    rtw_w8(m,0x0100,0xFF);
    rtw_w32(m,0x1330,(1u<<31));

    u32 rsvd_boundary=rtw_rsvd_boundary(), h2cq_addr_pg=(ci->txff_size>>7)-ci->csi_pages-4u-8u;
    u16 hq=ci->page_table.hq,nq=ci->page_table.nq,lq=ci->page_table.lq,exq=ci->page_table.exq,gap=ci->page_table.gapq;
    u16 acq=(u16)rsvd_boundary;
    u16 pubq=acq-hq-lq-nq-exq-gap;
    rtw_w16(m,0x0230,hq);
    rtw_w16(m,0x0234,lq);
    rtw_w16(m,0x0238,nq);
    rtw_w16(m,0x023C,exq);
    rtw_w16(m,0x0240,pubq);
    rtw_w32(m,0x022C,rtw_r32(m,0x022C)|(1u<<31));
    rtw_w16(m,0x0204,(u16)rsvd_boundary);
    rtw_w8(m,0x0422,(u8)(rtw_r8(m,0x0422)|((1u<<20)>>16)));
    rtw_w16(m,0x0424,(u16)rsvd_boundary);
    rtw_w16(m,0x0206,(u16)rsvd_boundary);
    rtw_w16(m,0x0456,(u16)rsvd_boundary);
    rtw_w32(m,0x011C,ci->rxff_size-256u-1u);
    rtw_w8(m,0x060F,4);
    rtw_w8(m,0x0208,(u8)(rtw_r8(m,0x0208)|1u));
    int llt=0; for(int t=0;t<1000;t++){ if(!(rtw_r8(m,0x0208)&1u)){llt=1;break;} rtw_delay_us(10); }
    if(!llt)return -1;
    rtw_w8(m,0x0103,0);

    u32 h2cq_addr=h2cq_addr_pg<<7;
    u32 h2cq_size=8u<<7;
    rtw_w32(m,0x0244,(rtw_r32(m,0x0244)&0xFFFC0000u)|h2cq_addr);
    rtw_w32(m,0x024C,(rtw_r32(m,0x024C)&0xFFFC0000u)|h2cq_addr);
    rtw_w32(m,0x0248,(rtw_r32(m,0x0248)&0xFFFC0000u)|(h2cq_addr+h2cq_size));
    rtw_w8(m,0x0254,(u8)((rtw_r8(m,0x0254)&0xFC)|0x01));
    rtw_w8(m,0x0254,(u8)((rtw_r8(m,0x0254)&0xFB)|0x04));
    rtw_w8(m,0x020D,(u8)((rtw_r8(m,0x020D)&0x7F)|0x80));
    rtw_w8(m,0x0115,(u8)((rtw_r8(m,0x0115)&0xF0u)|0x0Fu));
    rtw_w8(m,0x07D5,(u8)(rtw_r8(m,0x07D5)&~0x03u));
    return 0;
}
int rtw_mac_trx_ok=0;
static void rtw_bcnq_prep(u32 m,u8* bd){
    if(!rtw_mac_trx_ok&&!g_rtw_in_fwdl){ if(rtw_mac_trx_init(m)==0)rtw_mac_trx_ok=1; }
    u8 pc=rtw_r8(m,0x0303); rtw_w8(m,0x0303,(u8)(pc|0xF7));
    rtw_w32(m,0x030C,0); rtw_w32(m,0x0314,0); rtw_w32(m,0x032C,0); rtw_w32(m,0x033C,0);
    rtw_w32(m,0x0308,(u32)(u64)bd);
    rtw_delay_us(50);
}
static int rtw_bcnq_stage_pg(u32 m,const u8* payload,u32 plen,u32 pghead){
    static u8* pkt=0;
    if(!pkt){ pkt=(u8*)dma_alloc_pages(2); rtw_bcn_bd=(u8*)dma_alloc_pages(1); }
    if(!pkt||!rtw_bcn_bd)return -1;
    if(plen>4096)plen=4096;
    if(!rtw_bcnq_prep_done){ rtw_bcnq_prep(m,rtw_bcn_bd); rtw_bcnq_prep_done=1; }
    u32 total=48+plen;
    for(u32 i=0;i<total;i++)pkt[i]=0;
    u32 w0=(plen&0xFFFFu)|((48u&0xFFu)<<16)|(1u<<26)|(1u<<31);
    *(volatile u32*)(pkt+0)=w0;
    u32 w1=((u32)16<<8);
    *(volatile u32*)(pkt+4)=w1;
    for(u32 i=0;i<plen;i++)pkt[48+i]=payload[i];
    rtw_desc_csum(pkt);
    u16 bk204=rtw_r16(m,0x0204); u8 bk101=rtw_r8(m,0x0101), bk550=rtw_r8(m,0x0550), bk422=rtw_r8(m,0x0422);
    rtw_w16(m,0x0204,(u16)((pghead&0x0FFF)|(1u<<15)));
    u8 cr1=rtw_r8(m,0x0101); rtw_w8(m,0x0101,(u8)(cr1|(1u<<0)));
    u8 bc=rtw_r8(m,0x0550); rtw_w8(m,0x0550,(u8)((bc&~(1u<<3))|(1u<<4)));
    u8 txq=rtw_r8(m,0x0422); rtw_w8(m,0x0422,(u8)(txq&~((1u<<22)>>16)));
    u32 psb=((total-1)/128)+1; psb|=(1u<<15);
    *(volatile u16*)(rtw_bcn_bd+0)=(u16)48;
    *(volatile u16*)(rtw_bcn_bd+2)=(u16)psb;
    *(volatile u32*)(rtw_bcn_bd+4)=(u32)(u64)pkt;
    *(volatile u16*)(rtw_bcn_bd+8)=(u16)plen;
    *(volatile u16*)(rtw_bcn_bd+10)=0;
    *(volatile u32*)(rtw_bcn_bd+12)=(u32)(u64)(pkt+48);
    cpu_wmb();
    rtw_w32(m,0x0308,(u32)(u64)rtw_bcn_bd);
    u8 bw=rtw_r8(m,0x0383); rtw_w8(m,0x0383,(u8)(bw|(1u<<4)));
    int ok=0; for(int t=0;t<1000;t++){ if(rtw_r16(m,0x0204)&(1u<<15)){ok=1;break;} rtw_delay_us(10); }
    for(int t=0;t<1000;t++){ if(!(*(volatile u16*)(rtw_bcn_bd+2) & (1u<<15)))break; rtw_delay_us(10); }
    rtw_delay_us(50);
    rtw_w16(m,0x0204,(u16)((bk204&0x0FFFu)|(1u<<15))); rtw_w8(m,0x0550,bk550); rtw_w8(m,0x0422,bk422); rtw_w8(m,0x0101,bk101);
    return ok?0:-2;
}
u8* g_rxring=0; u8* g_rxbuf=0; u32 g_rxnum=0; u32 g_rxidx=0;
u32 rtw_rf_read_a(u32 m,u32 addr){ return rtw_rf_read(m,0,addr,0x000FFFFFu); }
int g_chquiet=0;
static int rtw_rfe(void){ return rtw_efuse_valid?(g_rtw_efuse.rfe_option&0x1f):0; }
static int rtw_lte_wait(u32 m){ for(int i=0;i<1000;i++){ if(rtw_r32(m,0x1700)&(1u<<29))return 1; rtw_delay_us(10); } return 0; }
u32 rtw_lte_read(u32 m,u16 off){ if(!rtw_lte_wait(m))return 0xFFFFFFFFu; rtw_w32(m,0x1700,0x800F0000u|off); return rtw_r32(m,0x1708); }
int rtw_lte_write(u32 m,u16 off,u32 v){ if(!rtw_lte_wait(m))return 0; rtw_w32(m,0x1704,v); rtw_w32(m,0x1700,0xC00F0000u|off); return 1; }
int rtw_lte_mask(u32 m,u16 off,u32 mask,u32 v){ u32 sh=0; while(sh<31&&!((mask>>sh)&1u))sh++; u32 t=rtw_lte_read(m,off); if(t==0xFFFFFFFFu&&!rtw_lte_wait(m))return 0; t=(t&~mask)|((v<<sh)&mask); return rtw_lte_write(m,off,t); }
int g_rtw_coex_done=0;
int rtw_coex_wifi_only(u32 m){ g_rtw_coex_ok=1; if(g_rtw_chip->coex_wifi_only)g_rtw_chip->coex_wifi_only(m); g_rtw_coex_done=1; return g_rtw_coex_ok; }
int g_rtw_coex_bt=0;
static u32 cxs_lte38, cxs_6c0, cxs_6c4, cxs_cb4;
int rtw_coex_bt_share(u32 m,int on,int connected){
    if(!m||!g_rtw_coex_done)return 0;
    if(!on){ if(!g_rtw_coex_bt)return 1; rtw_w32(m,0x06C0,cxs_6c0); rtw_w32(m,0x06C4,cxs_6c4); rtw_w32(m,0x0CB4,cxs_cb4);
        for(int t=0;t<5;t++){ if(rtw_lte_write(m,0x38,cxs_lte38)&&(rtw_lte_read(m,0x38)&0xFF00u)==(cxs_lte38&0xFF00u)){ g_rtw_coex_bt=0; return 1; } rtw_delay_us(1000); }
        rtw_coex_wifi_only(m); g_rtw_coex_bt=0; return 0; }
    if(g_rtw_coex_bt)return 1;
    cxs_lte38=rtw_lte_read(m,0x38); cxs_6c0=rtw_r32(m,0x06C0); cxs_6c4=rtw_r32(m,0x06C4); cxs_cb4=rtw_r32(m,0x0CB4);
    if(cxs_lte38==0xFFFFFFFFu)return 0;
    int ok=rtw_lte_write(m,0x38,cxs_lte38&~0xFF00u);
    if(connected){ rtw_w32(m,0x06C0,0x66555555u); rtw_w32(m,0x06C4,0x5A5A5A5Au); }
    else { rtw_w32(m,0x06C0,0x55555555u); rtw_w32(m,0x06C4,0x55555555u); }
    int rfe=rtw_rfe(); const char* nm=g_rtw_chip?g_rtw_chip->name:0;
    int is21=nm&&nm[0]=='R'&&nm[3]=='8'&&nm[4]=='8'&&nm[5]=='2'&&nm[6]=='1';
    int btg=(rfe==2||rfe==10||rfe==7||rfe==15||rfe==4||rfe==12), nosw=(rfe==5||rfe==6||rfe==13||rfe==14);
    if(is21&&!btg&&!nosw){ int pol=(rfe==3||rfe==11); rtw_w32(m,0x0CB4,(cxs_cb4&~0xF00000FFu)|0x66u|((pol?1u:2u)<<28)); }
    g_rtw_coex_bt=1; return ok;
}
int rtw_rfe_type(void){ return rtw_rfe(); }
int g_wifi_txpwr=1, g_wifi_band5=0; u8 g_wifi_curch=0; u8 g_wifi_pwr[20];
void rtw_set_txpwr(u32 m,u8 ch){ if(g_rtw_chip->set_tx_power)g_rtw_chip->set_tx_power(m,ch); }
void rtw_set_channel_2g(u32 m,u8 ch){ g_rtw_chip->set_channel(m,ch,RTW_BW_20); g_wifi_band5=ch>14; g_wifi_curch=ch; }
static int rtw_rx_enable_safe(u32 m){
    rtw_w32(m,0x0608,0xE400220Eu);
    return 1;
}
static u32 rtw_rx_count(u32 m){ return rtw_r32(m,0x02B0); }
int rtw_rx_enable(u32 m){
    rtw_w32(m,0x0608,0xE400220Eu);
    rtw_w32(m,0x0608,rtw_r32(m,0x0608)|(1u<<28));
    u32 cr=rtw_r32(m,0x0100); cr|=(1u<<3)|(1u<<1); rtw_w32(m,0x0100,cr);
    return 1;
}
int rtw_rx_ring_up(u32 m){
    if(!g_rxring||!g_rxbuf||!g_rxnum)return 0;
    for(u32 i=0;i<g_rxnum;i++){ u8* d=g_rxring+i*8; u64 pb=(u64)(g_rxbuf+i*RTW_RXBUF);
        *(volatile u16*)(d)=(u16)RTW_RXBUF; *(volatile u16*)(d+2)=0; *(volatile u32*)(d+4)=(u32)pb; }
    cpu_wmb();
    rtw_w16(m,0x0382,(u16)(g_rxnum & 0x0FFF));
    rtw_w32(m,0x033C,0);
    rtw_w32(m,0x0338,(u32)(u64)g_rxring);
    rtw_w32(m,0x039C,0xFFFFFFFFu);
    rtw_w16(m,0x03B4,0);
    rtw_w32(m,0x0300,(rtw_r32(m,0x0300)&~((1u<<0)|(1u<<16)))|(1u<<20)|(1u<<15));
    g_rxidx=0; g_beidx=0; g_mgidx=0;
    u32 cr=rtw_r32(m,0x0100); cr|=0xFFu; rtw_w32(m,0x0100,cr);
    rtw_w16(m,0x0522,0);
    rtw_delay_us(2000);
    return 1;
}
int rtw_rx_hw_wp(u32 m){ u32 t=rtw_r32(m,0x03B4); return (int)((t>>16)&0x0FFF); }

scan_ap g_aps[24]; int g_apn=0;
extern volatile u64 g_ticks;
u8 g_scan_grp=0;
static void scan_add(const u8* bssid,const char* ssid,u8 ch,u8 sec,int rssi,u8 ht,u8 akm,u8 pmf,u8 wmm,int offch){
    int hid=!ssid[0]; if(rssi<-127)rssi=-127;
    for(int i=0;i<g_apn;i++){ int eq=1; for(int b=0;b<6;b++)if(g_aps[i].bssid[b]!=bssid[b])eq=0;
        if(eq){ g_aps[i].seen++; if(ch&&!offch)g_aps[i].ch=ch; else if(ch&&!g_aps[i].ch)g_aps[i].ch=ch; g_aps[i].sec=sec; if(rssi>-127&&(!offch||g_aps[i].rssi<=-127))g_aps[i].rssi=(signed char)(g_aps[i].rssi<=-127?rssi:(g_aps[i].rssi*3+rssi)/4); g_aps[i].ht=ht; g_aps[i].akm=akm; g_aps[i].pmf=pmf; g_aps[i].wmm=wmm; g_aps[i].grp=g_scan_grp; g_aps[i].last=g_ticks;
            if(!hid&&g_aps[i].hidden){ int j=0; while(ssid[j]&&j<32){g_aps[i].ssid[j]=ssid[j];j++;} g_aps[i].ssid[j]=0; g_aps[i].hidden=0; } return; } }
    if(g_apn>=24){ int w=-1, wr=0; for(int i=0;i<g_apn;i++) if(g_aps[i].rssi<wr||w<0){ wr=g_aps[i].rssi; w=i; } if(w<0||rssi<=wr)return; g_aps[w]=g_aps[--g_apn]; }
    scan_ap* a=&g_aps[g_apn]; for(int b=0;b<6;b++)a->bssid[b]=bssid[b];
    int j=0; while(ssid[j]&&j<32){a->ssid[j]=ssid[j];j++;} a->ssid[j]=0;
    a->grp=g_scan_grp; a->ch=ch; a->seen=1; a->sec=sec; a->rssi=(signed char)rssi; a->ht=ht; a->akm=akm; a->pmf=pmf; a->wmm=wmm; a->hidden=(u8)hid; a->last=g_ticks; g_apn++;
}
int rtw_rx_rssi(const u8* base){ u32 w0=*(const volatile u32*)base; u32 drvsz=((w0>>16)&0xFu)*8u, shift=(w0>>24)&3u; if(!(w0&(1u<<26))||drvsz<8)return -127;
    const u8* ps=base+24+shift; u32 d0=(u32)ps[0]|((u32)ps[1]<<8)|((u32)ps[2]<<16)|((u32)ps[3]<<24); u32 pg=d0&0xF; if(pg>1)return -127; int v;
    if(pg==0){ if(drvsz<16)return -127; u32 d3=(u32)ps[12]|((u32)ps[13]<<8)|((u32)ps[14]<<16)|((u32)ps[15]<<24); int vga=(int)((d3>>8)&0x1F), lna=(int)(((d3>>23)&1)<<3|((d3>>13)&7));
        static const signed char T0[8]={22,8,-6,-22,-31,-40,-46,-52}; static const signed char T1[16]={10,6,2,-2,-6,-10,-14,-17,-20,-24,-28,-31,-34,-37,-40,-44};
        if(rtw_rfe()==0){ if(lna>=8)return -127; v=T0[lna]-2*vga; } else v=T1[lna&15]-2*vga; }
    else v=(int)((d0>>8)&0xFF)-110; if(v<-100)v=-100; if(v>-10)v=-10; return v; }
static u8 g_rsn_grp;
static void rtw_rsn_parse(const u8* b,int l,u8* akm,u8* pmf){
    int o=2; if(l<o+4)return; g_rsn_grp=(b[2]==0x00&&b[3]==0x0F&&b[4]==0xAC)?b[5]:0xFF; o+=4; if(l<o+2)return; int pc=b[o]|(b[o+1]<<8); o+=2+4*pc; if(l<o+2)return; int ac=b[o]|(b[o+1]<<8); o+=2;
    for(int k=0;k<ac&&o+4<=l;k++,o+=4) if(b[o]==0x00&&b[o+1]==0x0F&&b[o+2]==0xAC){ u8 t=b[o+3]; if(t==2)*akm|=AKM_PSK; else if(t==8)*akm|=AKM_SAE; else if(t==1)*akm|=AKM_8021X; else if(t==6)*akm|=AKM_PSK256; }
    if(o+2<=l){ u16 cap=(u16)(b[o]|(b[o+1]<<8)); if(cap&(1u<<7))*pmf|=1; if(cap&(1u<<6))*pmf|=2; } }
u8 g_scan_ch;
int rtw_rx_bad(const u8* base){
    u32 w0=*(const volatile u32*)base, w2=*(const volatile u32*)(base+8);
    u32 plen=w0&0x3FFFu, dsz=(w0>>16)&0xFu, shift=(w0>>24)&3u;
    if(w2&(1u<<28))return 1;
    if(dsz!=0&&dsz!=4)return 1;
    if(24u+dsz*8u+shift+plen>RTW_RXBUF)return 1;
    return 0;
}
static int rtw_frame_kind(const u8* base){
    if(rtw_rx_bad(base))return -1;
    u32 w0=*(const volatile u32*)base; u32 plen=w0&0x3FFFu, crc=(w0>>14)&1u, drvsz=((w0>>16)&0xFu)*8u, shift=(w0>>24)&3u; u32 hoff=24u+drvsz+shift;
    if(crc||plen<24||hoff+24u>RTW_RXBUF)return -1; u8 fc=base[hoff]&0xFC; if(fc==0x80||fc==0x50)return 1;
    extern const u8* wnd_bssid(void); const u8* bs=wnd_bssid(); const u8* a2=base+hoff+10; const u8* a3=base+hoff+16; int m2=1, m3=1; for(int k=0;k<6;k++){ if(a2[k]!=bs[k])m2=0; if(a3[k]!=bs[k])m3=0; } return (m2||m3)?0:-1; }
int wifi_scan_parse(const u8* fr,int flen,u8 ch,int rssi,int onch){
    if(flen<36)return 0;
    u8 fc=fr[0];
    if((fc&0xFC)!=0x80&&(fc&0xFC)!=0x50)return 0;
    u32 frame_end=(u32)flen;
    u8 bssid[6]; for(int b=0;b<6;b++)bssid[b]=fr[16+b];
    u8 sec=0, ht=0, akm=0, pmf=0, wmm=0, dsch=0, htch=0, rsn=0, wpa1=0; g_rsn_grp=0;
    u32 capoff=24u+10u;
    if(capoff+2u<=frame_end){ u16 cap=fr[capoff]|(fr[capoff+1]<<8); if(cap&(1u<<4))sec=1; }
    u32 iep=24u+12u; char ss[33]; ss[0]=0; int gotssid=0;
    while(iep+2u<=frame_end){
        u8 tag=fr[iep], tl=fr[iep+1];
        if((u32)(iep+2u+tl)>frame_end)break;
        if(tag==0&&!gotssid){ int n=tl<32?tl:32, z=1; for(int c=0;c<n;c++){ u8 c2=fr[iep+2u+c]; if(c2)z=0; ss[c]=(char)c2; } ss[n]=0; if(!tl||z)ss[0]=0; gotssid=1; }
        else if(tag==48){ sec=2; rsn=1; rtw_rsn_parse(fr+iep+2,tl,&akm,&pmf); }
        else if(tag==3&&tl>=1){ dsch=fr[iep+2]; }
        else if(tag==61&&tl>=1){ htch=fr[iep+2]; }
        else if(tag==45&&tl>=26){ ht=(u8)(1|((fr[iep+2]&0x20)?2:0)); }
        else if(tag==221&&tl>=5&&fr[iep+2]==0x00&&fr[iep+3]==0x50&&fr[iep+4]==0xF2&&fr[iep+5]==0x02){ wmm=1; }
        else if(tag==221&&tl>=4&&fr[iep+2]==0x00&&fr[iep+3]==0x50&&fr[iep+4]==0xF2&&fr[iep+5]==0x01){ wpa1=1; if(sec<2)sec=2; }
        iep+=2u+tl;
    }
    if(wpa1&&!rsn)g_rsn_grp=2;
    if(sec==2&&!akm)akm=AKM_PSK;
    u8 rch=dsch?dsch:htch?htch:ch; if(!rch)return 0;
    if(onch)ch=rch;
    g_scan_grp=g_rsn_grp;
    int off=!ch||rch!=ch;
    scan_add(bssid,ss,rch,sec,(off&&rssi>-100)?rssi-12:rssi,ht,akm,pmf,wmm,off);
    return 1; }
int rtw_scan_frame(const u8* base,u8 ch){
    if(rtw_rx_bad(base))return 0;
    u32 w0=*(const volatile u32*)base;
    u32 plen=w0&0x3FFFu; u32 crc=(w0>>14)&1u; u32 drvsz=((w0>>16)&0xFu)*8u; u32 shift=(w0>>24)&3u;
    u32 hoff=24u+drvsz+shift;
    u32 frame_end=hoff+plen-(plen>=4u?4u:0u); if(frame_end>RTW_RXBUF)frame_end=RTW_RXBUF;
    if(plen<40||crc||hoff+36u>frame_end)return 0;
    u8 fc=base[hoff]; if((fc&0xFC)!=0x80&&(fc&0xFC)!=0x50)return 0;
    return wifi_scan_parse(base+hoff,(int)(frame_end-hoff),ch,rtw_rx_rssi(base),0); }
u32 g_rtw_rx_seen=0, g_rtw_rx_crc=0, g_rtw_rx_bcn=0;
int rtw_scan_drain(u32 m,u8 ch){
    extern int g_wnd_up; int found=0;
    for(int guard=0;guard<(int)g_rxnum&&found<64;guard++){
        int wp=rtw_rx_hw_wp(m);
        if((int)(g_rxidx%g_rxnum)==(wp%(int)g_rxnum))break;
        u32 i=g_rxidx%g_rxnum; u8* base=g_rxbuf+i*RTW_RXBUF;
        if(!*(volatile u32*)(u64)base)break;
        if(g_wnd_up&&rtw_frame_kind(base)==0)break;
        { u32 w0=*(volatile u32*)(u64)base; g_rtw_rx_seen++; if((w0>>14)&1u)g_rtw_rx_crc++; }
        if(rtw_scan_frame(base,ch)){ found++; g_rtw_rx_bcn++; }
        *(volatile u32*)(u64)base=0;
        *(volatile u16*)(u64)(g_rxring+i*8)=(u16)RTW_RXBUF; *(volatile u16*)(u64)(g_rxring+i*8+2)=0;
        *(volatile u32*)(u64)(g_rxring+i*8+4)=(u32)(u64)base;
        g_rxidx++; if((int)g_rxidx>=(int)g_rxnum)g_rxidx=0;
        rtw_w16(m,0x03B4,(u16)g_rxidx);
    }
    return found;
}
int rtw_rx_poll_beacon(u32 m,char* ssid_out,int* rssi_out,u8* bssid_out){
    (void)m;
    if(!g_rxring||!g_rxbuf||!g_rxnum)return -1;
    for(int spin=0;spin<60000;spin++){
        u32 i=g_rxidx % g_rxnum;
        u8* base=g_rxbuf+i*RTW_RXBUF;
        u32 w0=*(volatile u32*)(u64)base;
        u32 plen=w0 & 0x3FFFu;
        if(plen>=36 && plen<2000 && !(w0&(1u<<14))){
            u32 drvsz=((w0>>16)&0xF)*8;
            u32 hdroff=24+drvsz;
            if(hdroff+36<=plen && hdroff+36<2000){
                const u8* fr=base+hdroff;
                u8 fc=fr[0];
                if((fc&0xFC)==0x80){
                    for(int b=0;b<6;b++)bssid_out[b]=fr[16+b];
                    u32 iepos=hdroff+24+12;
                    while(iepos+2<=plen && iepos+2<2000){
                        u8 tag=base[iepos], tl=base[iepos+1];
                        if(iepos+2+tl>plen)break;
                        if(tag==0){ int n=tl<32?tl:32; for(int c=0;c<n;c++){ u8 ch=base[iepos+2+c]; ssid_out[c]=(ch>=32&&ch<127)?(char)ch:'.'; } ssid_out[n]=0; *rssi_out=0;
                            *(volatile u32*)(u64)base=0; g_rxidx++; if(g_rxidx>=g_rxnum)g_rxidx=0; return 1; }
                        iepos+=2+tl;
                    }
                }
                *(volatile u32*)(u64)base=0; g_rxidx++; if(g_rxidx>=g_rxnum)g_rxidx=0;
            } else { *(volatile u32*)(u64)base=0; g_rxidx++; if(g_rxidx>=g_rxnum)g_rxidx=0; }
        }
        rtw_delay_us(50);
    }
    return 0;
}
u8* g_bering=0; u32 g_benum=0; u32 g_beidx=0;
u8* g_mgring=0; u32 g_mgnum=0; u32 g_mgidx=0;
int g_probe_txok=0; u32 g_probe_hwb=0, g_probe_hwa=0; int g_probe_ring=0; u32 g_probe_other=0;
/* v11.10: g_probe_uni sends a directed probe to g_probe_da (with SSID g_probe_ssid) instead of a broadcast one */
int g_probe_uni=0; u8 g_probe_da[6]; char g_probe_ssid[33];   /* g_probe_ring: 0 = data (BE) queue, 1 = management queue */
int rtw_tx_probe(u32 m,int* got_resp,char* resp_ssid){
    int rg=g_probe_ring?1:0; u8* ring=rg?g_mgring:g_bering; u32 num=rg?g_mgnum:g_benum; u32* idxp=rg?&g_mgidx:&g_beidx; u32 reg=rg?0x03B0u:0x03A8u;
    *got_resp=0; resp_ssid[0]=0; g_probe_txok=0; g_probe_other=0;
    if(!ring||!num||!g_rxnum||!g_rxbuf)return -1;
    if(!rtw_tx_room(m,rg))return -1;
    u8* pkt=rtw_tx_buf(rg);
    if(!pkt)return -1;
    u8 mac[6]; for(int i=0;i<6;i++)mac[i]=rtw_r8(m,0x0610+i);
    int zero=1; for(int i=0;i<6;i++) if(mac[i])zero=0;
    if(zero && rtw_efuse_valid){ for(int i=0;i<6;i++)mac[i]=g_rtw_efuse.mac[i]; for(int i=0;i<6;i++)rtw_w8(m,0x0610+i,mac[i]); }
    u8 frame[128]; int fl=0;
    frame[fl++]=0x40; frame[fl++]=0x00;
    frame[fl++]=0; frame[fl++]=0;
    int uni=g_probe_uni;
    for(int i=0;i<6;i++)frame[fl++]=uni?g_probe_da[i]:0xFF;
    for(int i=0;i<6;i++)frame[fl++]=mac[i];
    for(int i=0;i<6;i++)frame[fl++]=uni?g_probe_da[i]:0xFF;
    frame[fl++]=0; frame[fl++]=0;
    { int sl=0; if(uni)while(sl<32&&g_probe_ssid[sl])sl++; frame[fl++]=0; frame[fl++]=(u8)sl; for(int i=0;i<sl;i++)frame[fl++]=(u8)g_probe_ssid[i]; }
    frame[fl++]=1; frame[fl++]=8;
    frame[fl++]=0x82;frame[fl++]=0x84;frame[fl++]=0x8B;frame[fl++]=0x96;
    frame[fl++]=0x0C;frame[fl++]=0x12;frame[fl++]=0x18;frame[fl++]=0x24;
    u32 total=48+fl;
    for(u32 i=0;i<total;i++)pkt[i]=0;
    u32 w0=((u32)fl & 0xFFFFu) | ((48u&0xFFu)<<16) | (uni?0u:(1u<<24)) | (1u<<26) | (rg?(1u<<31):0u);
    *(volatile u32*)(pkt+0)=w0;
    u32 w1=((u32)(rg?18u:0u)<<8) | ((u32)8u<<16);
    *(volatile u32*)(pkt+4)=w1;
    *(volatile u32*)(pkt+12)=(1u<<8)|(1u<<10);
    *(volatile u32*)(pkt+16)=0x00u;
    rtw_txdesc_rate(pkt,1);
    *(volatile u32*)(pkt+32)=(1u<<15);
    rtw_desc_csum(pkt);
    for(int i=0;i<fl;i++)pkt[48+i]=frame[i];
    u32 i=*idxp % num;
    u8* bd=ring+i*16;
    u32 psb=((total-1)/128)+1;
    *(volatile u16*)(bd+0)=48; *(volatile u16*)(bd+2)=(u16)psb; *(volatile u32*)(bd+4)=(u32)(u64)pkt;
    *(volatile u16*)(bd+8)=(u16)fl; *(volatile u16*)(bd+10)=0; *(volatile u32*)(bd+12)=(u32)(u64)(pkt+48);
    cpu_wmb();
    u32 hw_before=(rtw_r32(m,reg)>>16)&0x0FFF;
    *idxp=(*idxp+1)%num;
    rtw_w16(m,reg,(u16)*idxp);
    u32 hw_after=hw_before; int txok=0;
    for(int t=0;t<20000;t++){ hw_after=(rtw_r32(m,reg)>>16)&0x0FFF; if(hw_after!=hw_before){txok=1;break;} rtw_delay_us(50); }
    g_probe_txok=txok; g_probe_hwb=hw_before; g_probe_hwa=hw_after;
    *got_resp=0; resp_ssid[0]=0;
    for(int t=0;t<40000 && !*got_resp;t++){
        int wp=rtw_rx_hw_wp(m);
        while((int)(g_rxidx%g_rxnum)!=(wp%(int)g_rxnum) && !*got_resp){
            u32 ri=g_rxidx % g_rxnum; u8* base=g_rxbuf+ri*RTW_RXBUF; if(!*(volatile u32*)(u64)base)break;
            u32 rw0=*(volatile u32*)(u64)base; u32 plen=rw0&0x3FFFu;
            u32 drvsz=((rw0>>16)&0xFu)*8u; u32 shift=(rw0>>24)&3u; u32 crc=((rw0>>14)&1u)|(u32)rtw_rx_bad(base);
            u32 hoff=24u+drvsz+shift; u32 fe=hoff+plen-(plen>=4u?4u:0u); if(fe>RTW_RXBUF)fe=RTW_RXBUF;
            if(plen>=40 && !crc && hoff+36u<=fe){
                const u8* fr=base+hoff; u8 fc=fr[0];
                int tous=1; for(int k=0;k<6;k++) if(fr[4+k]!=mac[k])tous=0;
                if((fc&0xFC)==0x50&&!tous)g_probe_other++;
                if((fc&0xFC)==0x50&&tous){
                    u32 iep=hoff+24u+12u;
                    while(iep+2u<=fe){ u8 tag=base[iep],tl=base[iep+1]; if((u32)(iep+2u+tl)>fe)break;
                        if(tag==0){ int n=tl<32?tl:32; for(int c=0;c<n && iep+2u+(u32)c<RTW_RXBUF;c++){u8 c2=base[iep+2u+c]; resp_ssid[c]=(c2>=32&&c2<127)?(char)c2:'.';} resp_ssid[n]=0; break; }
                        iep+=2u+tl; }
                    *got_resp=1;
                }
            }
            *(volatile u32*)(u64)base=0;
            *(volatile u16*)(u64)(g_rxring+ri*8)=(u16)RTW_RXBUF; *(volatile u16*)(u64)(g_rxring+ri*8+2)=0; *(volatile u32*)(u64)(g_rxring+ri*8+4)=(u32)(u64)base;
            g_rxidx++; if((int)g_rxidx>=(int)g_rxnum)g_rxidx=0;
            rtw_w16(m,0x03B4,(u16)g_rxidx);
        }
        rtw_delay_us(100); if((t&7)==0)ui_pump();
    }
    return 0;
}
u8* rtw_tx_buf(int ring){
    static u8* pool[2][16]; static u32 n[2];
    ring=ring?1:0; u32 k=(n[ring]++)&15u;
    if(!pool[ring][k])pool[ring][k]=(u8*)dma_alloc_pages(1);
    return pool[ring][k];
}
int rtw_tx_room(u32 m,int ring){
    u32 reg=ring?0x03B0u:0x03A8u, num=ring?g_mgnum:g_benum, idx=ring?g_mgidx:g_beidx;
    if(!num)return 0;
    u32 nx=(idx+1u)%num;
    for(int w=0;w<200;w++){ u32 raw=rtw_r32(m,reg); if(raw==0xFFFFFFFFu)return 0; u32 rp=((raw>>16)&0x0FFFu)%num; u32 used=(idx+num-rp)%num; if(nx!=rp&&used<15u)return 1; rtw_delay_us(50); }
    return 0;
}
/* v11.10: join log - one line per join step, shown by 'wifi diag'. */
char g_jlog[RTW_JLOG_N][RTW_JLOG_W]; u32 g_jlog_n=0;
void rtw_jlog_reset(void){ g_jlog_n=0; for(int i=0;i<RTW_JLOG_N;i++)g_jlog[i][0]=0; }
void rtw_jlog(const char* a,const char* b,int v){
    char* d=g_jlog[g_jlog_n%RTW_JLOG_N]; int n=0, cap=RTW_JLOG_W-1;
    for(const char* s=a;s&&*s&&n<cap;s++)d[n++]=*s;
    for(const char* s=b;s&&*s&&n<cap;s++)d[n++]=*s;
    if(v>=0&&n<cap-11){ char t[12]; int k=0; u32 x=(u32)v; do{ t[k++]=(char)('0'+x%10u); x/=10u; }while(x&&k<11); d[n++]=' '; while(k&&n<cap)d[n++]=t[--k]; }
    d[n]=0; g_jlog_n++; }
/* v11.10: management frames. The data (BE) queue is proven on this card (probe -> answer).
   The management queue (QSEL 18 -> EXTRA pages) matches rtw88 but has never been seen working here,
   so every send is checked: if the card's read pointer does not reach our write pointer within
   ~20 ms the frame is treated as stuck and resent through the data queue, and the driver stays on the
   data queue from then on (g_mg_via_be). The join code can also force the data queue on its late retries. */
/* v11.10: on the owner's card a probe on the management queue leaves the queue but is never answered, while the
   same probe on the data queue is (wifi probe 6 / wifi probe 6 mg). So management frames use the data queue by
   default; 'wifi mgq on' switches back to the checked management queue for testing. */
int g_mg_via_be=1, g_mg_last=0, g_mg_try_mgq=0; u32 g_mg_sent=0, g_mg_left=0, g_mg_stuck=0, g_mg_be=0, g_mg_noroom=0;
static int rtw_tx_mgmt_q(u32 m,const u8* frame,int fl,int ring){
    u8* rb=ring?g_mgring:g_bering; u32 num=ring?g_mgnum:g_benum; u32 reg=ring?0x03B0u:0x03A8u;
    if(!rb||!num)return -1;
    if(fl<10||fl>1900)return -1;
    if(!rtw_tx_room(m,ring))return -2;
    u8* pkt=rtw_tx_buf(ring); if(!pkt)return -1;
    int bmc=(frame[4]&1u)?1:0;
    u32 total=48+(u32)fl; for(u32 i=0;i<total;i++)pkt[i]=0;
    u32 w0=((u32)fl&0xFFFFu)|((48u&0xFFu)<<16)|(bmc?(1u<<24):0u)|(1u<<26)|(ring?(1u<<31):0u);
    *(volatile u32*)(pkt+0)=w0;
    *(volatile u32*)(pkt+4)=((u32)(ring?18u:0u)<<8)|((u32)8u<<16);
    *(volatile u32*)(pkt+12)=(1u<<8)|(1u<<10);
    *(volatile u32*)(pkt+16)=0x00u;
    rtw_txdesc_rate(pkt,1);
    *(volatile u32*)(pkt+32)=(1u<<15);
    rtw_desc_csum(pkt);
    for(int i=0;i<fl;i++)pkt[48+i]=frame[i];
    u32* idxp=ring?&g_mgidx:&g_beidx;
    u32 i=*idxp % num; u8* bd=rb+i*16;
    u32 psb=((total-1)/128)+1;
    *(volatile u16*)(bd+0)=48; *(volatile u16*)(bd+2)=(u16)psb; *(volatile u32*)(bd+4)=(u32)(u64)pkt;
    *(volatile u16*)(bd+8)=(u16)fl; *(volatile u16*)(bd+10)=0; *(volatile u32*)(bd+12)=(u32)(u64)(pkt+48);
    cpu_wmb();
    *idxp=(*idxp+1)%num; rtw_w16(m,reg,(u16)*idxp);
    for(int t=0;t<400;t++){ u32 raw=rtw_r32(m,reg); if(raw==0xFFFFFFFFu)return -1; if(((raw>>16)&0x0FFFu)%num==*idxp)return 0; rtw_delay_us(50); }
    return 1;
}
static const char* rtw_mg_res(int r){ return r==0?"left the queue":r==1?"STUCK in the queue":r==-2?"queue full":"not sent"; }
int rtw_tx_mgmt_path(u32 m,const u8* frame,int fl,int force_be){
    g_mg_sent++;
    if(!force_be&&!g_mg_via_be&&g_mgring&&g_mgnum){
        int r=rtw_tx_mgmt_q(m,frame,fl,1);
        if(r==0){ g_mg_left++; g_mg_last=1; return 0; }
        if(r==-2)g_mg_noroom++; else g_mg_stuck++;
        g_mg_via_be=1; rtw_jlog("  mgmt queue: ",rtw_mg_res(r),-1); rtw_jlog("  switching to the data queue for management frames","",-1);
    }
    int r=rtw_tx_mgmt_q(m,frame,fl,0);
    if(r==0||r==1){ g_mg_be++; g_mg_last=r==0?2:3; return 0; }
    if(r==-2)g_mg_noroom++;
    g_mg_last=-1; return -1;
}
int rtw_tx_mgmt(u32 m,const u8* frame,int fl){ return rtw_tx_mgmt_path(m,frame,fl,0); }
/* v11.10: port 0 network type (MSR, 0x102 bits 1:0). rtw88 keeps NO_LINK (0) through auth and association and
   switches to MGD_LINKED (2) only when mac80211 reports the association (rtw_vif_assoc_changed). */
int rtw_get_nettype(u32 m){ return (int)((rtw_r32(m,0x0100)>>16)&3u); }
void rtw_set_nettype(u32 m,int t,int log){ u32 nt=rtw_r32(m,0x0100); if(nt==0xFFFFFFFFu)return; nt=(nt&~0x30000u)|(((u32)t&3u)<<16); rtw_w32(m,0x0100,nt);
    if(log)rtw_jlog("  chip mode: ",rtw_get_nettype(m)==2?"connected (linked)":rtw_get_nettype(m)==0?"not connected (no link)":"other",-1); }
/* Wait until every management frame handed to the card has been fetched (used before radio off). */
void rtw_tx_mgmt_drain(u32 m){
    int be=g_mg_via_be||g_mg_last==2||g_mg_last==3; u32 reg=be?0x03A8u:0x03B0u; u32 num=be?g_benum:g_mgnum; u32 idx=be?g_beidx:g_mgidx;
    if(!num)return;
    for(int w=0;w<40;w++){ u32 raw=rtw_r32(m,reg); if(raw==0xFFFFFFFFu)return; if(((raw>>16)&0x0FFFu)%num==idx%num)return; rtw_delay_us(500); } }
const char* rtw_mg_last_name(void){ return g_mg_last==1?"mgmt queue":g_mg_last==2?"data queue":g_mg_last==3?"data queue (slow)":g_mg_last<0?"not sent":"-"; }
u8 g_eap_stash[512]; int g_eap_stashlen=0;
int rtw_rx_wait_mgmt(u32 m,u8 want_fc,const u8* from_bssid,u8* body_out,int* body_len,int ms){
    if(!g_rxnum||!g_rxbuf)return 0;
    /* v11.10: auth/assoc/reassoc answers must be addressed to us, not to another device on the same router */
    u8 me[6]; int tome=(want_fc==0xB0||want_fc==0x10||want_fc==0x30); for(int k=0;k<6;k++)me[k]=rtw_r8(m,0x0610+k);
    for(int t=0;t<ms*10;t++){
        int wp=rtw_rx_hw_wp(m);
        while((int)(g_rxidx%g_rxnum)!=(wp%(int)g_rxnum)){
            u32 ri=g_rxidx % g_rxnum; u8* base=g_rxbuf+ri*RTW_RXBUF; if(!*(volatile u32*)(u64)base)break;
            u32 rw0=*(volatile u32*)(u64)base; u32 plen=rw0&0x3FFFu;
            u32 drvsz=((rw0>>16)&0xFu)*8u; u32 shift=(rw0>>24)&3u; u32 crc=((rw0>>14)&1u)|(u32)rtw_rx_bad(base);
            u32 hoff=24u+drvsz+shift; u32 fe=hoff+plen-(plen>=4u?4u:0u); if(fe>RTW_RXBUF)fe=RTW_RXBUF;
            int match=0; int blen=0;
            if(plen>=28 && !crc && hoff+24u<=fe){
                const u8* fr=base+hoff; u8 fc=fr[0];
                if((fc&0x0C)==0x08 && g_eap_stashlen==0 && (!from_bssid||(fr[10]==from_bssid[0]&&fr[11]==from_bssid[1]&&fr[12]==from_bssid[2]&&fr[13]==from_bssid[3]&&fr[14]==from_bssid[4]&&fr[15]==from_bssid[5]))){
                    u32 hl=24u; if(fc&0x80)hl+=2u; u32 llc=hoff+hl;
                    if(llc+8u<=fe && base[llc]==0xAA && base[llc+6]==0x88 && base[llc+7]==0x8E){
                        u32 ep=llc+8; int el2=(int)(fe-ep); if(el2>512)el2=512;
                        for(int c=0;c<el2;c++)g_eap_stash[c]=base[ep+c]; g_eap_stashlen=el2;
                    }
                }
                if(fc==want_fc){
                    int ok=1; if(from_bssid){ for(int k=0;k<6;k++) if(fr[16+k]!=from_bssid[k])ok=0; }
                    if(tome){ for(int k=0;k<6;k++) if(fr[4+k]!=me[k])ok=0; }
                    if(ok){ blen=(int)(fe-hoff-24); if(blen>0 && body_out){ if(blen>256)blen=256; for(int c=0;c<blen;c++)body_out[c]=fr[24+c]; } match=1; }
                }
            }
            *(volatile u32*)(u64)base=0;
            *(volatile u16*)(u64)(g_rxring+ri*8)=(u16)RTW_RXBUF; *(volatile u16*)(u64)(g_rxring+ri*8+2)=0; *(volatile u32*)(u64)(g_rxring+ri*8+4)=(u32)(u64)base;
            g_rxidx++; if((int)g_rxidx>=(int)g_rxnum)g_rxidx=0; rtw_w16(m,0x03B4,(u16)g_rxidx);
            if(match){ if(body_len)*body_len=blen; return 1; }
        }
        rtw_delay_us(100); if((t&7)==0)ui_pump();
    }
    return 0;
}
u16 g_seq=0x10;
int g_wifi_assoc_qos=0, g_wifi_assoc_ht=0, g_wifi_ht_enable=1;
int rtw_assoc_ies_mid(u8* f,const scan_ap* ap,const u8* mid,int midl);
int rtw_assoc_ies(u8* f,const scan_ap* ap){ return rtw_assoc_ies_mid(f,ap,0,0); }
int rtw_assoc_ies_mid(u8* f,const scan_ap* ap,const u8* mid,int midl){ int n=0;
    if(ap->ch>14){ static const u8 R5[8]={0x8C,0x12,0x98,0x24,0xB0,0x48,0x60,0x6C}; f[n++]=1; f[n++]=8; for(int i=0;i<8;i++)f[n++]=R5[i]; }
    else { static const u8 R2[8]={0x82,0x84,0x8B,0x96,0x0C,0x12,0x18,0x24}; f[n++]=1; f[n++]=8; for(int i=0;i<8;i++)f[n++]=R2[i]; f[n++]=50; f[n++]=4; f[n++]=0x30; f[n++]=0x48; f[n++]=0x60; f[n++]=0x6C; }
    for(int i=0;i<midl;i++)f[n++]=mid[i];
    int ht=g_wifi_ht_enable&&ap->ht&&ap->wmm;
    if(ht){ f[n++]=45; f[n++]=26; f[n++]=0x2C; f[n++]=0x01; f[n++]=0x17; f[n++]=0xFF; for(int i=1;i<16;i++)f[n++]=0; f[n++]=0; f[n++]=0; for(int i=0;i<4;i++)f[n++]=0; f[n++]=0; }
    if(ap->wmm){ f[n++]=221; f[n++]=7; f[n++]=0x00; f[n++]=0x50; f[n++]=0xF2; f[n++]=0x02; f[n++]=0x00; f[n++]=0x01; f[n++]=0x00; }
    g_wifi_assoc_qos=ap->wmm?1:0; g_wifi_assoc_ht=ht; return n; }
int rtw_assoc(u32 m,const scan_ap* ap,const char* ssid){
    u8 mac[6]; for(int i=0;i<6;i++)mac[i]=rtw_r8(m,0x0610+i);
    { int z=1; for(int i=0;i<6;i++) if(mac[i])z=0; if(z&&rtw_efuse_valid){ for(int i=0;i<6;i++){mac[i]=g_rtw_efuse.mac[i]; rtw_w8(m,0x0610+i,mac[i]);} } }
    for(int i=0;i<6;i++)rtw_w8(m,0x0618+i,ap->bssid[i]);
    rtw_set_nettype(m,0,1);
    rtw_set_channel_2g(m,ap->ch?ap->ch:6); { extern void rtw_edca_defaults(u32,int); rtw_edca_defaults(m,ap->ch>14); } rtw_chip_calibrate(m);
    rtw_rx_ring_up(m);
    u8 f[256]; int fl;
    fl=0; f[fl++]=0xB0; f[fl++]=0x00; f[fl++]=0;f[fl++]=0;
    for(int i=0;i<6;i++)f[fl++]=ap->bssid[i];
    for(int i=0;i<6;i++)f[fl++]=mac[i];
    for(int i=0;i<6;i++)f[fl++]=ap->bssid[i];
    f[fl++]=(u8)(g_seq<<4); f[fl++]=(u8)(g_seq>>4); g_seq++;
    f[fl++]=0;f[fl++]=0;
    f[fl++]=1;f[fl++]=0;
    f[fl++]=0;f[fl++]=0;
    u8 body[256]; int blen=0;
    if(!rtw_mgmt_xchg(m,f,fl,0xB0,ap->bssid,body,&blen,"auth")) return 1;
    rtw_jlog("  auth answer, status","",blen>=6?(body[4]|(body[5]<<8)):-1);
    if(blen>=6 && (body[4]|(body[5]<<8))!=0) return 2;
    fl=0; f[fl++]=0x00; f[fl++]=0x00; f[fl++]=0;f[fl++]=0;
    for(int i=0;i<6;i++)f[fl++]=ap->bssid[i];
    for(int i=0;i<6;i++)f[fl++]=mac[i];
    for(int i=0;i<6;i++)f[fl++]=ap->bssid[i];
    f[fl++]=(u8)(g_seq<<4); f[fl++]=(u8)(g_seq>>4); g_seq++;
    f[fl++]=0x21;f[fl++]=0x00;
    f[fl++]=0x0A;f[fl++]=0x00;
    int sl=0; while(ssid[sl]&&sl<32)sl++;
    f[fl++]=0; f[fl++]=(u8)sl; for(int i=0;i<sl;i++)f[fl++]=(u8)ssid[i];
    fl+=rtw_assoc_ies(f+fl,ap);
    if(!rtw_mgmt_xchg(m,f,fl,0x10,ap->bssid,body,&blen,"assoc")) return 3;
    rtw_jlog("  assoc answer, status","",blen>=4?(body[2]|(body[3]<<8)):-1);
    if(blen>=4 && (body[2]|(body[3]<<8))!=0) return 4;
    if(blen>=6)rtw_w16(m,0x06A8,(u16)((rtw_r16(m,0x06A8)&~0x7FFu)|((body[4]|(body[5]<<8))&0x7FFu)));
    rtw_set_nettype(m,2,1);
    return 0;
}

u32 rtw_find_mmio(void);
static int rtwh_init(void* c){ (void)c; return rtw_find_mmio()?0:-1; }
static int rtwh_scan_start(void* c,const u8* ch,int n){ (void)c; u32 m=rtw_find_mmio(); if(!m||!ch||n<1)return -1; rtw_set_channel_2g(m,ch[0]); return 0; }
static void rtwh_leave(void* c){ (void)c; u32 m=rtw_find_mmio(); if(m)rtw_ra_disconnect(m); }
static int rtwh_tx(void* c,const u8* f,int n){ (void)c; u32 m=rtw_find_mmio(); return m?rtw_tx_mgmt(m,f,n):-1; }
static int rtwh_rssi(void* c){ (void)c; return g_wifi_rssi; }
static int rtwh_mac(void* c,u8* mac){ (void)c; if(!rtw_efuse_valid)return -1; for(int i=0;i<6;i++)mac[i]=g_rtw_efuse.mac[i]; return 0; }
static const struct wifi_hw_ops rtw_hw_ops={ .name="rtw88", .flags=WIFI_HW_NATIVE, .init=rtwh_init, .scan_start=rtwh_scan_start, .leave=rtwh_leave, .tx=rtwh_tx, .get_rssi=rtwh_rssi, .get_mac=rtwh_mac };
void rtw_wifi_hw_probe(void){ if(rtw_find_mmio())wifi_hw_register(&rtw_hw_ops,0); }
