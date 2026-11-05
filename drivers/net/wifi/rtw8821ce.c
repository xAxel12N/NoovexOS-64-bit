#include "include/nvx/base.h"
#include "include/nvx/mem/pmm_vmm.h"
#include "include/nvx/lib/js/js.h"
#include "include/nvx/shell/terminal.h"
#include "include/nvx/fs/fat/fat_ata.h"
#include "include/nvx/drivers/net/wifi/rtw_efuse.h"
#include "include/nvx/drivers/net/wifi/rtw8821ce.h"
#include "include/nvx/graphics/ui/desktop.h"
#include "include/nvx/core/init/splash.h"
int rtw_apply_one(u32 m,u32 off,u8 sz,u32 v){
    if(sz==5){
        if(off==0xFFEu){ rtw_delay_us(50000); return 1; }
        if(off==0xFEu){ rtw_delay_us(105); return 1; }
        rtw_rf_write_a(m,off,v); return 1;
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
int rtw_bcnq_stage(u32 m,const u8* payload,u32 plen){ return rtw_bcnq_stage_pg(m,payload,plen,460); }
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
int rtw_fw_download_iddma(u32 m,const u8* fw,long fwsz){
    if(fwsz<64)return -10;
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

    rtw_w32(m,0x0210,(1u<<2));
    u16 fc=rtw_r16(m,0x0080);
    if((fc&((1u<<4)|(1u<<6)))==((1u<<4)|(1u<<6))){ fc=(u16)((fc|(1u<<14))&~1u); rtw_w16(m,0x0080,fc); }

    rtw_w8(m,0x001D,(u8)(rtw_r8(m,0x001D)|1u));
    rtw_w8(m,0x0003,(u8)(rtw_r8(m,0x0003)|(1u<<2)));

    u32 fwready=(1u<<15)|(1u<<14)|(1u<<3)|(1u<<5)|((1u<<4)|(1u<<6));
    u32 fwmask=0xFFFFu & ~((1u<<12)|(1u<<13));
    if(rtw_hw_ready(m,0x0080,fwmask,fwready))return 0;
    return 1;
}

static void rtw_desc_chksum(u8* d){
    d[14]=0; d[15]=0;
    u16 ck=0; for(int i=0;i<16;i++) ck^=(u16)(d[i*2]|(d[i*2+1]<<8));
    d[14]=(u8)(ck&0xFF); d[15]=(u8)(ck>>8);
}
static int rtw_bcnq_prep_done=0;
static u8* rtw_bcn_bd=0;

int rtw_mac_trx_init(u32 m){
    u16 pqmap=0;
    pqmap|=((2u&3u)<<0);
    pqmap|=((2u&3u)<<6);
    pqmap|=((2u&3u)<<8);
    pqmap|=((1u&3u)<<10);
    pqmap|=((0u&3u)<<12);
    pqmap|=((3u&3u)<<14);
    rtw_w16(m,0x010C,pqmap);
    rtw_w8(m,0x0100,0);
    rtw_w8(m,0x0100,0xFF);
    rtw_w32(m,0x1330,(1u<<31));

    u32 rsvd_boundary=460, h2cq_addr_pg=500;
    u16 hq=16,nq=16,lq=16,exq=14,gap=1;
    u16 acq=460;
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
    rtw_w32(m,0x011C,16384u-256u-1u);              /* REG_RXFF_BNDY = rxff - C2H_BUF - 1 */
    rtw_w16(m,0x0116,(u16)(16384u-128u-1u));       /* REG_TRXFF_BNDY+2 = rxff - REPORT_BUF - 1 */
    rtw_w8(m,0x060F,4);                            /* REG_RX_DRVINFO_SZ = PHY_STATUS_SIZE(4) - critical for RX parse offset */
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
    return 0;
}
int rtw_mac_trx_ok=0;
static void rtw_bcnq_prep(u32 m,u8* bd){
    if(!rtw_mac_trx_ok){ if(rtw_mac_trx_init(m)==0)rtw_mac_trx_ok=1; }
    u8 pc=rtw_r8(m,0x0303); rtw_w8(m,0x0303,(u8)(pc|0xF7));
    rtw_w32(m,0x0308,(u32)(u64)bd);
    rtw_delay_us(50);
}
static int rtw_bcnq_stage_pg(u32 m,const u8* payload,u32 plen,u32 pghead){
    static u8* pkt=0;
    if(!pkt){ pkt=(u8*)dma_alloc(8192); rtw_bcn_bd=(u8*)dma_alloc(64); }
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
    rtw_desc_chksum(pkt);
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
    return ok?0:-2;
}
u8* g_rxring=0; u8* g_rxbuf=0; u32 g_rxnum=0; u32 g_rxidx=0;
u32 rtw_rf_read_a(u32 m,u32 addr){ return rtw_r32(m,0x2800u+((addr&0xFFu)<<2)) & 0x000FFFFFu; }
static void rtw_rf_write_mask_a(u32 m,u32 addr,u32 mask,u32 val){
    if(mask==0x000FFFFFu){ rtw_rf_write_a(m,addr,val); return; }
    u32 sh=0; while(sh<20 && !((mask>>sh)&1u))sh++;
    u32 cur=rtw_rf_read_a(m,addr);
    cur=(cur&~mask)|((val<<sh)&mask);
    rtw_rf_write_a(m,addr,cur);
}
static void rtw_switch_to_wlg(u32 m){
    rtw_w32(m,0x1080,rtw_r32(m,0x1080)|(1u<<16));
    rtw_w32(m,0x0000,rtw_r32(m,0x0000)|(1u<<26));
    u32 reg=rtw_r32(m,0x0cb8);
    reg|=(1u<<20)|(1u<<22)|(1u<<21);
    reg&=~((1u<<16)|(1u<<18)|(1u<<23));
    u32 a84=rtw_r32(m,0x0a84); a84=(a84&~0x00FF0000u)|(0x12u<<16); rtw_w32(m,0x0a84,a84);
    u32 a80=rtw_r32(m,0x0a80); a80=(a80&~0x0000FFFFu)|0x7532u; rtw_w32(m,0x0a80,a80);
    rtw_w32(m,0x0cb8,reg);
}
static int g_chstep=0;
int g_chquiet=0;
static void chmark(const char* t){ if(g_chquiet)return; tcol(0x9AA0B0u); term_puts(t); term_putc('\n'); tcol(0xFFFFFFu); }
static void rtw_switch_to_wla(u32 m){
    rtw_w32(m,0x1080,rtw_r32(m,0x1080)|(1u<<16));
    rtw_w32(m,0x0000,rtw_r32(m,0x0000)|(1u<<26));
    u32 reg=rtw_r32(m,0x0cb8); reg|=(1u<<20)|(1u<<22)|(1u<<23); reg&=~((1u<<16)|(1u<<18)|(1u<<21)); rtw_w32(m,0x0cb8,reg);
}
static void rtw_switch_to_btg(u32 m){
    rtw_w32(m,0x1080,rtw_r32(m,0x1080)|(1u<<16));
    rtw_w32(m,0x0000,rtw_r32(m,0x0000)|(1u<<26));
    u32 reg=rtw_r32(m,0x0cb8); reg|=(1u<<16); reg&=~((1u<<18)|(1u<<20)|(1u<<21)|(1u<<22)|(1u<<23));
    u32 a84=rtw_r32(m,0x0a84); a84=(a84&~0x00FF0000u)|(0x0Eu<<16); rtw_w32(m,0x0a84,a84);
    u32 a80=rtw_r32(m,0x0a80); a80=(a80&~0x0000FFFFu)|0xFC84u; rtw_w32(m,0x0a80,a80);
    rtw_w32(m,0x0cb8,reg);
}
static int rtw_rfe(void){ return rtw_efuse_valid?(rtw_logmap[0xCA]&0x1f):0; }
static int rtw_rfe_btg(void){ int r=rtw_rfe(); return r==2||r==4||r==7||r==0xa||r==0xc||r==0xf; }
static void rtw_wm32(u32 m,u32 r,u32 mask,u32 v);
static void rtw_bw20(u32 m,u8 ch){
    u32 v=rtw_r32(m,0x08AC); v=(v&0xffcffc00u)|0x10010000u; rtw_w32(m,0x08AC,v);
    rtw_wm32(m,0x08C4,1u<<30,1);
    rtw_wm32(m,0x0948,3u<<28,2); rtw_wm32(m,0x094C,3u<<28,2); rtw_wm32(m,0x0C20,1u<<31,1); rtw_wm32(m,0x08F0,1u<<31,0);
    static const u32 SW[4]={0x200,0x16a,0x101,0x0b6}; u8 sv=rtw_efuse_valid?rtw_logmap[ch<=14?0xC6:0xC7]:0; if(sv>9)sv=0; rtw_wm32(m,0x0c1c,0xFFE00000u,SW[sv/3]);
}
static void rtw_wm32(u32 m,u32 r,u32 mask,u32 v){ u32 sh=0; while(sh<31&&!((mask>>sh)&1u))sh++; u32 c=rtw_r32(m,r); c=(c&~mask)|((v<<sh)&mask); rtw_w32(m,r,c); }
static void rtw_bb_band(u32 m,u8 ch){
    if(ch<=14){ rtw_wm32(m,0x0808,1u<<28,1); rtw_wm32(m,0x0454,1u<<7,0); rtw_wm32(m,0x0a80,1u<<18,0); rtw_wm32(m,0x0814,0x0000FC00u,15); rtw_wm32(m,0x0c1c,0xF00u,0); rtw_wm32(m,0x0860,0x1FFE0000u,0x96A); return; }
    rtw_wm32(m,0x0a80,1u<<18,1); rtw_wm32(m,0x0454,1u<<7,1); rtw_wm32(m,0x0808,1u<<28,0); rtw_wm32(m,0x0814,0x0000FC00u,15);
    rtw_wm32(m,0x0c1c,0xF00u,ch<=64?1u:ch<=144?2u:3u);
    rtw_wm32(m,0x0860,0x1FFE0000u,ch<=48?0x494u:ch<=64?0x453u:ch<=116?0x452u:0x412u);
}
int g_wifi_txpwr=1, g_wifi_band5=0; u8 g_wifi_curch=0; u8 g_wifi_pwr[20];
static int rtw_s4(u8 v){ v&=0xF; return (v&8)?(int)v-16:(int)v; }
static int rtw_pg2(u8 c){ return c<=2?0:c<=5?1:c<=8?2:c<=11?3:c<=13?4:5; }
static int rtw_pg5(u8 c){ if(c<=42)return 0; if(c<=50)return 1; if(c<=58)return 2; if(c<=64)return 3; if(c<=106)return 4; if(c<=114)return 5; if(c<=122)return 6; if(c<=130)return 7; if(c<=138)return 8; if(c<=144)return 9; if(c<=155)return 10; if(c<=161)return 11; if(c<=171)return 12; return 13; }
static int rtw_clp(int v,int d){ if(v==0xFF||v<0)return d; return v>63?63:v; }
void rtw_set_txpwr(u32 m,u8 ch){
    if(!g_wifi_txpwr||!rtw_efuse_valid)return; const u8* e=rtw_logmap+0x10; int cck=0x20,b40,d;
    if(ch<=14){ int g=rtw_pg2(ch); cck=rtw_clp(e[g],0x20); b40=rtw_clp(e[6+(g>4?4:g)],0x20); d=e[11]; }
    else { b40=rtw_clp(e[18+rtw_pg5(ch)],0x20); d=e[32]; }
    if(d==0xFF)d=0; int of=b40+rtw_s4((u8)d), b20=b40+rtw_s4((u8)(d>>4));
    for(int r=0;r<20;r++){ int v=r<4?cck:r<12?of:b20; if(v<0)v=0; if(v>63)v=63; g_wifi_pwr[r]=(u8)v; }
    for(int r=(ch<=14?0:4);r<20;r+=4) rtw_w32(m,0x1d00u+(u32)r,(u32)g_wifi_pwr[r]|((u32)g_wifi_pwr[r+1]<<8)|((u32)g_wifi_pwr[r+2]<<16)|((u32)g_wifi_pwr[r+3]<<24));
}
void rtw_set_channel_2g(u32 m,u8 ch){
    int g5=ch>14;
    chmark("    ch.a read RF18 (0x2860)");
    u32 rf18=rtw_rf_read_a(m,0x18);
    g_chstep=2;
    rf18&=~(((1u<<16)|(1u<<9)|(1u<<8)) | 0xFFu | ((1u<<18)|(1u<<17)) | ((1u<<11)|(1u<<10)));
    rf18|=(ch&0xFFu);
    if(g5){ rf18|=(1u<<16)|(1u<<8); if(ch>=100&&ch<=140)rf18|=(1u<<17); else if(ch>140)rf18|=(1u<<18); }
    rf18|=(1u<<11)|(1u<<10);
    chmark("    ch.b antenna switch (0xcb8/0xa80/0xa84)");
    if(g5)rtw_switch_to_wla(m); else if(rtw_rfe_btg())rtw_switch_to_btg(m); else rtw_switch_to_wlg(m);
    g_chstep=3;
    chmark("    ch.c RF LUTDBG/0x64");
    if(g5)rtw_rf_write_mask_a(m,0xDF,(1u<<6),0);
    else { rtw_rf_write_mask_a(m,0xDF,(1u<<6),1); rtw_rf_write_mask_a(m,0x64,0xFu,0xF); }
    g_chstep=4;
    chmark("    ch.d RF18 write (the channel)");
    rtw_rf_write_a(m,0x18,rf18);
    rtw_delay_us(100);
    chmark("    ch.e RF XTALX2 toggle");
    rtw_rf_write_mask_a(m,0xB8,(1u<<19),0);
    rtw_rf_write_mask_a(m,0xB8,(1u<<19),1);
    g_chstep=5;
    chmark("    ch.f MAC channel regs (0x483/0x668)");
    rtw_w8(m,0x0483,0);
    u32 v=rtw_r32(m,0x0668); v&=~((1u<<7)|(1u<<8)); rtw_w32(m,0x0668,v);
    if(g5||g_wifi_band5)rtw_bb_band(m,ch);
    rtw_bw20(m,ch);
    g_wifi_band5=g5; g_wifi_curch=ch;
    rtw_set_txpwr(m,ch);
    rtw_delay_us(2000);
    g_chstep=6;
}
static int rtw_rx_enable_safe(u32 m){
    rtw_w32(m,0x0608,0xE400220Eu);   /* WLAN_RCR_CFG: accept all beacons (no CBSSID filter) */
    return 1;
}
static u32 rtw_rx_count(u32 m){ return rtw_r32(m,0x02B0); }
int rtw_rx_enable(u32 m){
    rtw_w32(m,0x0608,0xE400220Eu);   /* WLAN_RCR_CFG (no CBSSID_BCN filter) */
    rtw_w32(m,0x0608,rtw_r32(m,0x0608)|(1u<<28));
    u32 cr=rtw_r32(m,0x0100); cr|=(1u<<3)|(1u<<1); rtw_w32(m,0x0100,cr);
    return 1;
}
int rtw_rx_ring_up(u32 m){
    if(!g_rxring||!g_rxbuf||!g_rxnum)return 0;
    for(u32 i=0;i<g_rxnum;i++){ u8* d=g_rxring+i*8; u64 pb=(u64)(g_rxbuf+i*2048);
        *(volatile u16*)(d)=2048; *(volatile u16*)(d+2)=0; *(volatile u32*)(d+4)=(u32)pb; }
    cpu_wmb();
    /* rtw88 rtw_pci_reset_buf_desc order: NUM, DESA, RWPTR_CLR, rp=0; THEN dma_reset (CTRL). */
    rtw_w16(m,0x0382,(u16)(g_rxnum & 0x0FFF));      /* RXBD_NUM_MPDUQ */
    rtw_w32(m,0x0338,(u32)(u64)g_rxring);           /* RXBD_DESA_MPDUQ */
    rtw_w32(m,0x039C,0xFFFFFFFFu);                  /* TXBD_RWPTR_CLR: reset all r/w pointers */
    rtw_w16(m,0x03B4,0);                            /* RXBD_IDX_MPDUQ: host rp = 0 (matches source) */
    /* dma_reset LAST: arm the TRX DMA interface + rx tag */
    rtw_w32(m,0x0300,rtw_r32(m,0x0300)|(1u<<20)|(1u<<15));   /* RST_TRXDMA_INTF | RX_TAG_EN */
    g_rxidx=0;
    /* MAC TRX enable */
    u32 cr=rtw_r32(m,0x0100); cr|=(1u<<2)|(1u<<3)|(1u<<4)|(1u<<5)|(1u<<6)|(1u<<7); rtw_w32(m,0x0100,cr);
    rtw_w16(m,0x0522,0);   /* clear TXPAUSE */
    rtw_delay_us(2000);
    return 1;
}
int rtw_rx_hw_wp(u32 m){ u32 t=rtw_r32(m,0x03B4); return (int)((t>>16)&0x0FFF); }

scan_ap g_aps[24]; int g_apn=0;
extern volatile u64 g_ticks;
u8 g_scan_grp=0;
static void scan_add(const u8* bssid,const char* ssid,u8 ch,u8 sec,int rssi,u8 ht,u8 akm,u8 pmf,u8 wmm){
    int hid=!ssid[0]; if(rssi<-127)rssi=-127;
    for(int i=0;i<g_apn;i++){ int eq=1; for(int b=0;b<6;b++)if(g_aps[i].bssid[b]!=bssid[b])eq=0;
        if(eq){ g_aps[i].seen++; if(ch)g_aps[i].ch=ch; g_aps[i].sec=sec; if(rssi>-127)g_aps[i].rssi=(signed char)(g_aps[i].rssi<=-127?rssi:(g_aps[i].rssi*3+rssi)/4); g_aps[i].ht=ht; g_aps[i].akm=akm; g_aps[i].pmf=pmf; g_aps[i].wmm=wmm; g_aps[i].grp=g_scan_grp; g_aps[i].last=g_ticks;
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
int rtw_scan_drain(u32 m,u8 ch){
    int found=0;
    for(int guard=0; guard<(int)g_rxnum && found<64; guard++){
        int wp=rtw_rx_hw_wp(m);
        if((int)(g_rxidx%g_rxnum)==(wp%(int)g_rxnum))break;
        u32 i=g_rxidx % g_rxnum;
        u8* base=g_rxbuf+i*2048;
        u32 w0=*(volatile u32*)(u64)base;
        u32 plen=w0&0x3FFFu; u32 crc=(w0>>14)&1u; u32 drvsz=((w0>>16)&0xFu)*8u; u32 shift=(w0>>24)&3u;
        /* HARD read-side clamp: nothing below may ever index base[] past RXBUFSZ, no matter what the chip wrote. */
        #define RXBUFSZ 2048u
        u32 hoff=24u+drvsz+shift;
        u32 frame_end=hoff+plen; if(frame_end>RXBUFSZ)frame_end=RXBUFSZ;   /* clamp true frame extent into the buffer */
        if(plen>=40 && !crc && hoff+36u<=frame_end){
            const u8* fr=base+hoff; u8 fc=fr[0];
            if((fc&0xFC)==0x80||(fc&0xFC)==0x50){
                u8 bssid[6]; for(int b=0;b<6;b++)bssid[b]=fr[16+b];
                u8 sec=0, ht=0, akm=0, pmf=0, wmm=0, dsch=0; g_rsn_grp=0; int rssi=rtw_rx_rssi(base);
                /* capability field is 2 bytes at body+10 (after 8-byte timestamp + 2-byte beacon int) */
                u32 capoff=hoff+24u+10u;
                if(capoff+2u<=frame_end){ u16 cap=base[capoff]|(base[capoff+1]<<8); if(cap&(1u<<4))sec=1; } /* Privacy bit -> WEP/WPA */
                u32 iep=hoff+24u+12u; char ss[33]; ss[0]=0; int gotssid=0;
                while(iep+2u<=frame_end){
                    u8 tag=base[iep], tl=base[iep+1];
                    if((u32)(iep+2u+tl)>frame_end)break;
                    if(tag==0 && !gotssid){ int n=tl<32?tl:32; for(int c=0;c<n && (iep+2u+(u32)c)<RXBUFSZ;c++){u8 c2=base[iep+2u+c]; ss[c]=(c2>=32&&c2<127)?(char)c2:'.';} ss[n]=0; if(!tl||!base[iep+2])ss[0]=0; gotssid=1; }
                    else if(tag==48){ sec=2; rtw_rsn_parse(base+iep+2,tl,&akm,&pmf); }
                    else if(tag==3&&tl>=1){ dsch=base[iep+2]; }
                    else if(tag==45&&tl>=26){ ht=(u8)(1|((base[iep+2]&0x20)?2:0)); }
                    else if(tag==221 && tl>=5 && base[iep+2]==0x00 && base[iep+3]==0x50 && base[iep+4]==0xF2 && base[iep+5]==0x02){ wmm=1; }
                    else if(tag==221 && tl>=4 && base[iep+2]==0x00 && base[iep+3]==0x50 && base[iep+4]==0xF2 && base[iep+5]==0x01){ if(sec<2)sec=2; } /* WPA vendor IE */
                    iep+=2u+tl;
                }
                if(sec==2&&!akm)akm=AKM_PSK;
                { extern u8 g_scan_grp; g_scan_grp=g_rsn_grp; }
                scan_add(bssid,ss,dsch?dsch:ch,sec,(dsch&&dsch!=ch&&rssi>-100)?rssi-12:rssi,ht,akm,pmf,wmm);
                found++;
            }
        }
        #undef RXBUFSZ
        *(volatile u32*)(u64)base=0;
        *(volatile u16*)(u64)(g_rxring+i*8)=2048;
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
        u8* base=g_rxbuf+i*2048;
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
int g_probe_txok=0; u32 g_probe_hwb=0, g_probe_hwa=0;
int rtw_tx_probe(u32 m,int* got_resp,char* resp_ssid){
    if(!g_bering||!g_benum||!g_rxnum||!g_rxbuf)return -1;
    static u8* pkt=0; if(!pkt)pkt=(u8*)dma_alloc(2048);
    if(!pkt)return -1;
    u8 mac[6]; for(int i=0;i<6;i++)mac[i]=rtw_r8(m,0x0610+i);
    int zero=1; for(int i=0;i<6;i++) if(mac[i])zero=0;
    if(zero && rtw_efuse_valid){ for(int i=0;i<6;i++)mac[i]=rtw_logmap[0xD0+i]; for(int i=0;i<6;i++)rtw_w8(m,0x0610+i,mac[i]); }
    /* build 802.11 probe request: FC=0x0040 subtype4, dur, DA=bcast, SA=mac, BSSID=bcast, seq */
    u8 frame[128]; int fl=0;
    frame[fl++]=0x40; frame[fl++]=0x00;      /* frame control: mgmt, probe req */
    frame[fl++]=0; frame[fl++]=0;            /* duration */
    for(int i=0;i<6;i++)frame[fl++]=0xFF;    /* DA broadcast */
    for(int i=0;i<6;i++)frame[fl++]=mac[i];  /* SA */
    for(int i=0;i<6;i++)frame[fl++]=0xFF;    /* BSSID broadcast */
    frame[fl++]=0; frame[fl++]=0;            /* seq ctrl (hw fills) */
    frame[fl++]=0; frame[fl++]=0;            /* IE SSID: wildcard (len 0) */
    frame[fl++]=1; frame[fl++]=8;            /* IE supported rates */
    frame[fl++]=0x82;frame[fl++]=0x84;frame[fl++]=0x8B;frame[fl++]=0x96;
    frame[fl++]=0x0C;frame[fl++]=0x12;frame[fl++]=0x18;frame[fl++]=0x24;
    /* TX descriptor: 48 bytes, then frame */
    u32 total=48+fl;
    for(u32 i=0;i<total;i++)pkt[i]=0;
    u32 w0=((u32)fl & 0xFFFFu) | ((48u&0xFFu)<<16) | (1u<<24) | (1u<<26);   /* broadcast probe: BMC ok, LS, no DISQSELSEQ */
    *(volatile u32*)(pkt+0)=w0;
    u32 w1=((u32)18u<<8) | ((u32)8u<<16);    /* QSEL=MGMT(18), RATE_ID=B_20M(8) */
    *(volatile u32*)(pkt+4)=w1;
    *(volatile u32*)(pkt+12)=(1u<<8)|(1u<<10); /* w3: USE_RATE|DISDATAFB */
    *(volatile u32*)(pkt+16)=0x00u;          /* w4: DATARATE=1M(0) */
    rtw_txdesc_rate(pkt,1);
    *(volatile u32*)(pkt+32)=(1u<<15);       /* w8: EN_HWSEQ */
    for(int i=0;i<fl;i++)pkt[48+i]=frame[i];
    /* BE ring buffer descriptor: two 8-byte entries */
    u32 i=g_beidx % g_benum;
    u8* bd=g_bering+i*16;
    u32 psb=((total-1)/128)+1;
    *(volatile u16*)(bd+0)=48; *(volatile u16*)(bd+2)=(u16)psb; *(volatile u32*)(bd+4)=(u32)(u64)pkt;
    *(volatile u16*)(bd+8)=(u16)fl; *(volatile u16*)(bd+10)=0; *(volatile u32*)(bd+12)=(u32)(u64)(pkt+48);
    cpu_wmb();
    /* make sure BE ring regs are live (base/num/rwptr) */
    rtw_w32(m,0x0328,(u32)(u64)g_bering);
    rtw_w16(m,0x0388,(u16)(g_benum&0x0FFF));
    u32 hw_before=(rtw_r32(m,0x03A8)>>16)&0x0FFF;
    g_beidx=(g_beidx+1)%g_benum;
    rtw_w16(m,0x03A8,(u16)g_beidx);   /* TXBD_IDX_BEQ: kick */
    /* poll HW read-index to see if the frame was consumed (transmitted) */
    u32 hw_after=hw_before; int txok=0;
    for(int t=0;t<20000;t++){ hw_after=(rtw_r32(m,0x03A8)>>16)&0x0FFF; if(hw_after!=hw_before){txok=1;break;} rtw_delay_us(50); }
    g_probe_txok=txok; g_probe_hwb=hw_before; g_probe_hwa=hw_after;
    /* now listen for a probe response (subtype 5, FC 0x50) in the RX ring */
    *got_resp=0; resp_ssid[0]=0;
    for(int t=0;t<40000 && !*got_resp;t++){
        int wp=rtw_rx_hw_wp(m);
        while((int)(g_rxidx%g_rxnum)!=(wp%(int)g_rxnum) && !*got_resp){
            u32 ri=g_rxidx % g_rxnum; u8* base=g_rxbuf+ri*2048;
            u32 rw0=*(volatile u32*)(u64)base; u32 plen=rw0&0x3FFFu;
            u32 drvsz=((rw0>>16)&0xFu)*8u; u32 shift=(rw0>>24)&3u; u32 crc=(rw0>>14)&1u;
            u32 hoff=24u+drvsz+shift; u32 fe=hoff+plen; if(fe>2048u)fe=2048u;
            if(plen>=40 && !crc && hoff+36u<=fe){
                const u8* fr=base+hoff; u8 fc=fr[0];
                if((fc&0xFC)==0x50){   /* probe response */
                    u32 iep=hoff+24u+12u;
                    while(iep+2u<=fe){ u8 tag=base[iep],tl=base[iep+1]; if((u32)(iep+2u+tl)>fe)break;
                        if(tag==0){ int n=tl<32?tl:32; for(int c=0;c<n && iep+2u+(u32)c<2048u;c++){u8 c2=base[iep+2u+c]; resp_ssid[c]=(c2>=32&&c2<127)?(char)c2:'.';} resp_ssid[n]=0; break; }
                        iep+=2u+tl; }
                    *got_resp=1;
                }
            }
            *(volatile u32*)(u64)base=0;
            *(volatile u16*)(u64)(g_rxring+ri*8)=2048; *(volatile u32*)(u64)(g_rxring+ri*8+4)=(u32)(u64)base;
            g_rxidx++; if((int)g_rxidx>=(int)g_rxnum)g_rxidx=0;
            rtw_w16(m,0x03B4,(u16)g_rxidx);
        }
        rtw_delay_us(100); if((t&7)==0)ui_pump();
    }
    return 0;
}
int rtw_tx_mgmt(u32 m,const u8* frame,int fl){
    if(!g_mgring||!g_mgnum)return -1;
    static u8* pkt=0; if(!pkt)pkt=(u8*)dma_alloc(2048); if(!pkt)return -1;
    u32 total=48+(u32)fl; for(u32 i=0;i<total;i++)pkt[i]=0;
    /* unicast mgmt: NO BMC bit. LS set. EN_HWSEQ handles seq, so DISQSELSEQ off. */
    u32 w0=((u32)fl&0xFFFFu)|((48u&0xFFu)<<16)|(1u<<26);
    *(volatile u32*)(pkt+0)=w0;
    *(volatile u32*)(pkt+4)=((u32)18u<<8)|((u32)8u<<16);   /* QSEL=MGMT(18), RATE_ID=B */
    *(volatile u32*)(pkt+12)=(1u<<8)|(1u<<10);             /* USE_RATE|DISDATAFB */
    *(volatile u32*)(pkt+16)=0x00u;                        /* DATARATE=1M CCK */
    rtw_txdesc_rate(pkt,1);
    *(volatile u32*)(pkt+32)=(1u<<15);                     /* EN_HWSEQ */
    for(int i=0;i<fl;i++)pkt[48+i]=frame[i];
    u32 i=g_mgidx % g_mgnum; u8* bd=g_mgring+i*16;
    u32 psb=((total-1)/128)+1;
    *(volatile u16*)(bd+0)=48; *(volatile u16*)(bd+2)=(u16)psb; *(volatile u32*)(bd+4)=(u32)(u64)pkt;
    *(volatile u16*)(bd+8)=(u16)fl; *(volatile u16*)(bd+10)=0; *(volatile u32*)(bd+12)=(u32)(u64)(pkt+48);
    cpu_wmb();
    rtw_w32(m,0x0310,(u32)(u64)g_mgring); rtw_w16(m,0x0380,(u16)(g_mgnum&0x0FFF));
    g_mgidx=(g_mgidx+1)%g_mgnum; rtw_w16(m,0x03B0,(u16)g_mgidx);   /* TXBD_IDX_MGMTQ */
    return 0;
}
u8 g_eap_stash[512]; int g_eap_stashlen=0;
int rtw_rx_wait_mgmt(u32 m,u8 want_fc,const u8* from_bssid,u8* body_out,int* body_len,int ms){
    if(!g_rxnum||!g_rxbuf)return 0;
    for(int t=0;t<ms*10;t++){
        int wp=rtw_rx_hw_wp(m);
        while((int)(g_rxidx%g_rxnum)!=(wp%(int)g_rxnum)){
            u32 ri=g_rxidx % g_rxnum; u8* base=g_rxbuf+ri*2048;
            u32 rw0=*(volatile u32*)(u64)base; u32 plen=rw0&0x3FFFu;
            u32 drvsz=((rw0>>16)&0xFu)*8u; u32 shift=(rw0>>24)&3u; u32 crc=(rw0>>14)&1u;
            u32 hoff=24u+drvsz+shift; u32 fe=hoff+plen; if(fe>2048u)fe=2048u;
            int match=0; int blen=0;
            if(plen>=28 && !crc && hoff+24u<=fe){
                const u8* fr=base+hoff; u8 fc=fr[0];
                if((fc&0x0C)==0x08 && g_eap_stashlen==0){   /* stash an early EAPOL msg1 so it isn't lost */
                    u32 hl=24u; if(fc&0x80)hl+=2u; u32 llc=hoff+hl;
                    if(llc+8u<=fe && base[llc]==0xAA && base[llc+6]==0x88 && base[llc+7]==0x8E){
                        u32 ep=llc+8; int el2=(int)(fe-ep); if(el2>512)el2=512;
                        for(int c=0;c<el2;c++)g_eap_stash[c]=base[ep+c]; g_eap_stashlen=el2;
                    }
                }
                if(fc==want_fc){
                    int ok=1; if(from_bssid){ for(int k=0;k<6;k++) if(fr[16+k]!=from_bssid[k])ok=0; }
                    if(ok){ blen=(int)(fe-hoff-24); if(blen>0 && body_out){ if(blen>256)blen=256; for(int c=0;c<blen;c++)body_out[c]=fr[24+c]; } match=1; }
                }
            }
            *(volatile u32*)(u64)base=0;
            *(volatile u16*)(u64)(g_rxring+ri*8)=2048; *(volatile u32*)(u64)(g_rxring+ri*8+4)=(u32)(u64)base;
            g_rxidx++; if((int)g_rxidx>=(int)g_rxnum)g_rxidx=0; rtw_w16(m,0x03B4,(u16)g_rxidx);
            if(match){ if(body_len)*body_len=blen; return 1; }
        }
        rtw_delay_us(100); if((t&7)==0)ui_pump();
    }
    return 0;
}
u16 g_seq=0x10;
int g_wifi_assoc_qos=0, g_wifi_assoc_ht=0, g_wifi_ht_enable=1;
int rtw_assoc_ies(u8* f,const scan_ap* ap){ int n=0;
    if(ap->ch>14){ static const u8 R5[8]={0x8C,0x12,0x98,0x24,0xB0,0x48,0x60,0x6C}; f[n++]=1; f[n++]=8; for(int i=0;i<8;i++)f[n++]=R5[i]; }
    else { static const u8 R2[8]={0x82,0x84,0x8B,0x96,0x0C,0x12,0x18,0x24}; f[n++]=1; f[n++]=8; for(int i=0;i<8;i++)f[n++]=R2[i]; f[n++]=50; f[n++]=4; f[n++]=0x30; f[n++]=0x48; f[n++]=0x60; f[n++]=0x6C; }
    int ht=g_wifi_ht_enable&&ap->ht&&ap->wmm;
    if(ht){ f[n++]=45; f[n++]=26; f[n++]=0x2C; f[n++]=0x01; f[n++]=0x17; f[n++]=0xFF; for(int i=1;i<16;i++)f[n++]=0; f[n++]=0; f[n++]=0; for(int i=0;i<4;i++)f[n++]=0; f[n++]=0; }
    if(ap->wmm){ f[n++]=221; f[n++]=7; f[n++]=0x00; f[n++]=0x50; f[n++]=0xF2; f[n++]=0x02; f[n++]=0x00; f[n++]=0x01; f[n++]=0x00; }
    g_wifi_assoc_qos=ap->wmm?1:0; g_wifi_assoc_ht=ht; return n; }
int rtw_assoc(u32 m,const scan_ap* ap,const char* ssid){
    u8 mac[6]; for(int i=0;i<6;i++)mac[i]=rtw_r8(m,0x0610+i);
    { int z=1; for(int i=0;i<6;i++) if(mac[i])z=0; if(z&&rtw_efuse_valid){ for(int i=0;i<6;i++){mac[i]=rtw_logmap[0xD0+i]; rtw_w8(m,0x0610+i,mac[i]);} } }
    /* program BSSID + infra net type so hw accepts the AP's replies */
    for(int i=0;i<6;i++)rtw_w8(m,0x0618+i,ap->bssid[i]);
    u32 nt=rtw_r32(m,0x0100); nt=(nt&~0x30000u)|(2u<<16); rtw_w32(m,0x0100,nt);
    rtw_set_channel_2g(m,ap->ch?ap->ch:6);
    rtw_rx_ring_up(m);
    u8 f[256]; int fl;
    /* ---- AUTH request (open system, seq 1) ---- */
    fl=0; f[fl++]=0xB0; f[fl++]=0x00; f[fl++]=0;f[fl++]=0;
    for(int i=0;i<6;i++)f[fl++]=ap->bssid[i];
    for(int i=0;i<6;i++)f[fl++]=mac[i];
    for(int i=0;i<6;i++)f[fl++]=ap->bssid[i];
    f[fl++]=(u8)(g_seq<<4); f[fl++]=(u8)(g_seq>>4); g_seq++;
    f[fl++]=0;f[fl++]=0;   /* auth algorithm = open */
    f[fl++]=1;f[fl++]=0;   /* auth seq = 1 */
    f[fl++]=0;f[fl++]=0;   /* status = 0 */
    rtw_tx_mgmt(m,f,fl);
    u8 body[256]; int blen=0;
    if(!rtw_rx_wait_mgmt(m,0xB0,ap->bssid,body,&blen,800)) return 1;   /* no auth resp */
    /* auth resp: algo(2) seq(2) status(2) - status 0 == success */
    if(blen>=6 && (body[4]|(body[5]<<8))!=0) return 2;                 /* auth rejected */
    /* ---- ASSOC request ---- */
    fl=0; f[fl++]=0x00; f[fl++]=0x00; f[fl++]=0;f[fl++]=0;
    for(int i=0;i<6;i++)f[fl++]=ap->bssid[i];
    for(int i=0;i<6;i++)f[fl++]=mac[i];
    for(int i=0;i<6;i++)f[fl++]=ap->bssid[i];
    f[fl++]=(u8)(g_seq<<4); f[fl++]=(u8)(g_seq>>4); g_seq++;
    f[fl++]=0x21;f[fl++]=0x00;  /* capability: ESS + short preamble */
    f[fl++]=0x0A;f[fl++]=0x00;  /* listen interval */
    int sl=0; while(ssid[sl]&&sl<32)sl++;
    f[fl++]=0; f[fl++]=(u8)sl; for(int i=0;i<sl;i++)f[fl++]=(u8)ssid[i];   /* SSID IE */
    fl+=rtw_assoc_ies(f+fl,ap);
    rtw_tx_mgmt(m,f,fl);
    if(!rtw_rx_wait_mgmt(m,0x10,ap->bssid,body,&blen,800)) return 3;   /* no assoc resp */
    if(blen>=4 && (body[2]|(body[3]<<8))!=0) return 4;                 /* assoc rejected: status */
    return 0;   /* associated! */
}

